#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef PackageDir
  #error PackageDir must point to the deployed application
#endif
#ifndef OutputDir
  #error OutputDir must point to the release directory
#endif

[Setup]
AppId={{2B7F5CF1-ABF4-4992-899D-B0A17F8DA51E}
AppName=XLibrary
AppVersion={#AppVersion}
AppVerName=XLibrary {#AppVersion} pre-release
AppPublisher=Lappland-Julia
AppPublisherURL=https://github.com/Lappland-Julia/XLibrary
DefaultDirName={localappdata}\Programs\XLibrary
DefaultGroupName=XLibrary
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#PackageDir}\LICENSE
SetupIconFile=..\assets\xlibrary.ico
OutputDir={#OutputDir}
OutputBaseFilename=XLibrary-{#AppVersion}-pre-release-windows-x64-setup
VersionInfoVersion={#AppVersion}.0
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\XLibrary.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Excludes: "portable.flag,data\*"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\XLibrary"; Filename: "{app}\XLibrary.exe"
Name: "{autodesktop}\XLibrary"; Filename: "{app}\XLibrary.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\XLibrary.exe"; Description: "{cm:LaunchProgram,XLibrary}"; Flags: nowait postinstall skipifsilent
