#ifndef AppVersion
  #error AppVersion must be supplied by the build
#endif

#ifdef DeveloperBuild
  #define ProductName "Optical CAD Developer"
  #define ProductId "{{84C349E1-52B2-4073-B9B8-97712D487643}"
  #define SetupName "OpticalCAD-Developer-" + AppVersion + "-Setup-Windows-x64"
#else
  #define ProductName "Optical CAD"
  #define ProductId "{{48B17648-C3C9-4C50-A821-073CDE587B65}"
  #define SetupName "OpticalCAD-Setup-Windows-x64"
#endif

[Setup]
AppId={#ProductId}
AppName={#ProductName}
AppVersion={#AppVersion}
AppPublisher=Roboric-sun
AppPublisherURL=https://github.com/Roboric-sun/Optical-Cad-
DefaultDirName={localappdata}\Programs\{#ProductName}
DefaultGroupName={#ProductName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\artifacts
OutputBaseFilename={#SetupName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\optical_cad.exe
CloseApplications=yes
SetupLogging=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\release\*"; DestDir: "{app}"; Excludes: "*.zip"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#ProductName}"; Filename: "{app}\bin\optical_cad.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\{#ProductName}"; Filename: "{app}\bin\optical_cad.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\optical_cad.exe"; Description: "{cm:LaunchProgram,{#ProductName}}"; Flags: nowait postinstall skipifsilent
