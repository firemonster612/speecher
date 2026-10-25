# Updates a released Speecher to a new installer through the old app's own
# updater, on a disposable Windows machine, and checks what an installed user
# keeps.
#
#   $env:SPEECHER_DISPOSABLE_MACHINE = "1"
#   pwsh packaging\windows\verify-installer-update.ps1 -OldInstaller OLD.exe `
#       -NewInstaller NEW.exe -Evidence DIR
#
# Run it as an administrator in the user's desktop session: the old window and
# the tray need one. It changes the machine: the hosts file sends github.com
# to a local HTTPS server, a throwaway certificate joins the trusted roots,
# and the user's Speecher is reinstalled per-user with
# tests\migration\master-0.2.0.conf as its settings, Launch at login on and a
# sentinel file beside speecher.exe.
#
# The released app has no feed override, so it fetches the real manifest URL,
# which the local server answers with the new installer and its SHA-256. The
# old app downloads and verifies it on its own; the script presses the old
# window's own "Restart now" through UI Automation, and the old app runs
# Setup. Each check is a PASS or FAIL line in DIR\summary.txt; the exit status
# is 1 if any failed.
param(
    [Parameter(Mandatory)][string]$OldInstaller,
    [Parameter(Mandatory)][string]$NewInstaller,
    [Parameter(Mandatory)][string]$Evidence
)
$ErrorActionPreference = "Stop"
if ($env:SPEECHER_DISPOSABLE_MACHINE -ne "1") {
    throw "This edits the hosts file, the trusted roots and Speecher's state; set SPEECHER_DISPOSABLE_MACHINE=1 on a throwaway machine."
}
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes

$Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$App = Join-Path $env:LOCALAPPDATA "Programs\Speecher"
$Exe = Join-Path $App "speecher.exe"
$Com = Join-Path $App "speecher.com"
$SettingsKey = "HKCU:\Software\io.github.firemonster612\speecher"
$RunKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$ManifestPath = "/firemonster612/speecher/releases/latest/download/update-manifest.json"
$InstallerPath = "/firemonster612/speecher/releases/download/e2e/Speecher-Setup-x64.exe"
New-Item -ItemType Directory -Force $Evidence | Out-Null
$Summary = Join-Path $Evidence "summary.txt"
Set-Content $Summary ""
$script:Failed = @()

function Check($Name, $Ok, $Detail = "") {
    $line = "$Name=$(if ($Ok) { 'PASS' } else { 'FAIL' }) $Detail".TrimEnd()
    Write-Host $line
    Add-Content $Summary $line
    if (-not $Ok) { $script:Failed += $Name }
}

function WaitFor([scriptblock]$Condition, [int]$Seconds, [string]$What) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $result = & $Condition
        if ($result) { return $result }
        Start-Sleep 1
    }
    throw "Timed out: $What"
}

function Speecher { Get-Process speecher -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $Exe } }

function Invoke-Speecher { & $Com @args 2>&1 | Out-String }

# --- Settings as the old app keeps them: QSettings' registry layout ---------

# Writes an INI fixture written by QSettings into the registry the way
# QSettings writes the same values on Windows: groups as subkeys, integers as
# DWORD or QWORD, lists as multi-strings, everything else (byte arrays keep
# their @ByteArray() form) as strings.
function Import-Settings([string]$Path) {
    Remove-Item $SettingsKey -Recurse -Force -ErrorAction SilentlyContinue
    $group = ""
    foreach ($line in Get-Content $Path) {
        if ($line -match '^\[(.+)\]$') { $group = if ($Matches[1] -eq "General") { "" } else { $Matches[1] }; continue }
        if ($line -notmatch '^([^=]+)=(.*)$') { continue }
        $name = $Matches[1]; $raw = $Matches[2]
        $parts = (($group, $name) | Where-Object { $_ }) -join "\"
        $key = Join-Path $SettingsKey (Split-Path $parts -Parent)
        $leaf = Split-Path $parts -Leaf
        # New-Item -Force on an existing key would empty it.
        if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
        if ($raw.StartsWith('"') -and $raw.EndsWith('"')) {
            $value = [regex]::Replace($raw.Substring(1, $raw.Length - 2), '\\(.)', {
                param($m) switch ($m.Groups[1].Value) { 'n' { "`n" } 't' { "`t" } default { $m.Groups[1].Value } } })
            New-ItemProperty -Path $key -Name $leaf -Value $value -PropertyType String -Force | Out-Null
        } elseif ($raw -match '^-?\d+$') {
            $number = [long]$raw
            $type = if ([math]::Abs($number) -le [int]::MaxValue) { "DWord" } else { "QWord" }
            New-ItemProperty -Path $key -Name $leaf -Value $number -PropertyType $type -Force | Out-Null
        } elseif ($raw.Contains(", ") -and -not $raw.StartsWith("@")) {
            New-ItemProperty -Path $key -Name $leaf -Value ($raw -split ', ') -PropertyType MultiString -Force | Out-Null
        } else {
            New-ItemProperty -Path $key -Name $leaf -Value $raw -PropertyType String -Force | Out-Null
        }
    }
}

