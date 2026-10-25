# Updates a released Speecher to a new installer through the old app's own
# updater, on a disposable Windows machine, and checks what an installed user
# keeps.
#
#   $env:SPEECHER_DISPOSABLE_MACHINE = "1"
#   pwsh packaging\windows\verify-installer-update.ps1 -OldInstaller OLD.exe `
#       -NewInstaller NEW.exe -Evidence DIR
#
# Run it as an administrator in the user's desktop session: the old window, the
# tray and the global shortcut need one. It changes the machine: the hosts file
# sends github.com and api.openai.com to a local HTTPS server, a throwaway
# certificate joins the trusted roots, and the user's Speecher is reinstalled
# per-user with tests\migration\master-0.2.0.conf as its settings, Launch at
# login on, Ctrl+Alt+F9 as its global shortcut, an OpenAI key and a CLI Proxy
# key made up for the run, and a sentinel file beside speecher.exe.
#
# The released app has no feed override, so it fetches the real manifest URL,
# which the local server answers with the new installer and its SHA-256. The
# old app saves the OpenAI key in Credential Manager itself, then downloads and
# verifies the installer on its own; the script presses the old window's own
# "Restart now" through UI Automation, and the old app runs Setup. The new
# binary's requests to stub OpenAI and proxy servers show it reads both keys,
# after the update and again after an edit and a restart. Each check is a PASS
# or FAIL line in DIR\summary.txt; the exit status is 1 if any failed.
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
Add-Type -Namespace E2E -Name User32 -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true)] public static extern bool RegisterHotKey(IntPtr window, int id, uint modifiers, uint key);
[DllImport("user32.dll")] public static extern bool UnregisterHotKey(IntPtr window, int id);
'@

$Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$App = Join-Path $env:LOCALAPPDATA "Programs\Speecher"
$Exe = Join-Path $App "speecher.exe"
$Com = Join-Path $App "speecher.com"
$SettingsKey = "HKCU:\Software\io.github.firemonster612\speecher"
$RunKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$ManifestPath = "/firemonster612/speecher/releases/latest/download/update-manifest.json"
$InstallerPath = "/firemonster612/speecher/releases/download/e2e/Speecher-Setup-x64.exe"
$Shortcut = "Ctrl+Alt+F9"
$StubTranscript = "speecher migration stub transcript"
New-Item -ItemType Directory -Force $Evidence | Out-Null
$Summary = Join-Path $Evidence "summary.txt"
$KeyLog = Join-Path $Evidence "keys-seen.log"
Set-Content $Summary ""
Set-Content $KeyLog ""
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

function Quit-Speecher {
    Invoke-Speecher quit | Out-Null
    WaitFor { -not (Speecher) } 60 "speecher did not quit" | Out-Null
}

function Start-Speecher {
    Start-Process $Exe
    WaitFor { Speecher } 60 "speecher did not start" | Out-Null
    WaitFor { (Invoke-Speecher status --json) -match '"state"' } 60 "speecher did not answer" | Out-Null
}

# Ctrl+Alt+F9 as registering it here finds it: "free" when that works,
# "held" when another process has it (ERROR_HOTKEY_ALREADY_REGISTERED), or the
# error that kept it from telling.
function Get-ShortcutState {
    $controlAlt = 0x0003; $f9 = 0x78; $id = 0x5E2E
    if ([E2E.User32]::RegisterHotKey([IntPtr]::Zero, $id, $controlAlt, $f9)) {
        [E2E.User32]::UnregisterHotKey([IntPtr]::Zero, $id) | Out-Null
        return "free"
    }
    $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    return $(if ($code -eq 1409) { "held" } else { "error $code" })
}

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

