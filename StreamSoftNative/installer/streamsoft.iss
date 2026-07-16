#define MyAppName "StreamSoft"
#define MyAppVersion "1.0.18"
#define MyAppPublisher "PRISSETIK"
#define MyAppExeName "streamsoft_gui.exe"
#define SourceRoot "..\build\gui\Release"
#define WebSourceDir "..\core\web"
#define CertsSourceDir "..\core\certs"
#define ToolsSourceDir "..\core\tools"
#define AdaptersSourceDir "..\adapters"
#define IconFile "..\gui\qml\assets\icons\app-icon.ico"

[Setup]
AppId={{F3A6E6A0-6C3B-4A6E-9C7B-STREAMSOFT001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=Output
OutputBaseFilename=StreamSoftSetup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile={#IconFile}
DisableProgramGroupPage=yes
WizardStyle=modern

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать значок на рабочем столе"; GroupDescription: "Дополнительные значки:"
Name: "startupicon"; Description: "Запускать StreamSoft при входе в Windows"; GroupDescription: "Автозапуск:"; Flags: unchecked

[Dirs]
Name: "{userappdata}\{#MyAppName}"
Name: "{app}\adapters\tts"
Name: "{app}\adapters\rvc"
Name: "{app}\web\media"

[Files]
Source: "{#SourceRoot}\*"; DestDir: "{app}"; Excludes: "*.log,*.exp,*.lib,*.pdb,*.ilk,connections.json,runtime_settings.json,chat_commands.json,twitch_token.json"; Flags: recursesubdirs ignoreversion
Source: "{#WebSourceDir}\*.html"; DestDir: "{app}\web"; Flags: ignoreversion
Source: "{#WebSourceDir}\static\*"; DestDir: "{app}\web\static"; Flags: recursesubdirs ignoreversion
Source: "{#CertsSourceDir}\cacert.pem"; DestDir: "{app}\certs"; Flags: ignoreversion
Source: "{#ToolsSourceDir}\yt-dlp.exe"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "{#AdaptersSourceDir}\tts\server.py"; DestDir: "{app}\adapters\tts"; Flags: ignoreversion
Source: "{#AdaptersSourceDir}\tts\requirements.txt"; DestDir: "{app}\adapters\tts"; Flags: ignoreversion
Source: "{#AdaptersSourceDir}\rvc\server.py"; DestDir: "{app}\adapters\rvc"; Flags: ignoreversion
Source: "{#AdaptersSourceDir}\rvc\requirements.txt"; DestDir: "{app}\adapters\rvc"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{userappdata}\{#MyAppName}"
Name: "{group}\Удалить {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{userappdata}\{#MyAppName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Tasks: startupicon; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{userappdata}\{#MyAppName}"; Description: "Запустить {#MyAppName}"; Flags: postinstall nowait skipifsilent unchecked
