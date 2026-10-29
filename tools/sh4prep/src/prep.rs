//! Check of the PC installation and preparation of the `sh4/` folder for the Vita.
use sha2::{Digest, Sha256};
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};

/// SHA-256 of `SILENT HILL 4.exe` (GOG v5 80750): the recompiled code in the .vpk depends on it.
pub const EXE_SHA256: &str = "cf7081a6c78dbe51f6dc2e1c9bd978d8486f1d7b59d7a3b88e027c9b39f21dd0";
pub const EXE_NAME: &str = "SILENT HILL 4.exe";
pub const DIRS: [&str; 4] = ["data", "demo", "movie", "sound"];
pub const LANGUAGES: [&str; 5] = ["French", "English", "German", "Italian", "Spanish"];

#[derive(Debug)]
pub enum Error {
    MissingExe(PathBuf),
    UnsupportedExe(String),
    MissingFolder(String),
    Io { path: PathBuf, source: io::Error },
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::MissingExe(path) => write!(f, "{EXE_NAME} not found in {}", path.display()),
            Self::UnsupportedExe(sum) => write!(f, "Unsupported version (SHA-256 {sum}). Use the PC GOG release 1.0 gog v5 (80750)."),
            Self::MissingFolder(name) => write!(f, "Missing folder {name}: incomplete installation."),
            Self::Io { path, source } => write!(f, "{}: {source}", path.display()),
        }
    }
}

impl std::error::Error for Error {}

fn io_error(path: &Path, source: io::Error) -> Error { Error::Io { path: path.to_owned(), source } }

#[derive(Debug)]
pub struct Plan {
    pub files: Vec<(PathBuf, PathBuf)>, // (source, relative path inside sh4/)
    pub bytes: u64,
}

pub fn sha256_file(p: &Path) -> io::Result<String> {
    let mut f = fs::File::open(p)?;
    let mut h = Sha256::new();
    let mut buf = vec![0u8; 1 << 20];
    loop {
        let n = f.read(&mut buf)?;
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
    }
    Ok(h.finalize().iter().map(|b| format!("{b:02x}")).collect())
}

/// Checks the game folder and lists the files to copy.
pub fn check(game: &Path) -> Result<Plan, Error> {
    let exe = game.join(EXE_NAME);
    if !exe.is_file() {
        return Err(Error::MissingExe(game.to_owned()));
    }
    let sum = sha256_file(&exe).map_err(|e| io_error(&exe, e))?;
    if sum != EXE_SHA256 {
        return Err(Error::UnsupportedExe(sum));
    }
    let mut plan = Plan { files: vec![(exe.clone(), PathBuf::from(EXE_NAME))], bytes: 0 };
    for d in DIRS {
        let root = game.join(d);
        if !root.is_dir() {
            return Err(Error::MissingFolder(d.to_owned()));
        }
        walk(&root, Path::new(d), &mut plan.files).map_err(|e| io_error(&root, e))?;
    }
    plan.bytes = plan.files.iter().map(|(s, _)| fs::metadata(s).map(|m| m.len()).unwrap_or(0)).sum();
    Ok(plan)
}

fn walk(dir: &Path, rel: &Path, out: &mut Vec<(PathBuf, PathBuf)>) -> io::Result<()> {
    let mut entries: Vec<_> = fs::read_dir(dir)?.collect::<Result<_, _>>()?;
    entries.sort_by_key(|e| e.file_name());
    for e in entries {
        let p = e.path();
        let r = rel.join(e.file_name());
        if p.is_dir() {
            walk(&p, &r, out)?;
        } else {
            out.push((p, r));
        }
    }
    Ok(())
}

