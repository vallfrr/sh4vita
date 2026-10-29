//! sh4prep: prepares the Silent Hill 4 files (GOG PC) for the PS Vita port.
//! Without arguments: graphical interface. Otherwise: `sh4prep <game folder> <output> [language]`.
mod gui;
mod i18n;
mod prep;
mod data_index;
#[cfg(test)]
mod shaders;

use std::path::Path;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() {
        gui::run();
        return;
    }
    if args.len() < 2 || args[0] == "-h" || args[0] == "--help" {
        eprintln!("usage: sh4prep <game folder> <output folder> [language]\nlanguages: {}", prep::LANGUAGES.join(", "));
        std::process::exit(2);
    }
    let lang = args.get(2).map(String::as_str).unwrap_or("English");
    if !prep::LANGUAGES.contains(&lang) {
        eprintln!("unknown language \"{lang}\" ({})", prep::LANGUAGES.join(", "));
        std::process::exit(2);
    }
    let plan = prep::check(Path::new(&args[0])).unwrap_or_else(|e| {
        eprintln!("error: {e}");
        std::process::exit(1)
    });
    println!("{} files, {:.2} GB", plan.files.len(), plan.bytes as f64 / 1e9);
    let mut last = 0;
    let out = prep::prepare(&plan, Path::new(&args[1]), lang, |done, _| {
        let pct = done * 100 / plan.bytes.max(1);
        if pct != last {
            last = pct;
            eprint!("\r{pct:3} %");
        }
    })
    .unwrap_or_else(|e| {
        eprintln!("\nerror: {e}");
        std::process::exit(1)
    });
    println!("\ndone: copy the {} folder to ux0:data/ on the Vita", out.display());
}