function Setting([string]$Name) {
    $key = Join-Path $SettingsKey (Split-Path ($Name -replace '/', '\') -Parent)
    $value = (Get-ItemProperty -Path $key -ErrorAction SilentlyContinue).(Split-Path ($Name -replace '/', '\') -Leaf)
    if ($value -is [array]) { return $value -join ", " }
    return [string]$value
}

function Check-State([string]$Label) {
    $expected = [ordered]@{
        "app/setupCompleted" = "true"; "app/launchAtLogin" = "true"; "shortcuts/activationMode" = "toggle"
        "audio/preRollMs" = "400"; "refinement/provider" = "anthropic"; "refinement/defaultWritingProfile" = "work"
        "openai/model" = "gpt-6-luna"; "anthropic/model" = "claude-opus-5-5"; "anthropic/auth/mode" = "cliproxy"
        "output/method" = "automatic"; "output/format" = "html"; "updates/channel" = "stable"
        "cliproxy/baseUrl" = "http://127.0.0.1:8317"
    }
    $wrong = @($expected.Keys | Where-Object { (Setting $_) -ne $expected[$_] } | ForEach-Object { "$_=$(Setting $_)" })
    Check "$Label.settings" ($wrong.Count -eq 0) $(if ($wrong) { $wrong -join "; " } else { "$($expected.Count) keys as expected" })
    $vocabulary = (Setting "stt/vocabularyEntries") -replace '^@ByteArray\((.*)\)$', '$1' | ConvertFrom-Json
    Check "$Label.vocabulary" ($vocabulary.Count -ge 262 -and $vocabulary[0].starred) "$($vocabulary.Count) entries"
    Check "$Label.old_window_keys_untouched" ((Setting "ui/appWindow/a/geometry") -ne "")
    $proxyInKeyring = (Setting "secrets/inKeyring") -match "cliproxy-api-key"
    Check "$Label.proxy_key_kept" ($proxyInKeyring -or (Setting "cliproxy/apiKey") -eq "fixture-proxy-key") `
        $(if ($proxyInKeyring) { "in Credential Manager" } else { "in the registry" })
}

# --- The feed: github.com, served here --------------------------------------

function Serve-Feed($Version, $Build) {
    # A CA and a server certificate it signs, as github.com presents: 0.2.0
    # closes the connection to a self-signed server certificate even when it
    # is a trusted root.
    $ca = New-SelfSignedCertificate -Subject "CN=Speecher update E2E CA" -CertStoreLocation Cert:\LocalMachine\My `
        -KeyUsage CertSign, CRLSign, DigitalSignature -TextExtension @("2.5.29.19={critical}{text}ca=1") `
        -NotAfter (Get-Date).AddDays(2)
    $certificate = New-SelfSignedCertificate -DnsName "github.com" -Signer $ca -CertStoreLocation Cert:\LocalMachine\My `
        -NotAfter (Get-Date).AddDays(2)
    $public = Join-Path $env:TEMP "speecher-e2e-ca.cer"
    Export-Certificate -Cert $ca -FilePath $public | Out-Null
    Import-Certificate -FilePath $public -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    $hosts = "$env:windir\System32\drivers\etc\hosts"
    if (-not (Select-String -Path $hosts -Pattern "speecher-e2e" -Quiet)) {
        Add-Content $hosts "`n127.0.0.1 github.com # speecher-e2e"
    }
    $manifest = @{
        version = $Version; buildNumber = $Build
        "windows-x86_64" = @{ installer = "https://github.com$InstallerPath"; sha256 = (Get-FileHash $NewInstaller -Algorithm SHA256).Hash.ToLower() }
    } | ConvertTo-Json
    Set-Content (Join-Path $Evidence "update-manifest.json") $manifest
    # A plain HTTP/1.1 server over SslStream, with no http.sys binding to
    # leave behind.
    $job = Start-ThreadJob -ArgumentList $certificate.Thumbprint, $manifest, (Resolve-Path $NewInstaller).Path,
            (Join-Path $Evidence "https-requests.log"), $ManifestPath, $InstallerPath -ScriptBlock {
        param($thumbprint, $manifest, $installer, $log, $manifestPath, $installerPath)
        $certificate = Get-Item "Cert:\LocalMachine\My\$thumbprint"
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 443)
        $listener.Start()
        while ($true) {
            $client = $listener.AcceptTcpClient()
            try {
                $tls = [Net.Security.SslStream]::new($client.GetStream(), $false)
                $tls.AuthenticateAsServer($certificate, $false, [Security.Authentication.SslProtocols]::Tls12, $false)
                $reader = [IO.StreamReader]::new($tls)
                $path = ($reader.ReadLine() -split ' ')[1]
                while ($reader.ReadLine()) { }
                Add-Content $log "$(Get-Date -Format HH:mm:ss) GET $path"
                [byte[]]$body = [byte[]]::new(0)
                if ($path -eq $manifestPath) { $body = [Text.Encoding]::UTF8.GetBytes($manifest) }
                elseif ($path -eq $installerPath) { $body = [IO.File]::ReadAllBytes($installer) }
                $status = if ($body.Length) { "200 OK" } else { "404 Not Found" }
                $head = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 $status`r`nContent-Length: $($body.Length)`r`nConnection: close`r`n`r`n")
                $tls.Write($head, 0, $head.Length)
                $tls.Write($body, 0, $body.Length)
                $tls.Flush()
                $tls.ShutdownAsync().Wait()
            } catch {
                Add-Content $log "$(Get-Date -Format HH:mm:ss) error $_"
            } finally {
                $client.Close()
            }
        }
    }
    WaitFor { try { (Invoke-WebRequest "https://github.com$ManifestPath" -UseBasicParsing).StatusCode -eq 200 } catch { $false } } 30 "local feed"
    return $job
}

# Presses a button by its accessible name in one of the process's windows, as
# a person clicks it.
function Press([int]$ProcessId, [string]$Name, [int]$Seconds = 60) {
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $named = [System.Windows.Automation.AndCondition]::new(
        [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $ProcessId),
        [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $Name))
    $button = WaitFor { $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $named) } $Seconds "no `"$Name`" button"
    $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
}

# --- The run ----------------------------------------------------------------

Get-Process speecher -ErrorAction SilentlyContinue | Stop-Process -Force
# Waits for Setup alone: Start-Process -Wait also waits for the speecher.exe
# the old installer starts.
(Start-Process $OldInstaller -PassThru -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER",
    "/LOG=$(Join-Path $Evidence 'old-install.log')").WaitForExit()
Get-Process speecher -ErrorAction SilentlyContinue | Stop-Process -Force
Check "old.installed" (Test-Path $Exe) $Exe
$oldFiles = Get-ChildItem $App -Recurse -File | ForEach-Object { $_.FullName.Substring($App.Length + 1) }
Set-Content (Join-Path $Evidence "old-files.txt") $oldFiles
Import-Settings (Join-Path $Repo "tests\migration\master-0.2.0.conf")
Set-ItemProperty (Join-Path $SettingsKey "updates") -Name lastCheckTime -Value 0 -Type QWord
Set-ItemProperty (Join-Path $SettingsKey "updates") -Name autoInstall -Value "true"
Set-Content (Join-Path $App "sentinel.txt") "kept"

# The new build's version, from the speecher-version.txt build-installer.ps1
# writes beside the installer, so Speecher itself never runs before the update.
$build = [int]((Get-Content (Join-Path (Split-Path $NewInstaller) "speecher-version.txt")) -replace '.*\(build (\d+)\).*', '$1')
$version = (Get-Content (Join-Path (Split-Path $NewInstaller) "speecher-version.txt")) -replace '^speecher (\S+) .*', '$1'
$feed = Serve-Feed $version $build

Start-Process $Exe
$old = WaitFor { Speecher } 60 "the old app did not start"
Check "old.running" $true "pid $($old.Id) version $((Get-Item $Exe).VersionInfo.ProductVersion)"
$runValue = WaitFor { (Get-ItemProperty $RunKey -ErrorAction SilentlyContinue).Speecher } 60 "no startup entry"
Check "old.startup_entry" ($runValue -like "*$Exe*") $runValue
$downloaded = Join-Path $env:LOCALAPPDATA "Speecher\updates\Speecher-Setup-$version.exe"
try {
    # The old app holds the file open while it writes it.
    $wanted = (Get-FileHash $NewInstaller).Hash
    WaitFor { try { (Get-FileHash $downloaded -ErrorAction Stop).Hash -eq $wanted } catch { $false } } 300 "no verified download" | Out-Null
    Check "old.downloaded_verified" $true $downloaded
} catch {
    Check "old.downloaded_verified" $false "$_"
}
try {
    # 0.2.0 has no speecher.com; its window holds the update banner.
    Start-Process $Exe -ArgumentList "settings"
    Press $old.Id "Restart now"
    Check "old.restart_pressed" $true
} catch {
    Check "old.restart_pressed" $false "$_"
}
try {
    $new = WaitFor { Speecher | Where-Object { $_.Id -ne $old.Id } } 300 "no relaunch after Setup"
    WaitFor { -not (Get-Process -Id $old.Id -ErrorAction SilentlyContinue) } 60 "the old process stayed" | Out-Null
    Start-Sleep 10
    Check "relaunched" $true "pid $($old.Id) -> $($new.Id)"
} catch {
    Check "relaunched" $false "$_"
}
$installedVersion = Invoke-Speecher --version
Check "version" ($installedVersion -match [regex]::Escape($version)) $installedVersion.Trim()
Check "one_process" (@(Speecher).Count -eq 1) "$(@(Speecher).Id)"
$status = Invoke-Speecher status --json
Check "cli" ($status -match '"state"') $status.Trim()

$retired = @(Get-Content (Join-Path $PSScriptRoot "..\..\packaging\windows\speecher.iss") |
    Select-String -Pattern 'Name: "\{app\}\\(Microsoft\.[^"]+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value })
$left = @($retired | Where-Object { Test-Path (Join-Path $App $_) })
Check "winui_files_removed" ($retired.Count -gt 0 -and $left.Count -eq 0) "$($retired.Count) retired, left: $($left -join ', ')"
# Every file left in the folder is one Setup wrote, by its log, besides the
# sentinel and Setup's own uninstall data.
$setupLog = Join-Path (Split-Path $downloaded) "speecher-update.log"
$written = @(Select-String -Path $setupLog -Pattern "Dest filename: (.*)$" | ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() })
$stale = @(Get-ChildItem $App -Recurse -File | Where-Object { $written -notcontains $_.FullName -and $_.Name -notin "sentinel.txt", "unins000.dat" } |
    ForEach-Object { $_.FullName.Substring($App.Length + 1) })
Copy-Item $setupLog $Evidence
Set-Content (Join-Path $Evidence "files-not-from-update.txt") $stale
Check "no_old_files_left" ($written.Count -gt 0 -and $stale.Count -eq 0) "$($written.Count) written by Setup; left: $($stale -join ', ')"
Check "sentinel_kept" ((Get-Content (Join-Path $App "sentinel.txt")) -eq "kept")
$runValue = (Get-ItemProperty $RunKey -ErrorAction SilentlyContinue).Speecher
Check "startup_entry" ($runValue -like "*$Exe*") $runValue
Check "open_with" ((Test-Path "HKCU:\Software\Classes\Applications\speecher.exe") -and
    (Get-ItemProperty "HKCU:\Software\Classes\.wav\OpenWithProgids" -ErrorAction SilentlyContinue).PSObject.Properties.Name -contains "Speecher.AudioFile")
$wavChoice = (Get-ItemProperty "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.wav\UserChoice" -ErrorAction SilentlyContinue).ProgId
Check "default_handler_untouched" ($wavChoice -ne "Speecher.AudioFile") "wav UserChoice=$wavChoice"
Check-State "first_launch"

# One edit through the running app, then a restart as at the next login.
Invoke-Speecher vocabulary add "migration e2e term" | Out-Null
Invoke-Speecher quit | Out-Null
WaitFor { -not (Speecher) } 30 "speecher did not quit" | Out-Null
Start-Process $Exe
WaitFor { Speecher } 60 "speecher did not restart" | Out-Null
Start-Sleep 5
Check-State "after_edit_and_restart"
$terms = ((Setting "stt/vocabularyEntries") -replace '^@ByteArray\((.*)\)$', '$1' | ConvertFrom-Json).term
Check "edit_saved" ($terms -contains "migration e2e term")
reg export "HKCU\Software\io.github.firemonster612\speecher" (Join-Path $Evidence "settings-after.reg") /y | Out-Null

Stop-Job $feed -ErrorAction SilentlyContinue
if ($script:Failed.Count) { Write-Host "$($script:Failed.Count) failed: $($script:Failed -join ', ')"; exit 1 }
Write-Host "all passed"
