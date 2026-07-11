; StreamSoft installer — Inno Setup script.
; Build with: ISCC.exe installer\streamsoft.iss  (from the StreamSoftNative directory)
; Requires a Release build already sitting in build\gui\Release (see docs/BUILD.md) —
; this script only packages what's already built, it doesn't invoke CMake itself.
;
; Program files (exe + Qt runtime) go to Program Files, which a standard user
; account can't write to without elevation. User data (connections.json,
; runtime_settings.json, twitch_token.json, chat_commands.json) is plain
; cwd-relative in the C++ code (see connections_config.hpp etc.), so every
; shortcut here points its working directory at a per-user AppData folder
; instead — core_app.hpp's ensure_writable_config_cwd() falls back to the same
; folder on its own even if a shortcut's WorkingDir setting is ever bypassed.

#define MyAppName "StreamSoft"
#define MyAppVersion "1.0.7"
#define MyAppPublisher "PRISSETIK"
#define MyAppExeName "streamsoft_gui.exe"
#define SourceRoot "..\build\gui\Release"
#define WebSourceDir "..\core\web"
#define CertsSourceDir "..\core\certs"
#define AdaptersSourceDir "..\adapters"
#define IconFile "..\gui\qml\assets\icons\app-icon.ico"

[Setup]
AppId={{F3A6E6A0-6C3B-4A6E-9C7B-STREAMSOFT001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
; Deliberately NOT setting AppMutex here: that directive makes Setup pop up
; its own "please close the app first" prompt *before* anything else runs,
; and under /SUPPRESSMSGBOXES that prompt auto-answers Cancel and aborts
; the whole install outright (confirmed live: "Defaulting to Cancel for
; suppressed message box... Got EAbort exception" — the app was never even
; given a chance to close). /CLOSEAPPLICATIONS below doesn't need AppMutex
; at all — Restart Manager finds the running process by which one holds the
; actual target files open, so that's the only mechanism this relies on for
; auto_update.hpp's silent self-update to actually close and reopen the app.
; Per-user install, no admin/UAC prompt — everything this app touches
; (Registry autostart key, connections.json/etc. via
; ensure_writable_config_cwd()) is already per-user, so a machine-wide
; Program Files install would need admin rights for zero benefit.
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

; Everything windeployqt already assembled (exe + Qt DLLs/plugins/qml
; modules) — dev-only artifacts (logs, .lib/.exp from linking, a dev
; connections.json that would otherwise ship someone's local test config)
; are explicitly excluded rather than hand-listing every real file.
[Files]
Source: "{#SourceRoot}\*"; DestDir: "{app}"; Excludes: "*.log,*.exp,*.lib,*.pdb,*.ilk,connections.json,runtime_settings.json,chat_commands.json,twitch_token.json"; Flags: recursesubdirs ignoreversion
Source: "{#WebSourceDir}\*.html"; DestDir: "{app}\web"; Flags: ignoreversion
Source: "{#WebSourceDir}\static\*"; DestDir: "{app}\web\static"; Flags: recursesubdirs ignoreversion
Source: "{#CertsSourceDir}\cacert.pem"; DestDir: "{app}\certs"; Flags: ignoreversion
; Adapter *source* (server.py + requirements.txt, a few KB) ships with the
; base install regardless of Check&Install — it's our own code, not the
; heavy runtime (venv/torch/etc.) that gets fetched separately per-module.
; RVC's live pip install (module_installer.hpp) needs server.py already
; here before it ever runs; TTS's zip already contains its own copy too,
; this is just a harmless duplicate for that one.
Source: "{#AdaptersSourceDir}\tts\server.py"; DestDir: "{app}\adapters\tts"; Flags: ignoreversion
Source: "{#AdaptersSourceDir}\tts\requirements.txt"; DestDir: "{app}\adapters\tts"; Flags: ignoreversion
Source: "{#AdaptersSourceDir}\rvc\server.py"; DestDir: "{app}\adapters\rvc"; Flags: ignoreversion
Source: "{#AdaptersSourceDir}\rvc\requirements.txt"; DestDir: "{app}\adapters\rvc"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{userappdata}\{#MyAppName}"
Name: "{group}\Удалить {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{userappdata}\{#MyAppName}"; Tasks: desktopicon

; Same launch line as the shortcuts above — Windows invokes Run-key entries
; itself on login, so this can't rely on a WorkingDir set by the icon; the
; app finds its own way to a writable folder either way via
; ensure_writable_config_cwd() regardless of whatever cwd Windows starts it
; with here.
[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; Tasks: startupicon; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{userappdata}\{#MyAppName}"; Description: "Запустить {#MyAppName}"; Flags: postinstall nowait skipifsilent unchecked