/// Copies the files into `<out>/sh4/` (files already copied at the right size are skipped)
/// then writes `sh4vita.json`. `progress(bytes done, current file)`.
pub fn prepare(plan: &Plan, out: &Path, lang: &str, mut progress: impl FnMut(u64, &Path)) -> Result<PathBuf, Error> {
    let dst_root = out.join("sh4");
    let (index, indexed_bytes) = crate::data_index::encode(&plan.files, EXE_NAME)
        .map_err(|e| io_error(&dst_root.join("files.idx"), e))?;
    if indexed_bytes != plan.bytes {
        return Err(io_error(&dst_root, io::Error::new(io::ErrorKind::InvalidData, "Game files changed since selection; select the game folder again.")));
    }
    let mut done = 0u64;
    for (src, rel) in &plan.files {
        let dst = dst_root.join(rel);
        let size = fs::metadata(src).map_err(|e| io_error(src, e))?.len();
        progress(done, rel);
        let same = fs::metadata(&dst).map(|m| m.len() == size).unwrap_or(false)
            && (rel != Path::new(EXE_NAME) || sha256_file(&dst).map(|s| s == EXE_SHA256).unwrap_or(false));
        if !same {
            if let Some(parent) = dst.parent() {
                fs::create_dir_all(parent).map_err(|e| io_error(parent, e))?;
            }
            fs::copy(src, &dst).map_err(|e| io_error(&dst, e))?;
        }
        done += size;
    }
    if done != indexed_bytes {
        return Err(io_error(&dst_root, io::Error::new(io::ErrorKind::InvalidData, "Game files changed during preparation; prepare again.")));
    }
    let index_path = dst_root.join("files.idx");
    fs::write(&index_path, index).map_err(|e| io_error(&index_path, e))?;
    progress(done, Path::new("files.idx"));
    let manifest = format!(
        "{{\n  \"format\": 1,\n  \"exe_sha256\": \"{EXE_SHA256}\",\n  \"language\": \"{lang}\",\n  \"file_index\": \"files.idx\",\n  \"files\": {},\n  \"bytes\": {},\n  \"created_by\": \"sh4prep {}\"\n}}\n",
        plan.files.len(),
        plan.bytes,
        env!("CARGO_PKG_VERSION")
    );
    let manifest_path = dst_root.join("sh4vita.json");
    fs::write(&manifest_path, manifest).map_err(|e| io_error(&manifest_path, e))?;
    progress(done, Path::new("sh4vita.json"));
    Ok(dst_root)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn refuses_wrong_exe() {
        let tmp = std::env::temp_dir().join(format!("sh4prep-test-{}", std::process::id()));
        let game = tmp.join("game");
        fs::create_dir_all(&game).unwrap();
        fs::write(game.join(EXE_NAME), b"not the right exe").unwrap();
        assert!(matches!(check(&game), Err(Error::UnsupportedExe(_))));
        fs::remove_dir_all(&tmp).unwrap();
    }

    #[test]
    fn prepares_clean_data_and_repairs_same_size_wrong_exe_when_provided() {
        if let Some(source) = std::env::var_os("SH4PREP_TEST_EXE") {
            let source = PathBuf::from(source);
            let bytes = fs::metadata(&source).unwrap().len();
            let tmp = std::env::temp_dir().join(format!("sh4prep-bank-test-{}", std::process::id()));
            let root = tmp.join("sh4");
            fs::create_dir_all(&root).unwrap();
            fs::write(root.join(EXE_NAME), vec![0; bytes as usize]).unwrap();
            fs::create_dir_all(root.join("save")).unwrap();
            fs::write(root.join("save/sh4.sav"), b"existing player progress").unwrap();
            fs::write(root.join("camera.cfg"), b"7\n").unwrap();
            let plan = Plan { files: vec![(source, PathBuf::from(EXE_NAME))], bytes };
            let mut files = Vec::new();
            prepare(&plan, &tmp, "French", |_, path| files.push(path.to_owned())).unwrap();
            assert_eq!(sha256_file(&root.join(EXE_NAME)).unwrap(), EXE_SHA256);
            assert!(!root.join("shaders.bin").exists());
            assert!(!root.join("env.txt").exists());
            assert!(!root.join("ctl.txt").exists());
            assert!(!root.join("audit-tour.txt").exists());
            assert!(fs::read_to_string(root.join("files.idx")).unwrap().starts_with("SH4FILES1\n"));
            assert!(fs::read_to_string(root.join("sh4vita.json")).unwrap().contains("\"file_index\": \"files.idx\""));
            assert_eq!(files.last(), Some(&PathBuf::from("sh4vita.json")));
            assert!(!fs::read_to_string(root.join("sh4vita.json")).unwrap().contains("shader_bank"));
            assert_eq!(fs::read(root.join("save/sh4.sav")).unwrap(), b"existing player progress");
            assert_eq!(fs::read(root.join("camera.cfg")).unwrap(), b"7\n");
            // The translated selector must keep the canonical game-language value.
            for language in LANGUAGES {
                prepare(&plan, &tmp, language, |_, _| {}).unwrap();
                let json = fs::read_to_string(root.join("sh4vita.json")).unwrap();
                assert!(json.contains(&format!("\"language\": \"{language}\"")));
                assert_eq!(fs::read(root.join("save/sh4.sav")).unwrap(), b"existing player progress");
                assert_eq!(fs::read(root.join("camera.cfg")).unwrap(), b"7\n");
            }
            fs::remove_dir_all(tmp).unwrap();
        }
    }
}
