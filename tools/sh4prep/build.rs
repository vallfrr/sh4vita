fn main() {
    let source = "../../port/src/d3d8/shader_translate.c";
    println!("cargo:rerun-if-changed={source}");
    println!("cargo:rerun-if-changed=../../port/src/d3d8/shader_translate.h");
    cc::Build::new()
        .file(source)
        .define("SH4_TRANSLATOR_STANDALONE", None)
        .flag_if_supported("-std=c11")
        .compile("sh4_shader_translate");
}
