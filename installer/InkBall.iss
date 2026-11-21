#define GameExe AddBackslash(SourcePath) + "..\bin\x64\Release\inkball.exe"
#define GameVersion GetFileProductVersionString(GameExe)
#ifndef ReleaseVersion
  #define ReleaseVersion GameVersion
#endif

[Setup]
AppId=Leader-board.InkBall-Enhanced
AppName=InkBall Enhanced
AppVersion={#ReleaseVersion}
VersionInfoVersion={#GetVersionNumbersString(GameExe)}
VersionInfoProductVersion={#GetVersionNumbersString(GameExe)}
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
