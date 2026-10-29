//! Extracts the static VS bank from the supported executable. No game data is shipped.
use std::collections::BTreeMap;
use std::ffi::{c_char, c_int, c_void, CStr};
use std::sync::Mutex;
use sha2::{Digest, Sha256};

const VERSION: u32 = 0x0001_0001; // format 1, shared translator revision 1
const EXPECTED_COUNT: usize = 210;
// The shared C translator tracks address/constant registers in static variables.
static TRANSLATOR: Mutex<()> = Mutex::new(());

unsafe extern "C" {
    fn vs_translate_ex(tokens: *const u32, bgra: u32, error: *mut c_int) -> *mut c_char;
    fn vs_constant_count() -> c_int;
    fn shader_translate_free(source: *mut c_void);
}

fn word(data: &[u8], at: usize) -> Option<u32> {
    Some(u32::from_le_bytes(data.get(at..at.checked_add(4)?)?.try_into().ok()?))
}

struct Section { va: u32, raw: usize, size: usize, executable: bool }
struct Pe<'a> { data: &'a [u8], sections: Vec<Section> }
impl<'a> Pe<'a> {
    fn parse(data: &'a [u8]) -> Result<Self, String> {
        let invalid = || "invalid PE32 shader source".to_string();
        if data.get(..2) != Some(b"MZ") { return Err(invalid()); }
        let p = word(data, 0x3c).ok_or_else(invalid)? as usize;
        if data.get(p..p.saturating_add(4)) != Some(b"PE\0\0") { return Err(invalid()); }
        let n = data.get(p + 6..p + 8).ok_or_else(invalid)?;
        let n = u16::from_le_bytes(n.try_into().unwrap()) as usize;
        let size = data.get(p + 20..p + 22).ok_or_else(invalid)?;
        let size = u16::from_le_bytes(size.try_into().unwrap()) as usize;
        let opt = p + 24;
        if data.get(opt..opt + 2) != Some(&[0x0b, 0x01]) || size < 96 || n > 96 { return Err(invalid()); }
        let base = word(data, opt + 28).ok_or_else(invalid)?;
        let mut sections = Vec::new();
        for i in 0..n {
            let at = opt + size + i * 40;
            let va = base.checked_add(word(data, at + 12).ok_or_else(invalid)?).ok_or_else(invalid)?;
            let raw = word(data, at + 20).ok_or_else(invalid)? as usize;
            let size = word(data, at + 16).ok_or_else(invalid)? as usize;
            if raw.checked_add(size).filter(|&end| end <= data.len()).is_none() { return Err(invalid()); }
            sections.push(Section { va, raw, size, executable: word(data, at + 36).ok_or_else(invalid)? & 0x2000_0000 != 0 });
        }
        Ok(Self { data, sections })
    }
    fn at(&self, va: u32) -> Option<u32> {
        self.sections.iter().find_map(|s| {
            let offset = va.checked_sub(s.va)? as usize;
            if offset.checked_add(4)? > s.size { return None; }
            word(self.data, s.raw + offset)
        })
    }
    fn declaration_bgra(&self, va: u32) -> Option<u32> {
        let mut k = 0u32;
        let mut bgra = 0;
        while k < 64 {
            let t = self.at(va.checked_add(k * 4)?)?;
            if t == u32::MAX { return Some(bgra); }
            if t >> 29 == 2 && t & (1 << 28) == 0 && (t >> 16) & 15 == 4 { bgra |= 1 << (t & 15); }
            k += 1;
            if t >> 29 == 4 { k += ((t >> 25) & 15) * 4; }
        }
        None
    }
    // Validate complete instruction boundaries before passing tokens to the C translator.
    fn tokens(&self, va: u32) -> Result<Vec<u32>, String> {
        let mut tokens = Vec::new();
        let mut i = 0u32;
        let bad = || format!("invalid static shader at {va:08x}");
        if self.at(va) != Some(0xfffe_0101) { return Err(bad()); }
        tokens.push(0xfffe_0101);
        loop {
            i += 1;
            let t = self.at(va.checked_add(i * 4).ok_or_else(bad)?).ok_or_else(bad)?;
            tokens.push(t);
            if t == 0x0000_ffff { return Ok(tokens); }
            let n = match t & 0xffff {
                0xfffe => (t >> 16) & 0x7fff,
                1 | 6 | 7 | 14 | 15 | 19 | 78 | 79 => 2,
                4 => 4,
                2 | 3 | 5 | 8..=13 | 20..=24 => 3,
                _ => return Err(bad()),
            };
            if i + n >= 4095 { return Err(bad()); }
            for _ in 0..n {
                i += 1;
                tokens.push(self.at(va.checked_add(i * 4).ok_or_else(bad)?).ok_or_else(bad)?);
            }
        }
    }
}

