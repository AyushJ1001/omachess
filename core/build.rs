//! Builds vendored Fairy-Stockfish and the rules bridge into the core.
//!
//! The engine is vendored rather than fetched, so this script carries no
//! build-time crate dependency of its own: it drives the C++ compiler that
//! the workspace already requires, the same way the engine's own Makefile
//! does, and hands the resulting archive to rustc.
//!
//! The engine is compiled in its large-board configuration. That is the build
//! the Variant Workshop needs (12 files by 10 ranks), and using one build for
//! both play and the workshop keeps one Rules Authority rather than two
//! engines that could answer differently.

use std::env;
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Child, Command};

/// The engine translation units, relative to the vendored `src` directory.
///
/// This is the engine Makefile's own source list minus its `main.cpp` entry
/// point and its language bindings, which Omachess replaces with the bridge.
const ENGINE_SOURCES: &[&str] = &[
    "benchmark.cpp",
    "bitbase.cpp",
    "bitboard.cpp",
    "endgame.cpp",
    "evaluate.cpp",
    "material.cpp",
    "misc.cpp",
    "movegen.cpp",
    "movepick.cpp",
    "parser.cpp",
    "partner.cpp",
    "pawns.cpp",
    "piece.cpp",
    "position.cpp",
    "psqt.cpp",
    "search.cpp",
    "thread.cpp",
    "timeman.cpp",
    "tt.cpp",
    "tune.cpp",
    "uci.cpp",
    "ucioption.cpp",
    "variant.cpp",
    "xboard.cpp",
    "nnue/evaluate_nnue.cpp",
    "nnue/features/half_ka_v2.cpp",
    "nnue/features/half_ka_v2_variants.cpp",
    "syzygy/tbprobe.cpp",
];

/// The configuration the engine expects of a hosted build.
///
/// `NNUE_EMBEDDING_OFF` keeps a neural network out of the binary. Fresh
/// player-made variants are therefore evaluated by Fairy-Stockfish's generic
/// handcrafted evaluator, which the cockpit discloses; no network ships with
/// the app.
const ENGINE_DEFINES: &[&str] = &[
    "NDEBUG",
    "IS_64BIT",
    "USE_PTHREADS",
    "NO_PREFETCH",
    "NNUE_EMBEDDING_OFF",
    "LARGEBOARDS",
    "PRECOMPUTED_MAGICS",
    "ALLVARS",
];

fn main() {
    let manifest = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let out = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR"));
    let engine = manifest.join("../vendor/fairy-stockfish/src");
    let bridge = manifest.join("rules");

    for directory in [&engine, &bridge] {
        println!("cargo:rerun-if-changed={}", directory.display());
    }
    println!("cargo:rerun-if-env-changed=CXX");
    println!("cargo:rerun-if-env-changed=CXXFLAGS");

    let objects = out.join("objects");
    fs::create_dir_all(&objects).expect("create the object directory");

    // Every translation unit is independent, so compile them all at once and
    // collect the failures afterwards.
    let mut compiling: Vec<(PathBuf, Child)> = Vec::new();
    for source in ENGINE_SOURCES {
        compiling.push(compile(&engine, source, &engine, &bridge, &objects));
    }
    compiling.push(compile(
        &bridge,
        "omachess_rules.cpp",
        &engine,
        &bridge,
        &objects,
    ));

    let mut archive_inputs = Vec::new();
    for (object, mut child) in compiling {
        let status = child.wait().expect("wait for the C++ compiler");
        if !status.success() {
            panic!("failed to compile {}", object.display());
        }
        archive_inputs.push(object);
    }

    let library = out.join("libomachess_rules.a");
    let _ = fs::remove_file(&library);
    let archiver = env::var_os("AR").unwrap_or_else(|| OsString::from("ar"));
    let status = Command::new(&archiver)
        .arg("crs")
        .arg(&library)
        .args(&archive_inputs)
        .status()
        .expect("run the archiver");
    assert!(status.success(), "failed to archive the rules library");

    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=omachess_rules");
    // The engine is C++; the Rust side has to bring its runtime along.
    println!("cargo:rustc-link-lib=dylib=stdc++");
}

/// Starts compiling `root`/`relative`, returning where its object will land.
fn compile(
    root: &Path,
    relative: &str,
    engine: &Path,
    bridge: &Path,
    objects: &Path,
) -> (PathBuf, Child) {
    let source = root.join(relative);
    // Sources live in nested directories (`nnue/features/...`), so flatten the
    // relative path into the object name to keep them distinct.
    let object = objects.join(format!("{}.o", relative.replace('/', "_")));

    let compiler = env::var_os("CXX").unwrap_or_else(|| OsString::from("c++"));
    let mut command = Command::new(compiler);
    command
        .arg("-std=c++17")
        .arg("-fno-exceptions")
        .arg("-fPIC")
        .arg(optimisation_flag())
        .args(ENGINE_DEFINES.iter().map(|define| format!("-D{define}")))
        .arg("-I")
        .arg(engine)
        .arg("-I")
        .arg(bridge)
        .arg("-c")
        .arg(&source)
        .arg("-o")
        .arg(&object);
    if let Some(flags) = env::var_os("CXXFLAGS") {
        command.args(flags.to_string_lossy().split_whitespace());
    }

    let child = command
        .spawn()
        .expect("start the C++ compiler; install a C++ toolchain to build the core");
    (object, child)
}

/// The engine is only ever asked for rules, so a debug build trades its search
/// speed for a shorter compile.
fn optimisation_flag() -> &'static str {
    match env::var("OPT_LEVEL").as_deref() {
        Ok("0") | Ok("1") => "-O1",
        _ => "-O3",
    }
}