function Set-Setting([string]$Name, [string]$Value) {
    $key = Join-Path $SettingsKey (Split-Path ($Name -replace '/', '\') -Parent)
    if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
    Set-ItemProperty -Path $key -Name (Split-Path ($Name -replace '/', '\') -Leaf) -Value $Value
}

function Check-State([string]$Label) {
    $expected = [ordered]@{
        "app/setupCompleted" = "true"; "app/launchAtLogin" = "true"; "shortcuts/activationMode" = "toggle"
        "audio/preRollMs" = "400"; "refinement/provider" = "anthropic"; "refinement/defaultWritingProfile" = "work"
        "openai/model" = "gpt-6-luna"; "anthropic/model" = "claude-opus-5-5"; "anthropic/auth/mode" = "cliproxy"
        "output/method" = "automatic"; "output/format" = "html"; "updates/channel" = "stable"
        "cliproxy/baseUrl" = "http://127.0.0.1:8317"; "shortcuts/toggleDictation" = $Shortcut
    }
    $wrong = @($expected.Keys | Where-Object { (Setting $_) -ne $expected[$_] } | ForEach-Object { "$_=$(Setting $_)" })
    Check "$Label.settings" ($wrong.Count -eq 0) $(if ($wrong) { $wrong -join "; " } else { "$($expected.Count) keys as expected" })
    $vocabulary = (Setting "stt/vocabularyEntries") -replace '^@ByteArray\((.*)\)$', '$1' | ConvertFrom-Json
    Check "$Label.vocabulary" ($vocabulary.Count -ge 262 -and $vocabulary[0].starred) "$($vocabulary.Count) entries"
    Check "$Label.old_window_keys_untouched" ((Setting "ui/appWindow/a/geometry") -ne "")
    # The app registers it shortly after it starts.
    $held = try { WaitFor { (Get-ShortcutState) -eq "held" } 30 "" } catch { $false }
    Check "$Label.shortcut_held" $held "$Shortcut registered by another process"
}

# Once Speecher quits the combination is free, so the holder Check-State saw
# was Speecher.
function Check-ShortcutReleased([string]$Label) {
    $state = Get-ShortcutState
    Check "$Label.shortcut_released" ($state -eq "free") "$Shortcut $state after Speecher quit"
}

# --- The servers: github.com, api.openai.com and the proxy, served here -----

function Serve-Stubs($Version, $Build, $Keys) {
    # A CA and a server certificate it signs, as github.com presents: 0.2.0
    # closes the connection to a self-signed server certificate even when it
    # is a trusted root.
    $ca = New-SelfSignedCertificate -Subject "CN=Speecher update E2E CA" -CertStoreLocation Cert:\LocalMachine\My `
        -KeyUsage CertSign, CRLSign, DigitalSignature -TextExtension @("2.5.29.19={critical}{text}ca=1") `
        -NotAfter (Get-Date).AddDays(2)
    $certificate = New-SelfSignedCertificate -DnsName "github.com", "api.openai.com" -Signer $ca `
        -CertStoreLocation Cert:\LocalMachine\My -NotAfter (Get-Date).AddDays(2)
    $public = Join-Path $env:TEMP "speecher-e2e-ca.cer"
    Export-Certificate -Cert $ca -FilePath $public | Out-Null
    Import-Certificate -FilePath $public -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    $hosts = "$env:windir\System32\drivers\etc\hosts"
    foreach ($hostName in "github.com", "api.openai.com") {
        if (-not (Select-String -Path $hosts -Pattern "$hostName # speecher-e2e" -SimpleMatch -Quiet)) {
            Add-Content $hosts "`n127.0.0.1 $hostName # speecher-e2e"
        }
    }
    $manifest = @{
        version = $Version; buildNumber = $Build
        "windows-x86_64" = @{ installer = "https://github.com$InstallerPath"; sha256 = (Get-FileHash $NewInstaller -Algorithm SHA256).Hash.ToLower() }
    } | ConvertTo-Json
    Set-Content (Join-Path $Evidence "update-manifest.json") $manifest
    # Plain HTTP/1.1 over TcpListener, TLS through SslStream on 443, with no
    # http.sys binding to leave behind. A request to api.openai.com or the
    # proxy only notes, by name, whether it carried its key.
    $job = Start-ThreadJob -ArgumentList $certificate.Thumbprint, $manifest, (Resolve-Path $NewInstaller).Path,
            (Join-Path $Evidence "https-requests.log"), $KeyLog, $ManifestPath, $InstallerPath, $Keys, $StubTranscript -ScriptBlock {
        param($thumbprint, $manifest, $installer, $log, $keyLog, $manifestPath, $installerPath, $keys, $stubTranscript)
        $certificate = Get-Item "Cert:\LocalMachine\My\$thumbprint"
        $refined = "event: response.output_text.delta`ndata: {`"type`":`"response.output_text.delta`",`"delta`":`"speecher migration refined`"}`n`n" +
            "event: response.completed`ndata: {`"type`":`"response.completed`",`"response`":{}}`n`n"
        function Read-Request($stream) {
            $head = [IO.MemoryStream]::new()
            while (-not ($head.Length -ge 4 -and [Text.Encoding]::ASCII.GetString($head.ToArray(), $head.Length - 4, 4) -eq "`r`n`r`n")) {
                $byte = $stream.ReadByte()
                if ($byte -lt 0) { break }
                $head.WriteByte($byte)
            }
            $lines = [Text.Encoding]::ASCII.GetString($head.ToArray()) -split "`r`n"
            $headers = @{}
            foreach ($line in $lines[1..($lines.Count - 1)]) { if ($line -match '^([^:]+):\s*(.*)$') { $headers[$Matches[1].ToLower()] = $Matches[2] } }
            $length = [int]($headers["content-length"] ?? 0)
            $body = [byte[]]::new($length); $read = 0
            while ($read -lt $length) { $n = $stream.Read($body, $read, $length - $read); if ($n -le 0) { break }; $read += $n }
            return @{ Method = ($lines[0] -split ' ')[0]; Path = ($lines[0] -split ' ')[1]; Headers = $headers; Body = $body }
        }
        function Note-Key($name, $request) {
            $sent = "$($request.Headers['authorization']) $($request.Headers['x-api-key'])"
            $ok = $keys[$name] -and $sent.Contains($keys[$name])
            if ($ok) { Add-Content $keyLog "$(Get-Date -Format HH:mm:ss) $name" }
            return $ok
        }
        function Reply($stream, $status, [byte[]]$body, $type = "application/octet-stream") {
            $head = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 $status`r`nContent-Type: $type`r`nContent-Length: $($body.Length)`r`nConnection: close`r`n`r`n")
            $stream.Write($head, 0, $head.Length)
            $stream.Write($body, 0, $body.Length)
            $stream.Flush()
        }
        $listeners = @{
            tls = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 443)
            proxy = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 8317)
            speech = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 8318)
        }
        $listeners.Values | ForEach-Object { $_.Start() }
        while ($true) {
            $ready = @($listeners.Keys | Where-Object { $listeners[$_].Pending() })
            if (-not $ready) { Start-Sleep -Milliseconds 50; continue }
            foreach ($name in $ready) {
                $client = $listeners[$name].AcceptTcpClient()
                try {
                    $stream = $client.GetStream()
                    if ($name -eq "tls") {
                        $stream = [Net.Security.SslStream]::new($stream, $false)
                        $stream.AuthenticateAsServer($certificate, $false, [Security.Authentication.SslProtocols]::Tls12, $false)
                    }
                    $request = Read-Request $stream
                    Add-Content $log "$(Get-Date -Format HH:mm:ss) $name $($request.Method) $($request.Path)"
                    $empty = [byte[]]::new(0)
                    if ($name -eq "speech") {
                        Reply $stream "200 OK" ([Text.Encoding]::UTF8.GetBytes("{`"text`":`"$stubTranscript`"}")) "application/json"
                    } elseif ($name -eq "proxy") {
                        Reply $stream $(if (Note-Key "proxy" $request) { "502 Bad Gateway" } else { "401 Unauthorized" }) $empty
                    } elseif ($request.Method -eq "POST") {
                        if ($request.Path -eq "/v1/responses" -and (Note-Key "openai" $request)) {
                            Reply $stream "200 OK" ([Text.Encoding]::UTF8.GetBytes($refined)) "text/event-stream"
                        } else {
                            Reply $stream "401 Unauthorized" $empty
                        }
                    } elseif ($request.Path -eq $manifestPath) {
                        Reply $stream "200 OK" ([Text.Encoding]::UTF8.GetBytes($manifest))
                    } elseif ($request.Path -eq $installerPath) {
                        Reply $stream "200 OK" ([IO.File]::ReadAllBytes($installer))
                    } else {
                        Reply $stream "404 Not Found" $empty
                    }
                    if ($stream -is [Net.Security.SslStream]) { $stream.ShutdownAsync().Wait() }
                } catch {
                    Add-Content $log "$(Get-Date -Format HH:mm:ss) error $_"
                } finally {
                    $client.Close()
                }
            }
        }
    }
    WaitFor { try { (Invoke-WebRequest "https://github.com$ManifestPath" -UseBasicParsing).StatusCode -eq 200 } catch { $false } } 30 "local feed"
    return $job
}

