# Corresponding source for Omachess releases

The signed `omachess-<version>.tar.gz` release asset is the exact corresponding
source for the matching package. It must remain published beside the binary
package; a moving default branch is not corresponding source.

It contains all Omachess code and owned assets, the complete preferred SVG
source for distributed artwork, vendored Fairy-Stockfish source at the pinned
commit recorded in `vendor/fairy-stockfish/OMACHESS-VENDORING.md`, the C/C++
bridge, and every build script.

Reproducible build inputs are version-matched in the same archive:

- `Cargo.lock` pins Rust packages and checksums.
- `packaging/PKGBUILD` pins the release URL, tarball digest, signing-key
  fingerprint, dependencies, configuration, build, test, and install commands.
- `CMakeLists.txt`, `app/CMakeLists.txt`, `tests/CMakeLists.txt`, and
  `core/build.rs` provide the complete build recipe.
- `vendor/fairy-stockfish/OMACHESS-VENDORING.md` identifies its pinned commit
  and documents the intentionally omitted upstream files.

Build with the commands in `CONTRIBUTING.md`, or use `makepkg` with the matching
PKGBUILD. The ordinarily available unmodified system libraries and general
build tools listed by PKGBUILD are not copied into the source archive.

## Release gate

A release is not ready to publish until all of these refer to the same version
and source tree:

1. Create the source archive from the clean, signed release tag, including the
   vendored Fairy-Stockfish tree.
2. Build and run the complete test suite from that archive, not from a nearby
   checkout.
3. Put the archive URL and its SHA-256 digest in the release PKGBUILD, then
   regenerate `.SRCINFO`.
4. Sign the exact source archive with the fingerprint pinned by PKGBUILD.
5. Publish the source archive, detached signature, binary package, PKGBUILD,
   and release notes together, and verify the published downloads against
   their signatures and digests.

Until this gate has completed, neither a candidate binary nor these
instructions constitute a published release or a corresponding-source offer.
