//! Interface text. Canonical language names remain unchanged in sh4vita.json.
use crate::prep::Error;

pub const NAMES: [&str; 5] = ["Français", "English", "Deutsch", "Italiano", "Español"];

pub struct Text {
    pub title: &'static str,
    pub intro: &'static str,
    pub language: &'static str,
    pub language_hint: &'static str,
    pub game: &'static str,
    pub game_hint: &'static str,
    pub output: &'static str,
    pub output_hint: &'static str,
    pub browse: &'static str,
    pub prepare: &'static str,
    pub checking: &'static str,
    pub copying: &'static str,
    pub ready: &'static str,
    pub complete: &'static str,
    pub transfer: &'static str,
    pub failed: &'static str,
    pub missing_exe: &'static str,
    pub unsupported: &'static str,
    pub missing_folder: &'static str,
    pub io_error: &'static str,
    pub system_code: &'static str,
    pub files: &'static str,
    pub footer: &'static str,
}

pub const TEXT: [Text; 5] = [
    Text {
        title: "Préparer les fichiers du jeu",
        intro: "Votre version GOG de Silent Hill 4, prête pour la PS Vita.",
        language: "Langue",
        language_hint: "Langue du préparateur et langue initiale du jeu.",
        game: "Dossier du jeu",
        game_hint: "Le dossier contenant SILENT HILL 4.exe et les données du jeu.",
        output: "Dossier de destination",
        output_hint: "Le dossier sh4 sera créé ici. Vos sauvegardes sont conservées.",
        browse: "Parcourir…",
        prepare: "Préparer les fichiers",
        checking: "Vérification de la version GOG…",
        copying: "Copie des fichiers…",
        ready: "Choisissez les deux dossiers pour commencer.",
        complete: "Les fichiers sont prêts",
        transfer: "Copiez ce dossier dans ux0:data/ sur votre Vita :",
        failed: "La préparation n’a pas abouti",
        missing_exe: "SILENT HILL 4.exe est introuvable dans ce dossier :",
        unsupported: "Version incompatible. Utilisez la version PC GOG 1.0 gog v5 (80750).",
        missing_folder: "Installation incomplète. Dossier manquant :",
        io_error: "Impossible de lire ou d’écrire ce fichier. Vérifiez les droits d’accès et l’espace disponible :",
        system_code: "Code système",
        files: "fichiers",
        footer: "GOG PC · 1.0 gog v5 (80750) · Votre propre copie du jeu est nécessaire",
    },
    Text {
        title: "Prepare your game files",
        intro: "Your GOG copy of Silent Hill 4, ready for PS Vita.",
        language: "Language",
        language_hint: "Preparer language and initial in-game language.",
        game: "Game folder",
        game_hint: "The folder containing SILENT HILL 4.exe and the game data.",
        output: "Output folder",
        output_hint: "The sh4 folder will be created here. Existing saves are kept.",
        browse: "Browse…",
        prepare: "Prepare the files",
        checking: "Checking the GOG version…",
        copying: "Copying game files…",
        ready: "Choose both folders to get started.",
        complete: "Your files are ready",
        transfer: "Copy this folder to ux0:data/ on your Vita:",
        failed: "Preparation could not finish",
        missing_exe: "SILENT HILL 4.exe was not found in this folder:",
        unsupported: "Unsupported version. Use the PC GOG release 1.0 gog v5 (80750).",
        missing_folder: "Incomplete installation. Missing folder:",
        io_error: "Could not read or write this file. Check access permissions and free space:",
        system_code: "System code",
        files: "files",
        footer: "GOG PC · 1.0 gog v5 (80750) · Your own copy of the game is required",
    },
    Text {
        title: "Spieldateien vorbereiten",
        intro: "Deine GOG-Version von Silent Hill 4, bereit für die PS Vita.",
        language: "Sprache",
        language_hint: "Sprache dieses Programms und anfängliche Spielsprache.",
        game: "Spielordner",
        game_hint: "Der Ordner mit SILENT HILL 4.exe und den Spieldaten.",
        output: "Zielordner",
        output_hint: "Hier wird der Ordner sh4 erstellt. Spielstände bleiben erhalten.",
        browse: "Durchsuchen…",
        prepare: "Dateien vorbereiten",
        checking: "GOG-Version wird geprüft…",
        copying: "Spieldateien werden kopiert…",
        ready: "Wähle beide Ordner aus, um zu beginnen.",
        complete: "Deine Dateien sind bereit",
        transfer: "Kopiere diesen Ordner nach ux0:data/ auf deiner Vita:",
        failed: "Vorbereitung konnte nicht abgeschlossen werden",
        missing_exe: "SILENT HILL 4.exe wurde in diesem Ordner nicht gefunden:",
        unsupported: "Nicht unterstützte Version. Verwende die PC-GOG-Version 1.0 gog v5 (80750).",
        missing_folder: "Unvollständige Installation. Fehlender Ordner:",
        io_error: "Datei konnte nicht gelesen oder geschrieben werden. Prüfe Zugriffsrechte und freien Speicher:",
        system_code: "Systemcode",
        files: "Dateien",
        footer: "GOG PC · 1.0 gog v5 (80750) · Eine eigene Kopie des Spiels ist erforderlich",
    },
    Text {
        title: "Prepara i file del gioco",
        intro: "La tua copia GOG di Silent Hill 4, pronta per PS Vita.",
        language: "Lingua",
        language_hint: "Lingua del preparatore e lingua iniziale del gioco.",
        game: "Cartella del gioco",
        game_hint: "La cartella con SILENT HILL 4.exe e i dati del gioco.",
        output: "Cartella di destinazione",
        output_hint: "La cartella sh4 verrà creata qui. I salvataggi esistenti restano.",
        browse: "Sfoglia…",
        prepare: "Prepara i file",
        checking: "Verifica della versione GOG…",
        copying: "Copia dei file del gioco…",
        ready: "Scegli entrambe le cartelle per iniziare.",
        complete: "I file sono pronti",
        transfer: "Copia questa cartella in ux0:data/ sulla tua Vita:",
        failed: "Impossibile completare la preparazione",
        missing_exe: "SILENT HILL 4.exe non è presente in questa cartella:",
        unsupported: "Versione non supportata. Usa la versione PC GOG 1.0 gog v5 (80750).",
        missing_folder: "Installazione incompleta. Cartella mancante:",
        io_error: "Impossibile leggere o scrivere questo file. Controlla i permessi e lo spazio libero:",
        system_code: "Codice di sistema",
        files: "file",
        footer: "GOG PC · 1.0 gog v5 (80750) · È necessaria una copia personale del gioco",
    },
    Text {
        title: "Prepara los archivos del juego",
        intro: "Tu copia GOG de Silent Hill 4, lista para PS Vita.",
        language: "Idioma",
        language_hint: "Idioma del preparador e idioma inicial del juego.",
        game: "Carpeta del juego",
        game_hint: "La carpeta con SILENT HILL 4.exe y los datos del juego.",
        output: "Carpeta de destino",
        output_hint: "Aquí se creará la carpeta sh4. Se conservan las partidas guardadas.",
        browse: "Examinar…",
        prepare: "Preparar los archivos",
        checking: "Comprobando la versión de GOG…",
        copying: "Copiando los archivos del juego…",
        ready: "Elige las dos carpetas para empezar.",
        complete: "Tus archivos están listos",
        transfer: "Copia esta carpeta a ux0:data/ en tu Vita:",
        failed: "No se pudo completar la preparación",
        missing_exe: "No se encontró SILENT HILL 4.exe en esta carpeta:",
        unsupported: "Versión no compatible. Usa la versión PC GOG 1.0 gog v5 (80750).",
        missing_folder: "Instalación incompleta. Falta la carpeta:",
        io_error: "No se pudo leer o escribir este archivo. Comprueba los permisos y el espacio libre:",
        system_code: "Código del sistema",
        files: "archivos",
        footer: "GOG PC · 1.0 gog v5 (80750) · Necesitas tu propia copia del juego",
    },
];

