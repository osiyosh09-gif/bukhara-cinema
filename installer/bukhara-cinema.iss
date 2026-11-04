; BUKHARA CINEMA - Inno Setup script
; Built by .github/workflows/build.yml:
;   ISCC /DAppVersion=0.1.0 /DSourceDir=<deployed folder> /DOutDir=<output> bukhara-cinema.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif
#ifndef OutDir
  #define OutDir "..\out"
#endif

[Setup]
AppId={{8F2B6A3E-5C1D-4E7A-9B0C-2D4F6A8E1B3C}
AppName=BUKHARA CINEMA
AppVersion={#AppVersion}
AppVerName=BUKHARA CINEMA {#AppVersion}
AppPublisher=BUKHARA CINEMA
DefaultDirName={autopf}\BUKHARA CINEMA
DefaultGroupName=BUKHARA CINEMA
DisableProgramGroupPage=yes
; Installs without administrator rights by default (into the user's
; Programs folder); the wizard offers "install for all users" as well.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 version 1809 or newer (Qt 6 requirement)
MinVersion=10.0.17763
OutputDir={#OutDir}
OutputBaseFilename=BukharaCinema-Setup-{#AppVersion}
SetupIconFile=..\resources\icons\bukhara-cinema.ico
UninstallDisplayIcon={app}\BukharaCinema.exe
UninstallDisplayName=BUKHARA CINEMA
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=auto
ChangesAssociations=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Registry]
; Double-clicking a .bcproj file opens it in BUKHARA CINEMA.
Root: HKA; Subkey: "Software\Classes\.bcproj"; ValueType: string; ValueName: ""; ValueData: "BukharaCinema.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\BukharaCinema.Project"; ValueType: string; ValueName: ""; ValueData: "BUKHARA CINEMA project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\BukharaCinema.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\BukharaCinema.exe,0"
Root: HKA; Subkey: "Software\Classes\BukharaCinema.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\BukharaCinema.exe"" ""%1"""

[Icons]
Name: "{autoprograms}\BUKHARA CINEMA"; Filename: "{app}\BukharaCinema.exe"
Name: "{autodesktop}\BUKHARA CINEMA"; Filename: "{app}\BukharaCinema.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\BukharaCinema.exe"; Description: "{cm:LaunchProgram,BUKHARA CINEMA}"; Flags: nowait postinstall skipifsilent
