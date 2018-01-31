; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!

[Setup]
; NOTE: The value of AppId uniquely identifies this application.
; Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{DB53A3CB-C0CA-494E-AFD4-11C8B66A7B67}
AppName=QMC2 - M.A.M.E. Catalog Launcher II
AppVerName=QMC2 0.194
AppPublisher=The QMC2 Development Team
AppPublisherURL=http://qmc2.batcom-it.net/
AppSupportURL=http://qmc2.batcom-it.net/
AppUpdatesURL=http://qmc2.batcom-it.net/
DefaultDirName={pf}\QMC2
DefaultGroupName=QMC2
AllowNoIcons=yes
LicenseFile=c:\projects\gpl-2.0.txt
OutputDir=c:\projects\InstallerOutput
OutputBaseFilename=qmc2-win32-0.194
Compression=lzma2/max
SolidCompression=yes
InfoAfterFile=c:\projects\qmc2\package\doc\install-en.rtf

[Languages]
Name: "English"; MessagesFile: "compiler:Default.isl"; InfoAfterFile: "c:\projects\qmc2\package\doc\install-en.rtf"
Name: "French"; MessagesFile: "compiler:Languages\French.isl"; InfoAfterFile: "c:\projects\qmc2\package\doc\install-en.rtf"
Name: "German"; MessagesFile: "compiler:Languages\German.isl"; InfoAfterFile: "c:\projects\qmc2\package\doc\install-de.rtf"
Name: "Greek"; MessagesFile: "compiler:Languages\Greek.isl"; InfoAfterFile: "c:\projects\qmc2\package\doc\install-en.rtf"
Name: "Italian"; MessagesFile: "compiler:Languages\Italian.isl"; InfoAfterFile: "c:\projects\qmc2\package\doc\install-us.rtf"
Name: "Polish"; MessagesFile: "compiler:Languages\Polish.isl"; InfoAfterFile: "c:\projects\qmc2\package\doc\install-en.rtf"
Name: "Portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"; InfoAfterFile: "c:\projects\qmc2\package\doc\install-pt.rtf"
Name: "Romanian"; MessagesFile: "compiler:Languages\Romanian.isl"; InfoAfterFile: "c:\projects\qmc2\package\doc\install-en.rtf"
Name: "Spanish"; MessagesFile: "compiler:Languages\Spanish.isl"; InfoAfterFile: "c:\projects\qmc2\package\doc\install-es.rtf"
Name: "Swedish"; MessagesFile: "compiler:Languages\Swedish.isl"; InfoAfterFile: "c:\projects\qmc2\package\doc\install-en.rtf"

[Files]
Source: "c:\projects\qmc2\package\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Dirs]
Name: "{app}"; Permissions: everyone-readexec

[Icons]
Name: "{group}\{cm:ProgramOnTheWeb,QMC2}"; Filename: "http://qmc2.batcom-it.net/"
Name: "{group}\{cm:UninstallProgram,QMC2}"; Filename: "{uninstallexe}"
Name: "{group}\QMC2 (M.A.M.E.)"; Filename: "{app}\qmc2-mame.exe"; WorkingDir: "{app}"; IconFilename: "{app}\data\img\mame.ico"
Name: "{group}\QMC2 Arcade"; Filename: "{app}\qmc2-arcade.exe"; WorkingDir: "{app}"; IconFilename: "{app}\data\img\qmc2-arcade.ico"
Name: "{group}\Qt CHDMAN GUI"; Filename: "{app}\qchdman.exe"; WorkingDir: "{app}"; IconFilename: "{app}\data\img\qchdman.ico"

[CustomMessages]
English.InstallVCRuntimeEnv=Install VC++ 2010 run-time environment
French.InstallVCRuntimeEnv=Install VC++ 2010 run-time environment
German.InstallVCRuntimeEnv=VC++ 2010 Laufzeit-Umgebung installieren
Greek.InstallVCRuntimeEnv=Install VC++ 2010 run-time environment
Italian.InstallVCRuntimeEnv=Install VC++ 2010 run-time environment
Polish.InstallVCRuntimeEnv=Install VC++ 2010 run-time environment
Portuguese.InstallVCRuntimeEnv=Instalar ambiente de tempor de execução do VC++ 2010
Romanian.InstallVCRuntimeEnv=Install VC++ 2010 run-time environment
Spanish.InstallVCRuntimeEnv=Instalar entorno de ejecución VC++ 2010
Swedish.InstallVCRuntimeEnv=Install VC++ 2010 run-time environment

[Run]
Filename: "{app}\vcredist\vcredist_x86.exe"; Description: "{cm:InstallVCRuntimeEnv}"; Flags: postinstall runascurrentuser nowait unchecked skipifdoesntexist