# A second of warbling tone, for a speech server to be asked about.
function Write-Tone([string]$Path) {
    $samples = 16000
    $writer = [IO.BinaryWriter]::new([IO.File]::Create($Path))
    $writer.Write([Text.Encoding]::ASCII.GetBytes("RIFF")); $writer.Write([int](36 + 2 * $samples))
    $writer.Write([Text.Encoding]::ASCII.GetBytes("WAVEfmt ")); $writer.Write([int]16); $writer.Write([int16]1); $writer.Write([int16]1)
    $writer.Write([int]16000); $writer.Write([int]32000); $writer.Write([int16]2); $writer.Write([int16]16)
    $writer.Write([Text.Encoding]::ASCII.GetBytes("data")); $writer.Write([int](2 * $samples))
    for ($i = 0; $i -lt $samples; $i++) { $writer.Write([int16](8000 * [math]::Sin($i * (0.12 + 0.04 * [math]::Sin($i / 800))))) }
    $writer.Close()
}

# The new binary, with no window, refines a transcript through each stub,
# which takes the request only with the key the old app saved.
function Prove-Keys([string]$Label, [string]$Audio) {
    foreach ($pair in @(@("openai", "openai"), @("anthropic", "proxy"))) {
        $before = @(Get-Content $KeyLog | Where-Object { $_ -match " $($pair[1])$" }).Count
        $output = Invoke-Speecher transcribe --model endpoint --refine $pair[0] --output none --stdout $Audio
        $after = @(Get-Content $KeyLog | Where-Object { $_ -match " $($pair[1])$" }).Count
        Check "$Label.$($pair[1])_key_read" ($after -gt $before) "exit $LASTEXITCODE`: $(($output.Trim() -split "`r?`n")[-1])"
    }
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
Set-Setting "shortcuts/toggleDictation" $Shortcut
# 0.2.0 moves the OpenAI key into Credential Manager as it starts; the proxy
# key it keeps in the registry, for the new app to move.
$keys = @{ openai = "sk-e2e-" + [guid]::NewGuid().ToString("N"); proxy = "e2e-proxy-" + [guid]::NewGuid().ToString("N") }
Set-Setting "openai/apiKey" $keys.openai
Set-Setting "cliproxy/apiKey" $keys.proxy
Set-Content (Join-Path $App "sentinel.txt") "kept"

# The new build's version, from the speecher-version.txt build-installer.ps1
# writes beside the installer, so Speecher itself never runs before the update.
$build = [int]((Get-Content (Join-Path (Split-Path $NewInstaller) "speecher-version.txt")) -replace '.*\(build (\d+)\).*', '$1')
$version = (Get-Content (Join-Path (Split-Path $NewInstaller) "speecher-version.txt")) -replace '^speecher (\S+) .*', '$1'
$stubs = Serve-Stubs $version $build $keys

Start-Process $Exe
$old = WaitFor { Speecher } 60 "the old app did not start"
Check "old.running" $true "pid $($old.Id) version $((Get-Item $Exe).VersionInfo.ProductVersion)"
try {
    WaitFor { -not (Setting "openai/apiKey") } 30 "the OpenAI key stayed in the registry" | Out-Null
    Check "old.saved_key_in_keyring" $true "the old app moved the OpenAI key out of the registry"
} catch {
    Check "old.saved_key_in_keyring" $false "$_"
}
$runValue = WaitFor { (Get-ItemProperty $RunKey -ErrorAction SilentlyContinue).Speecher } 60 "no startup entry"
Check "old.startup_entry" ($runValue -like "*$Exe*") $runValue
Check "old.shortcut_held" (WaitFor { (Get-ShortcutState) -eq "held" } 30 "the old app did not register $Shortcut") "$Shortcut"
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
    WaitFor { (Invoke-Speecher status --json) -match '"state"' } 60 "the new app did not answer" | Out-Null
    Check "relaunched" $true "pid $($old.Id) -> $($new.Id)"
} catch {
    Check "relaunched" $false "$_"
}
$installedVersion = Invoke-Speecher --version
Check "version" ($installedVersion -match [regex]::Escape($version)) $installedVersion.Trim()
# Processes, not tray icons: the notification area can't be read reliably.
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
$pathEntries = @(((Get-ItemProperty "HKCU:\Environment").Path -split ';') | Where-Object { $_.TrimEnd('\') -ieq $App })
Check "one_path_entry" ($pathEntries.Count -eq 1) "$($pathEntries.Count) user Path entries for $App"
Check "open_with" ((Test-Path "HKCU:\Software\Classes\Applications\speecher.exe") -and
    (Get-ItemProperty "HKCU:\Software\Classes\.wav\OpenWithProgids" -ErrorAction SilentlyContinue).PSObject.Properties.Name -contains "Speecher.AudioFile")
$wavChoice = (Get-ItemProperty "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.wav\UserChoice" -ErrorAction SilentlyContinue).ProgId
Check "default_handler_untouched" ($wavChoice -ne "Speecher.AudioFile") "wav UserChoice=$wavChoice"
Check-State "first_launch"

# The keys, read by the new binary on its own. Transcribing a file needs a
# speech server, which a stub is.
Quit-Speecher
Check-ShortcutReleased "first_launch"
$audio = Join-Path $env:TEMP "speecher-e2e-tone.wav"
Write-Tone $audio
Set-Setting "endpoint/speech/baseUrl" "http://127.0.0.1:8318/v1"
Set-Setting "endpoint/speech/model" "stub"
Prove-Keys "after_update" $audio

# One edit through the running app, then a restart as at the next login. The
# CLI's vocabulary add saves only the vocabulary; the whole snapshot a Quick
# UI edit saves is tests/test_migration.cpp's to check.
Start-Speecher
Invoke-Speecher vocabulary add "migration e2e term" | Out-Null
Quit-Speecher
Start-Speecher
Check-State "after_edit_and_restart"
$terms = ((Setting "stt/vocabularyEntries") -replace '^@ByteArray\((.*)\)$', '$1' | ConvertFrom-Json).term
Check "edit_saved" ($terms -contains "migration e2e term")
Quit-Speecher
Check-ShortcutReleased "after_edit_and_restart"
Prove-Keys "after_edit_and_restart" $audio
reg export "HKCU\Software\io.github.firemonster612\speecher" (Join-Path $Evidence "settings-after.reg") /y | Out-Null
(Get-Content (Join-Path $Evidence "settings-after.reg")) -replace '^("apiKey")=.*$', '$1="<redacted>"' |
    Set-Content (Join-Path $Evidence "settings-after.reg")
Start-Process $Exe

Stop-Job $stubs -ErrorAction SilentlyContinue
if ($script:Failed.Count) { Write-Host "$($script:Failed.Count) failed: $($script:Failed -join ', ')"; exit 1 }
Write-Host "all passed"
