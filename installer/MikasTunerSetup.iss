; MikasTuner Inno Setup Script
; Requires Inno Setup 6 (https://jrsoftware.org/isinfo.php)
;
; Build the x64 Release before running this script:
;   msbuild MikasTuner.vcxproj /p:Configuration=Release /p:Platform=x64
;
; The resulting installer is placed in the installer\Output\ folder.

#define AppName      "MikasTuner"
#define AppVersion   "1.0"
#define AppPublisher "Mika Huttunen"
#define AppExe       "MikasTuner.exe"
#define AppURL       ""

[Setup]
AppId={{1B2372EA-ABDB-F416-34D4-64BCFA2B0F9F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
AllowNoIcons=yes
; Require 64-bit Windows
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Output
OutputDir=Output
OutputBaseFilename=MikasTunerSetup_{#AppVersion}_x64
Compression=lzma2/ultra64
SolidCompression=yes
; Appearance
WizardStyle=modern
SetupIconFile=..\res\MikasTunerIcon.ico
; Minimum Windows version: Windows 10
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main executable (x64 Release build)
Source: "..\x64\Release\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion

; Visual C++ 2019 / 2022 Redistributable (x64)
; Download vc_redist.x64.exe from https://aka.ms/vs/17/release/vc_redist.x64.exe
; and place it next to this .iss file before compiling the installer.
;Source: "vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#AppName}";                               Filename: "{app}\{#AppExe}"; IconFilename: "{app}\{#AppExe}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}";         Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";                         Filename: "{app}\{#AppExe}"; IconFilename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

; Install Visual C++ Redistributable silently (uncomment if you bundle vc_redist.x64.exe)
;Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /passive /norestart"; StatusMsg: "Installing Visual C++ Redistributable..."; Flags: waituntilterminated
