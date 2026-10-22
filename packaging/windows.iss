#ifndef AppVersion
  #error AppVersion must be supplied by the build
#endif

[Setup]
AppId={{48B17648-C3C9-4C50-A821-073CDE587B65}
AppName=Optical CAD
AppVersion={#AppVersion}
AppPublisher=Roboric-sun
AppPublisherURL=https://github.com/Roboric-sun/Optical-Cad-
DefaultDirName={localappdata}\Programs\Optical CAD
DefaultGroupName=Optical CAD
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\artifacts
OutputBaseFilename=OpticalCAD-Setup-Windows-x64
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
Name: "{group}\Optical CAD"; Filename: "{app}\bin\optical_cad.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Optical CAD"; Filename: "{app}\bin\optical_cad.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\optical_cad.exe"; Description: "{cm:LaunchProgram,Optical CAD}"; Flags: nowait postinstall skipifsilent
