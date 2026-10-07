param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir
)

$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BuildDir = Resolve-Path $BuildDir
$Exe = Join-Path $BuildDir "speecher.exe"
$StageDir = Join-Path $BuildDir "installer-staging"
$AppDir = Join-Path $StageDir "app"
$RedistDir = Join-Path $StageDir "redist"
$DistDir = Join-Path $RootDir "dist"
$RuntimeInstaller = Join-Path $RedistDir "WindowsAppRuntimeInstall-x64.exe"
$RuntimeUrl = "https://aka.ms/windowsappsdk/2.4/2.4.0/windowsappruntimeinstall-x64.exe"
$RuntimeSha256 = "851c35b0b0a59ce4c55f9171f601193322fc3413143b0dc3390ea11e14cfa7fc"

if (-not (Test-Path $Exe)) {
    throw "Speecher executable not found: $Exe"
}

$VersionOutput = (& $Exe --version | Select-Object -First 1)
if ($VersionOutput -notmatch '^speecher (.+) \(build ([0-9]+)\)$') {
    throw "Could not read Speecher version from: $VersionOutput"
}
$Version = $Matches[1]

Remove-Item $StageDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item $AppDir -ItemType Directory -Force | Out-Null
New-Item $RedistDir -ItemType Directory -Force | Out-Null
New-Item $DistDir -ItemType Directory -Force | Out-Null

Copy-Item $Exe $AppDir
# cmd and PowerShell try speecher.com before speecher.exe, and wait for it.
$Launcher = Join-Path $BuildDir "speecher-console.exe"
if (-not (Test-Path $Launcher)) {
    throw "Console launcher not found: $Launcher"
}
Copy-Item $Launcher (Join-Path $AppDir "speecher.com")
$Bootstrap = Join-Path $BuildDir "Microsoft.WindowsAppRuntime.Bootstrap.dll"
if (-not (Test-Path $Bootstrap)) {
    throw "Windows App Runtime bootstrap DLL not found: $Bootstrap"
}
Copy-Item $Bootstrap $AppDir
$Winmds = Get-ChildItem $BuildDir -Filter *.winmd
if (-not $Winmds) {
    throw "Windows App Runtime winmd files were not found in $BuildDir"
}
$Winmds | Copy-Item -Destination $AppDir
# transcribe.cpp and its ggml backend modules (one per CPU instruction set,
# plus Vulkan), which it loads from its own directory.
$Transcribe = Join-Path $BuildDir "transcribe.dll"
if (-not (Test-Path $Transcribe)) {
    throw "transcribe.cpp library not found: $Transcribe"
}
Copy-Item $Transcribe $AppDir
Get-ChildItem $BuildDir -Filter "ggml*.dll" | Copy-Item -Destination $AppDir
Copy-Item (Join-Path $BuildDir "transcribe.cpp-licenses.md") $AppDir
Copy-Item (Join-Path $RootDir "THIRD_PARTY_NOTICES.md") $AppDir

$WinDeployQt = (Get-Command windeployqt.exe).Source
& $WinDeployQt --release --no-translations --no-compiler-runtime (Join-Path $AppDir "speecher.exe")
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}
# The Visual C++ runtime ships beside speecher.exe (app-local deployment)
# rather than as vc_redist.x64.exe, which installs per-machine and needs
# administrator rights that this per-user installer does not have.
if (-not $env:VCToolsRedistDir) {
    throw "VCToolsRedistDir is not set; run this from an MSVC developer shell"
}
$VcRuntime = Get-ChildItem (Join-Path $env:VCToolsRedistDir "x64\Microsoft.VC*.CRT\*.dll")
if (-not $VcRuntime) {
    throw "Visual C++ runtime DLLs not found under VCToolsRedistDir '$env:VCToolsRedistDir'"
}
$VcRuntime | Copy-Item -Destination $AppDir

$QtBin = Split-Path $WinDeployQt
$QtPlugins = Join-Path (Split-Path $QtBin) "plugins"
foreach ($Dll in "Qt6WebSockets.dll", "Qt6Multimedia.dll") {
    $Source = Join-Path $QtBin $Dll
    if (-not (Test-Path $Source)) {
        throw "Required Qt DLL not found: $Source"
    }
    Copy-Item $Source $AppDir -Force
}
$OffscreenPlugin = Join-Path $QtPlugins "platforms\qoffscreen.dll"
if (-not (Test-Path $OffscreenPlugin)) {
    throw "Qt offscreen platform plugin not found: $OffscreenPlugin"
}
New-Item (Join-Path $AppDir "platforms") -ItemType Directory -Force | Out-Null
Copy-Item $OffscreenPlugin (Join-Path $AppDir "platforms") -Force
# windeployqt brings it along with QtNetwork; without it Speecher can't tell
# when the computer is offline.
if (-not (Test-Path (Join-Path $AppDir "networkinformation\qnetworklistmanager.dll"))) {
    throw "windeployqt did not deploy Qt's network reachability plugin"
}

Invoke-WebRequest $RuntimeUrl -OutFile $RuntimeInstaller
$ActualRuntimeSha256 = (Get-FileHash $RuntimeInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualRuntimeSha256 -ne $RuntimeSha256) {
    throw "Windows App Runtime installer checksum mismatch: $ActualRuntimeSha256"
}

$Iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $Iscc) {
    $Iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
}
if (-not (Test-Path $Iscc)) {
    throw "ISCC.exe was not found"
}

$Iss = Join-Path $PSScriptRoot "speecher.iss"
& $Iscc "/DAppVersion=$Version" "/DSourceDir=$StageDir" "/DOutputDir=$DistDir" $Iss
if ($LASTEXITCODE -ne 0) {
    throw "ISCC failed with exit code $LASTEXITCODE"
}

$Installer = Join-Path $DistDir "Speecher-Setup-x64.exe"
$InstallerSha256 = (Get-FileHash $Installer -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content (Join-Path $DistDir "Speecher-Setup-x64.exe.sha256") "$InstallerSha256  Speecher-Setup-x64.exe"
Set-Content (Join-Path $DistDir "speecher-version.txt") $VersionOutput
Write-Output "Created $Installer"
Write-Output "SHA-256: $InstallerSha256"
Write-Output $VersionOutput
