#ifndef AppVersion
  #error AppVersion must be passed to ISCC
#endif
#ifndef SourceDir
  #error SourceDir must be passed to ISCC
#endif
#ifndef OutputDir
  #error OutputDir must be passed to ISCC
#endif

[Setup]
AppId={{F7947F5B-BB74-490D-B9A2-9C05D0FC18B7}
AppName=Speecher
AppVersion={#AppVersion}
AppPublisher=Speecher
DefaultDirName={localappdata}\Programs\Speecher
DefaultGroupName=Speecher
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=Speecher
UninstallDisplayIcon={app}\speecher.exe
SetupIconFile=speecher.ico
OutputDir={#OutputDir}
OutputBaseFilename=Speecher-Setup-x64
Compression=lzma2
SolidCompression=yes

[Files]
Source: "{#SourceDir}\app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\redist\WindowsAppRuntimeInstall-x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\Speecher"; Filename: "{app}\speecher.exe"

[Run]
Filename: "{tmp}\WindowsAppRuntimeInstall-x64.exe"; Parameters: "--quiet"; StatusMsg: "Installing Windows App Runtime..."; Flags: runhidden waituntilterminated
Filename: "{app}\speecher.exe"; Description: "Launch Speecher"; Flags: nowait postinstall skipifsilent; Check: ShouldLaunchSpeecher
Filename: "{app}\speecher.exe"; Flags: nowait skipifnotsilent; Check: ShouldLaunchSpeecher

[Code]
function ShouldLaunchSpeecher(): Boolean;
begin
  Result := ExpandConstant('{param:VERIFYINSTALL|0}') <> '1';
end;
