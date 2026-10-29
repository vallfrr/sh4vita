//! Size inventory for detecting incomplete transfers without reading GB on Vita.
//! This is an accidental-corruption checksum, not an authenticity signature.
use std::fs;
use std::io;
use std::path::{Component, PathBuf};

const OFFSET: u64 = 0xcbf29ce484222325;
const PRIME: u64 = 0x100000001b3;

pub fn safe_path(path: &str, exe_name: &str) -> bool {
    if path.is_empty() || path.len() > 440 || path.bytes().any(|c| c < 32 || c == 127 || c == b'\\' || c == b':') {
        return false;
    }
    if path.split('/').any(|part| part.is_empty() || part == "." || part == "..") {
        return false;
    }
    path == exe_name || ["data/", "demo/", "movie/", "sound/"].iter().any(|dir| path.starts_with(dir))
}

pub fn encode(files: &[(PathBuf, PathBuf)], exe_name: &str) -> io::Result<(String, u64)> {
    let invalid = || io::Error::new(io::ErrorKind::InvalidData, "Invalid game file inventory");
    if files.is_empty() || files.len() > 8192 { return Err(invalid()); }
    let mut payload = String::new();
    let mut total = 0u64;
    let mut has_exe = false;
    for (source, relative) in files {
        let mut parts = Vec::new();
        for component in relative.components() {
            match component {
                Component::Normal(part) => parts.push(part.to_str().ok_or_else(invalid)?),
                _ => return Err(invalid()),
            }
        }
        let name = parts.join("/");
        if !safe_path(&name, exe_name) { return Err(invalid()); }
        let metadata = fs::metadata(source)?;
        if !metadata.is_file() { return Err(invalid()); }
        total = total.checked_add(metadata.len()).ok_or_else(invalid)?;
        if total > (1u64 << 40) { return Err(invalid()); }
        has_exe |= name == exe_name;
        payload.push_str(&format!("{}\t{}\n", metadata.len(), name));
    }
    if !has_exe { return Err(invalid()); }
    let hash = payload.bytes().fold(OFFSET, |h, b| (h ^ u64::from(b)).wrapping_mul(PRIME));
    let text = format!("SH4FILES1\n{payload}END\t{}\t{total}\t{hash:016x}\n", files.len());
    Ok((text, total))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn accepts_only_immutable_game_paths() {
        for good in ["SILENT HILL 4.exe", "data/foo.bin", "movie/m_00.pac", "sound/sub/file", "demo/demo.dat"] {
            assert!(safe_path(good, "SILENT HILL 4.exe"), "{good}");
        }
        for bad in ["", "/data/file", "../file", "data/../save/sh4.sav", "data//file", "data/./file",
                    "save/sh4.sav", "camera.cfg", "env.txt", "data/a\nb", "ux0:data/file", "data/a\\b"] {
            assert!(!safe_path(bad, "SILENT HILL 4.exe"), "{bad}");
        }
    }
    #[test]
    fn metadata_and_footer_are_deterministic() {
        let source = std::env::current_exe().unwrap();
        let size = fs::metadata(&source).unwrap().len();
        let entries = vec![(source.clone(), PathBuf::from("SILENT HILL 4.exe")), (source, PathBuf::from("data/test.bin"))];
        let (text,total) = encode(&entries, "SILENT HILL 4.exe").unwrap();
        assert_eq!(total,size*2);
        assert!(text.starts_with(&format!("SH4FILES1\n{size}\tSILENT HILL 4.exe\n{size}\tdata/test.bin\nEND\t2\t")));
        assert_eq!(text,encode(&entries, "SILENT HILL 4.exe").unwrap().0);
        assert!(encode(&entries[1..],"SILENT HILL 4.exe").is_err());
        assert!(encode(&[],"SILENT HILL 4.exe").is_err());
    }
}
