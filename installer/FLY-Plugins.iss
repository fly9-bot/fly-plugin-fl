#define AppName "FLY Plugins"
#define AppVersion "1.0"

[Setup]
AppId={{6F1D3A52-8B7E-4C1F-9A2D-FLY000000001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=FLY
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DefaultGroupName={#AppName}
UninstallDisplayName={#AppName} (VozEQ + MixChat)
OutputDir=..\out
OutputBaseFilename=FLY-Plugins-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Types]
Name: "full"; Description: "Instalar los dos plugins"
Name: "custom"; Description: "Elegir cuáles instalar"; Flags: iscustom

[Components]
Name: "vozeq"; Description: "FLY VozEQ — EQ automático para voz"; Types: full custom
Name: "mixchat"; Description: "FLY MixChat — mix por chat (type drake, type cro...)"; Types: full custom

[Files]
Source: "..\plugins\FLY VozEQ.vst3\*"; DestDir: "{commoncf64}\VST3\FLY VozEQ.vst3"; Components: vozeq; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\plugins\FLY MixChat.vst3\*"; DestDir: "{commoncf64}\VST3\FLY MixChat.vst3"; Components: mixchat; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
es.FinishedLabel=Listo. Abrí FL Studio y andá a Options > Manage plugins > Find installed plugins. Después buscalos en el Mixer como FLY VozEQ y FLY MixChat.
