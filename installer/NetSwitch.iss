#define AppVersion "2.1.0"
[Setup]
AppId={{81D4460B-C29A-4DB2-92C3-7A76B1D7BE9F}
AppName=网卡切换器
AppVersion={#AppVersion}
AppPublisher=AimeeAngelia
AppPublisherURL=https://github.com/AimeeAngelia/NetSwitch
AppSupportURL=https://github.com/AimeeAngelia/NetSwitch/issues
AppUpdatesURL=https://github.com/AimeeAngelia/NetSwitch/releases
DefaultDirName={autopf}\网卡切换器
DisableDirPage=yes
DefaultGroupName=网卡切换器
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.15063
WizardStyle=modern dynamic
SetupIconFile=..\app.ico
UninstallDisplayIcon={app}\NetSwitch.exe
Compression=lzma2
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=NetSwitch-{#AppVersion}-Setup-x64
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; Flags: unchecked

[Files]
Source: "..\build\NetSwitch.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\网卡切换器"; Filename: "{app}\NetSwitch.exe"
Name: "{commondesktop}\网卡切换器"; Filename: "{app}\NetSwitch.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\NetSwitch.exe"; Description: "启动网卡切换器"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
var
  Prepared: Boolean;
  Configured: Boolean;
  Helper: String;

function RunHelper(const Filename, Arguments: String): Boolean;
var
  ExitCode: Integer;
begin
  Result := Exec(Filename, Arguments, '', SW_HIDE, ewWaitUntilTerminated, ExitCode) and (ExitCode = 0);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if CompareText(ExpandConstant('{app}'), ExpandConstant('{autopf}\网卡切换器')) <> 0 then begin
    Result := '本版本需安装到 Program Files 下的网卡切换器目录。';
    Exit;
  end;
  ExtractTemporaryFile('NetSwitch.exe');
  Helper := ExpandConstant('{tmp}\NetSwitch.exe');
  if not RunHelper(Helper, '--prepare-update "' + ExpandConstant('{tmp}\prepare.json') + '"') then
    Result := '无法准备后台服务或关闭正在操作中的程序，请稍后重试。'
  else
    Prepared := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if not RunHelper(ExpandConstant('{app}\NetSwitch.exe'), '--install-service-all "' + ExpandConstant('{tmp}\service-install.json') + '"') then
      RaiseException('文件已解压，但后台服务安装失败。请查看安装日志并重试。');
    Configured := True;
  end;
end;

procedure DeinitializeSetup;
begin
  if Prepared and not Configured then
    RunHelper(Helper, '--start-service "' + ExpandConstant('{tmp}\service-restore.json') + '"');
end;

function InitializeUninstall: Boolean;
begin
  Result := RunHelper(ExpandConstant('{app}\NetSwitch.exe'), '--prepare-update "' + ExpandConstant('{tmp}\uninstall-prepare.json') + '"');
  if not Result then
    SuppressibleMsgBox('程序正在操作中，暂时无法卸载。请稍后重试。', mbError, MB_OK, IDOK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    if not RunHelper(ExpandConstant('{app}\NetSwitch.exe'), '--uninstall-service "' + ExpandConstant('{tmp}\service-uninstall.json') + '"') then
      RaiseException('后台服务未能移除，卸载未完成。');
end;
