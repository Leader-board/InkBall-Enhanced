#define GameExe AddBackslash(SourcePath) + "..\bin\x64\Release\inkball.exe"
#ifndef ReleaseVersion
  #error Pass ReleaseVersion via the GitHub Actions packaging step.
#endif
#define BinaryVersion GetVersionNumbersString(GameExe)

[Setup]
AppId=Leader-board.InkBall-Enhanced
AppName=InkBall Enhanced
AppVersion={#ReleaseVersion}
VersionInfoVersion={#BinaryVersion}
VersionInfoProductVersion={#BinaryVersion}
VersionInfoProductTextVersion={#ReleaseVersion}
DefaultDirName={localappdata}\Programs\InkBall-Reconstructed
DisableDirPage=auto
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
MinVersion=10.0
Uninstallable=yes
CreateUninstallRegKey=yes
UninstallDisplayIcon={app}\inkball.exe
UsePreviousAppDir=yes
OutputBaseFilename=InkBall-Setup-{#ReleaseVersion}-x64
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#GameExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\InkBall Enhanced"; Filename: "{app}\inkball.exe"
