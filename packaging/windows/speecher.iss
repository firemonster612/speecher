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
; Tells Explorer to re-read the Open with registrations below.
ChangesAssociations=yes
; And the user's Path, which install and uninstall change in [Code], so a
; new terminal finds speecher.com.
ChangesEnvironment=yes

[Files]
Source: "{#SourceDir}\app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\redist\WindowsAppRuntimeInstall-x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

; Earlier installers left these behind: the redist, now replaced by app-local
; runtime DLLs, and a nested copy of the multimedia plugins with debug builds.
[InstallDelete]
Type: files; Name: "{app}\vc_redist.x64.exe"
Type: filesandordirs; Name: "{app}\multimedia\multimedia"

[Icons]
Name: "{group}\Speecher"; Filename: "{app}\speecher.exe"

; Speecher writes its own Run value when the person turns Launch at login on.
; ValueType none leaves it alone on install, so an upgrade keeps the setting;
; uninsdeletevalue takes it off on uninstall instead of leaving a startup
; entry pointing at a removed executable.
[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "Speecher"; Flags: uninsdeletevalue

; Open with > Speecher for audio and video files. The ProgId is listed under each
; extension's OpenWithProgids only, so it never becomes the default handler.
; The file path arrives as a bare argument, which opens the Transcribe pane
; (or forwards to the running instance) with the file listed.
Root: HKCU; Subkey: "Software\Classes\Speecher.AudioFile"; ValueType: string; ValueName: ""; ValueData: "Audio or video file"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Speecher.AudioFile"; ValueType: string; ValueName: "FriendlyTypeName"; ValueData: "Audio or video file"
Root: HKCU; Subkey: "Software\Classes\Speecher.AudioFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\speecher.exe,0"
Root: HKCU; Subkey: "Software\Classes\Speecher.AudioFile\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Speecher"
Root: HKCU; Subkey: "Software\Classes\Speecher.AudioFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\speecher.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\.wav\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.mp3\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.m4a\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.aac\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.flac\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.ogg\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.oga\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.opus\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.webm\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.mp4\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.m4v\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.mov\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.mkv\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\.avi\OpenWithProgids"; ValueType: string; ValueName: "Speecher.AudioFile"; ValueData: ""; Flags: uninsdeletevalue
; Lists Speecher in the Open with dialog's app list under its own name.
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Speecher"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".wav"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".mp3"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".m4a"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".aac"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".flac"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".ogg"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".oga"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".opus"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".webm"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".mp4"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".m4v"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".mov"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".mkv"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\SupportedTypes"; ValueType: string; ValueName: ".avi"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\speecher.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\speecher.exe"" ""%1"""

[Run]
Filename: "{tmp}\WindowsAppRuntimeInstall-x64.exe"; Parameters: "--quiet"; StatusMsg: "Installing Windows App Runtime..."; Flags: runhidden waituntilterminated
Filename: "{app}\speecher.exe"; Description: "Launch Speecher"; Flags: nowait postinstall skipifsilent; Check: ShouldLaunchSpeecher
Filename: "{app}\speecher.exe"; Parameters: "{code:RestartArguments}"; Flags: nowait skipifnotsilent; Check: ShouldLaunchSpeecher

[Code]
var
  Wmi: Variant;

// Whether speecher.exe from this install folder is running. WMI rather than
// a marker the app sets, so it also finds releases older than any marker. If
// WMI is unavailable this answers no, which is how Setup behaved before.
function SpeecherRunning(): Boolean;
var
  Path: String;
  Locator, Processes: Variant;
begin
  Path := ExpandConstant('{app}\speecher.exe');
  StringChangeEx(Path, '\', '\\', True);
  StringChangeEx(Path, '''', '\''', True);
  try
    if VarIsEmpty(Wmi) then
    begin
      Locator := CreateOleObject('WbemScripting.SWbemLocator');
      Wmi := Locator.ConnectServer('.', 'root\CIMV2');
    end;
    Processes := Wmi.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE ExecutablePath = ''' + Path + '''');
    Result := Processes.Count > 0;
  except
    Result := False;
  end;
end;

// Asks the running Speecher to quit and waits up to ten seconds for it.
function QuitSpeecher(): Boolean;
var
  ResultCode: Integer;
  Waited: Integer;
begin
  Exec(ExpandConstant('{app}\speecher.exe'), 'quit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Waited := 0;
  while SpeecherRunning() and (Waited < 40) do
  begin
    Sleep(250);
    Waited := Waited + 1;
  end;
  Result := not SpeecherRunning();
end;

// Quit a running Speecher before Setup's files-in-use check. Releases before
// the tray window answered Restart Manager ignore its close request, so Setup
// would wait on them and then fail. This quits Speecher whatever is chosen on
// the Preparing page: leaving it running only ever ended in a half-replaced
// install. Anything still running falls through to Restart Manager.
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if SpeecherRunning() then
    QuitSpeecher();
  Result := '';
end;

// Whether Dir or any folder under it holds a file. A junction, or a folder
// that cannot be listed, counts as content, so nothing near it is removed.
function HoldsFiles(const Dir: String): Boolean;
var
  Find: TFindRec;
  IsJunction: Boolean;
begin
  Result := True;
  if not FindFirst(Dir, Find) then
    Exit;
  IsJunction := (Find.Attributes and FILE_ATTRIBUTE_REPARSE_POINT) <> 0;
  FindClose(Find);
  if IsJunction or not FindFirst(Dir + '\*', Find) then
    Exit;
  try
    Result := False;
    repeat
      if (Find.Name <> '.') and (Find.Name <> '..') then
        Result := ((Find.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0)
          or HoldsFiles(Dir + '\' + Find.Name);
    until Result or not FindNext(Find);
  finally
    FindClose(Find);
  end;
end;

// Removes Dir and the folders under it. RemoveDir refuses a folder that is not
// empty, so a file written since HoldsFiles looked survives.
procedure RemoveEmptyTree(const Dir: String);
var
  Find: TFindRec;
begin
  if FindFirst(Dir + '\*', Find) then
  try
    repeat
      if ((Find.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0)
         and ((Find.Attributes and FILE_ATTRIBUTE_REPARSE_POINT) = 0)
         and (Find.Name <> '.') and (Find.Name <> '..') then
        RemoveEmptyTree(Dir + '\' + Find.Name);
    until not FindNext(Find);
  finally
    FindClose(Find);
  end;
  RemoveDir(Dir);
end;

// The user's Path without any entry naming Dir, compared without case as
// Windows does. Every other entry, empty ones too, stays as it was, so the
// result equals Path when Dir is not on it.
function PathWithout(const Path, Dir: String): String;
var
  Rest, Entry: String;
  Split: Integer;
begin
  Result := '';
  Rest := Path + ';';
  repeat
    Split := Pos(';', Rest);
    Entry := Copy(Rest, 1, Split - 1);
    Rest := Copy(Rest, Split + 1, Length(Rest));
    if CompareText(Entry, Dir) <> 0 then
      Result := Result + ';' + Entry;
  until Rest = '';
  Delete(Result, 1, 1);
end;

// Puts the install folder on the user's Path, once, so `speecher` in a new
// terminal runs speecher.com.
procedure AddToPath();
var
  Dir, Path: String;
begin
  Dir := ExpandConstant('{app}');
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Path) then
    Path := '';
  if PathWithout(Path, Dir) <> Path then
    Exit;
  if (Path <> '') and (Path[Length(Path)] <> ';') then
    Path := Path + ';';
  RegWriteExpandStringValue(HKCU, 'Environment', 'Path', Path + Dir);
end;

procedure RemoveFromPath();
var
  Path, Kept: String;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Path) then
    Exit;
  Kept := PathWithout(Path, ExpandConstant('{app}'));
  if Kept <> Path then
    RegWriteExpandStringValue(HKCU, 'Environment', 'Path', Kept);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    AddToPath();
end;

// The uninstaller has no Restart Manager step. Run under a live Speecher, it
// cannot delete the locked files, so the folder stays behind with Speecher
// still running from it. Once the person has confirmed, quit Speecher; if it
// is still running, stop before anything is removed rather than leave half an
// install. The Path entry comes off here rather than afterwards because the
// uninstaller announces the environment change while it removes files, and
// terminals opened later should not look for speecher in a removed folder.
//
// Folders an earlier interrupted uninstall left behind were not created by
// this install, so Inno does not remove them and the install folder outlives
// the uninstall. Afterwards, if no file is left anywhere under the install
// folder, remove it; a folder that still holds anything, such as a shared one
// typed in as the destination, is left alone.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  case CurUninstallStep of
    usUninstall:
      begin
        while SpeecherRunning() and not QuitSpeecher() do
          if SuppressibleMsgBox('Speecher is still running. Quit it from its tray icon, then click Retry.',
                                mbError, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
            Abort;
        RemoveFromPath();
      end;
    usPostUninstall:
      if not HoldsFiles(ExpandConstant('{app}')) then
        RemoveEmptyTree(ExpandConstant('{app}'));
  end;
end;

function ShouldLaunchSpeecher(): Boolean;
begin
  Result := ExpandConstant('{param:VERIFYINSTALL|0}') <> '1';
end;

function RestartArguments(Param: String): String;
var
  I: Integer;
  Count: Integer;
  Value: String;
begin
  Count := StrToIntDef(ExpandConstant('{param:RESTARTARGCOUNT|0}'), 0);
  Result := '';
  for I := 0 to Count - 1 do
  begin
    Value := ExpandConstant('{param:RESTARTARG' + IntToStr(I) + '|}');
    if Result <> '' then
      Result := Result + ' ';
    Result := Result + AddQuotes(Value);
  end;
end;