fn append_word(out: &mut Vec<u8>, value: u32) { out.extend_from_slice(&value.to_le_bytes()); }
fn checksum(data: &[u8]) -> u64 {
    data.iter().fold(0xcbf2_9ce4_8422_2325u64, |h, b| (h ^ u64::from(*b)).wrapping_mul(0x100_0000_01b3))
}

pub fn generate(exe: &[u8]) -> Result<Vec<u8>, String> {
    let _translator = TRANSLATOR.lock().map_err(|_| "shader translator lock poisoned".to_string())?;
    let digest = Sha256::digest(exe);
    let hex: String = digest.iter().map(|b| format!("{b:02x}")).collect();
    if hex != crate::prep::EXE_SHA256 { return Err("shader preparation requires the supported GOG executable".into()); }
    let pe = Pe::parse(exe)?;
    let mut bank = BTreeMap::new();
    for s in pe.sections.iter().filter(|s| !s.executable) {
        for at in (s.raw..s.raw + s.size.saturating_sub(15)).step_by(4) {
            if word(exe, at) != Some(u32::MAX) { continue; }
            let decl = word(exe, at + 4).unwrap();
            let func = word(exe, at + 8).unwrap();
            let stride = word(exe, at + 12).unwrap();
            if stride > 1024 || stride % 4 != 0 || pe.at(func) != Some(0xfffe_0101) { continue; }
            if let Some(bgra) = pe.declaration_bgra(decl) { bank.insert((func, decl), (bgra, stride)); }
        }
    }
    if bank.len() != EXPECTED_COUNT { return Err(format!("static shader bank: expected {EXPECTED_COUNT}, found {}", bank.len())); }
    let mut payload = Vec::new();
    for ((func, decl), (bgra, stride)) in bank {
        let tokens = pe.tokens(func)?;
        let mut error = 0;
        // Serial calls: the shared translator tracks address and constant registers per shader.
        let source = unsafe { vs_translate_ex(tokens.as_ptr(), bgra, &mut error) };
        if source.is_null() { return Err(format!("shader translation failed at {func:08x}")); }
        let bytes = unsafe { CStr::from_ptr(source).to_bytes_with_nul().to_vec() };
        let nc = unsafe { vs_constant_count() };
        unsafe { shader_translate_free(source.cast()); }
        if error != 0 || !(0..=96).contains(&nc) || bytes.len() > 131072 {
            return Err(format!("incomplete shader translation at {func:08x}: {error}"));
        }
        for value in [func, decl, bgra, stride, nc as u32, bytes.len() as u32] { append_word(&mut payload, value); }
        payload.extend_from_slice(&bytes);
        while payload.len() % 4 != 0 { payload.push(0); }
    }
    let mut out = Vec::new();
    out.extend_from_slice(b"SH4VSB1\0");
    for value in [VERSION, EXPECTED_COUNT as u32, payload.len() as u32, 0] { append_word(&mut out, value); }
    out.extend_from_slice(&digest);
    out.extend_from_slice(&checksum(&payload).to_le_bytes());
    out.extend_from_slice(&payload);
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_invalid_pe_headers_without_panicking() {
        for len in 0..256 { assert!(Pe::parse(&vec![0; len]).is_err()); }
        let mut data = vec![0; 128]; data[..2].copy_from_slice(b"MZ");
        data[0x3c..0x40].copy_from_slice(&u32::MAX.to_le_bytes());
        assert!(Pe::parse(&data).is_err());
    }
    #[test]
    fn rejects_truncated_and_unsupported_instruction_streams() {
        for tokens in [vec![0xfffe0101, 1], vec![0xfffe0101, 81, 0xffff], vec![0xfffe0101, 0xfffeffff]] {
            let data: Vec<u8> = tokens.iter().flat_map(|t: &u32| t.to_le_bytes()).collect();
            let pe = Pe { data: &data, sections: vec![Section { va: 0x400000, raw: 0, size: data.len(), executable: false }] };
            assert!(pe.tokens(0x400000).is_err());
        }
    }
    #[test]
    fn real_gog_bank_when_explicitly_provided() {
        if let Some(path) = std::env::var_os("SH4PREP_TEST_EXE") {
            let data = std::fs::read(path).unwrap();
            let bank = generate(&data).unwrap();
            assert_eq!(word(&bank, 12), Some(210));
            assert_eq!(bank, generate(&data).unwrap());
            if let Some(out) = std::env::var_os("SH4PREP_TEST_BANK") { std::fs::write(out, bank).unwrap(); }
        }
    }

    #[test]
    fn concurrent_preparations_produce_identical_banks_when_provided() {
        if let Some(path) = std::env::var_os("SH4PREP_TEST_EXE") {
            let data = std::sync::Arc::new(std::fs::read(path).unwrap());
            let expected = generate(&data).unwrap();
            let threads: Vec<_> = (0..8).map(|_| {
                let data = data.clone();
                std::thread::spawn(move || generate(&data).unwrap())
            }).collect();
            for thread in threads { assert_eq!(expected, thread.join().unwrap()); }
        }
    }
}