impl Text {
    pub fn error(&self, error: &Error) -> String {
        match error {
            Error::MissingExe(path) => format!("{}\n{}", self.missing_exe, path.display()),
            Error::UnsupportedExe(sum) => format!("{}\nSHA-256: {sum}", self.unsupported),
            Error::MissingFolder(name) => format!("{} {name}", self.missing_folder),
            Error::Io { path, source } => {
                let code = source.raw_os_error().map(|n| format!("\n{}: {n}", self.system_code)).unwrap_or_default();
                format!("{}\n{}{code}", self.io_error, path.display())
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn every_game_language_has_complete_localized_ui_and_errors() {
        assert_eq!(crate::prep::LANGUAGES.len(), TEXT.len());
        assert_eq!(NAMES.len(), TEXT.len());
        for text in &TEXT {
            let errors = [Error::MissingExe("game".into()), Error::UnsupportedExe("bad".into()),
                Error::MissingFolder("sound".into()), Error::Io { path: "output".into(), source: std::io::Error::from_raw_os_error(5) }];
            for error in errors { assert!(!text.error(&error).is_empty()); }
            assert!(!text.language.is_empty() && !text.prepare.is_empty() && !text.transfer.is_empty());
        }
        assert_eq!(crate::prep::LANGUAGES[0], "French");
        assert!(TEXT[0].error(&Error::MissingExe("jeu".into())).contains("introuvable"));
        assert!(TEXT[1].error(&Error::MissingExe("game".into())).contains("not found"));
    }
}
