# Arch chess-engine integration facts for Omachess v0.1

Research snapshot: 2026-07-24

## Decision answer

Omachess v0.1 can make already-installed Stockfish and Lc0 effectively
zero-setup on Omarchy, and can recognize an existing Komodo installation on a
best-effort basis. It should do this with a small bundled registry of known
Arch package names and expected executable paths, confirm ownership through
the local pacman database, and then interrogate each executable through a
strictly timed UCI handshake. It should not scan and execute arbitrary files in
`/usr/bin`, infer capabilities from filenames, bundle engines, or treat a
curated profile as more authoritative than the live UCI process.

For v0.1, a "ready" engine means that Omachess has:

1. found a known package-owned executable, or received an executable path
   explicitly from the user;
2. started it successfully and completed `uci` → `uciok` and `isready` →
   `readyok` within bounded time;
3. captured its reported identity and complete option schema; and
4. completed a minimal bounded search and clean shutdown.

UCI is sufficient for standard chess, Chess960 when the engine advertises the
relevant option, local play, and analysis. It is not a source for profile
artwork, a canonical semantic version, package identity, or a headline
"current engine rating." Those remain curated presentation metadata and must
never gate operation.

## What Arch can reliably tell Omachess

Arch consolidated packaged executables under `/usr/bin`; `/bin` and `/sbin`
are symlinked into that unified layout.[^arch-usrbin] The stronger local
source of truth is nevertheless pacman, not directory enumeration:
`pacman -Q` queries installed packages, `pacman -Ql` lists a package's files,
and `pacman -Qo <path>` identifies the package owning a file.[^pacman]
Packages installed from the AUR are still recorded in that local package
database.

As of the research snapshot, the official Arch package-search API returns no
exact `stockfish` or `lc0` package.[^arch-stockfish-search][^arch-lc0-search]
Both are in the user-maintained AUR:

| Profile | Current Arch/AUR evidence | Installed executable | Important qualification |
| --- | --- | --- | --- |
| Stockfish | AUR `stockfish` 18-1, GPL-3.0 | `/usr/bin/stockfish` | The PKGBUILD builds Stockfish and runs its upstream `make PREFIX=/usr install`; it embeds the two release NNUE files during the build.[^aur-stockfish][^aur-stockfish-pkgbuild] |
| Lc0 | AUR `lc0` 0.32.1-3, GPL-3.0-or-later | `/usr/bin/lc0` | The package depends on an `lc0-network` provider and OpenBLAS, conditionally enables cuDNN at build time, and lists other compute backends as optional. Network choice and backend materially distinguish usable configurations.[^aur-lc0][^aur-lc0-pkgbuild] |
| Komodo | AUR `komodo-engine` 14.1-1, custom license, flagged out of date | `/usr/bin/komodo` and `/usr/bin/komodo-generic` | The package downloads a prebuilt proprietary binary. Its included notice says even freeware Komodo versions may not be redistributed elsewhere.[^aur-komodo-rpc][^aur-komodo-pkgbuild][^aur-komodo-copying] |

The AUR itself warns that its packages are user-produced content used at the
user's own risk.[^aur-stockfish] Therefore an AUR package name and expected
path are useful discovery hints, not a compatibility guarantee. Omachess must
still probe the installed process.

There is no durable basis for silently installing Komodo in v0.1. The official
Komodo/Dragon site says sales ended on 2026-05-31, its final production release
was Dragon 3.3 from October 2023, and the site will remain available only until
2026-07-31 for previous purchasers to retrieve files.[^komodo] Combined with
the no-redistribution notice and stale AUR package, this makes Komodo an
**installed-if-present compatibility profile**, not an engine Omachess should
promise to acquire.

### Recommended discovery order

For each known profile:

1. Query the local package database for known package names.
2. Resolve the expected executable and confirm its owning package and
   executable bit.
3. Canonicalize the path, but retain the package name/version/path as separate
   provenance fields.
4. Perform the bounded UCI probe described below.
5. If package discovery fails, check only the profile's explicit known
   executable names through `PATH`. Mark this provenance as "unverified local
   installation."
6. Never recursively enumerate or execute `/usr/bin`.

For a custom engine, the user explicitly selects an executable. Omachess stores
the exact path plus optional arguments and working directory, warns that the
program runs with the user's permissions, then uses the same UCI probe. This
preserves support for locally compiled or unusually packaged engines without
turning every executable on the machine into a probe target.

## What the UCI process can reliably tell Omachess

UCI communicates through newline-delimited text on the child process's
standard input and output. After `uci`, an engine identifies itself, emits
zero or more typed options, and terminates the handshake with `uciok`; the GUI
may kill an engine that does not respond in time. `isready`/`readyok`
synchronizes initialization.[^uci]

The live process is authoritative for:

- its free-form `id name` and `id author` strings;
- every option it advertises, including its type (`check`, `spin`, `combo`,
  `button`, or `string`), default, bounds, and variants;
- whether it supports standardized controls such as `MultiPV`,
  `UCI_LimitStrength` + `UCI_Elo`, and engine-specific options;
- analysis output such as principal variation, centipawn or mate score,
  depth, nodes, and `bestmove`; and
- its actual ability to initialize and search on this machine.

The protocol explicitly expects GUIs to construct controls from the option
lines, and warns them to ignore unknown `UCI_` options.[^uci] Omachess should
therefore generate the advanced settings panel from the live schema and add
polished, engine-specific controls only as optional profile overlays.

Stockfish's own integration guide confirms the process model: open the
executable, write UCI commands to `stdin`, and consume `stdout`; a meaningful
position evaluation requires a bounded `go` search.[^stockfish-integration]
Current Stockfish source advertises controls including `Threads`, `Hash`,
`MultiPV`, `UCI_Chess960`, `UCI_LimitStrength`, `UCI_Elo`, and
`SyzygyPath`.[^stockfish-options] Current Lc0 source returns an identity
containing its version and author, emits its registered options, and includes
`UCI_Chess960` among them.[^lc0-uci][^lc0-flags]

### Minimal conformance probe

Use separate startup, readiness, search, and shutdown deadlines:

1. Spawn the process without a shell; capture both stdout and stderr.
2. Send `uci`; parse identity and options until `uciok`.
3. Send only safe profile defaults, then `isready`; require `readyok`.
4. Send `ucinewgame`, `position startpos`, and a tightly bounded
   `go nodes <small-limit>` (or `go movetime <small-limit>`); require a legal
   `bestmove`.
5. Send `quit`; if the process misses the shutdown deadline, terminate it.
6. Preserve diagnostics without treating arbitrary engine output as markup.

UCI also defines registration and copy-protection messages for commercial
engines.[^uci] If v0.1 does not implement these flows, it must classify an
engine that requests them as "recognized but requires unsupported
registration" instead of hanging or claiming readiness.

## Identity, capability, and configuration model

Keep three layers separate:

### 1. Installation provenance

- executable path and resolved path;
- owning package name/version when available;
- discovery method (`pacman`, known `PATH` name, or user-selected);
- last successful probe time and executable fingerprint.

### 2. Live UCI facts

- raw `id name` and `id author`;
- complete option schema and current user overrides;
- conformance/probe state and diagnostics;
- inferred capabilities based only on advertised options and successful
  operations.

The filename is not identity, and profile matching should be conservative:
package provenance first, then normalized UCI identity. A mismatched UCI
identity should downgrade a known-path match rather than silently applying
engine-specific defaults.

### 3. Bundled profile presentation

- stable Omachess profile key and display label;
- aliases used only for matching;
- a generic, redistributable Omachess-owned icon unless separate artwork
  rights are documented;
- upstream homepage and license label/link;
- optional explanatory text and safe suggested defaults;
- optional rating record with source, list date, engine build, network where
  relevant, hardware/time-control context, and a conspicuous "estimate"
  label.

The profile is a fallback/overlay. It must not overwrite the live identity,
invent support for an unadvertised option, or prevent an unknown but conforming
UCI engine from working.

## Ratings are presentation metadata, not engine facts

`UCI_Elo` is commonly misunderstood. In the protocol it is the **requested
limited playing strength** used together with `UCI_LimitStrength`; it is not a
declaration of the engine's measured maximum rating.[^uci] Omachess must not
display this option's maximum as "engine rating."

No UCI field provides a canonical current rating. Furthermore, Lc0 explicitly
requires a neural network and supports materially different CPU/GPU backends,
while its AUR package offers multiple network-provider packages.[^lc0-readme][^aur-lc0]
An unlabeled single number for "Lc0" would therefore conceal the configuration
being rated.

For v0.1, the honest choices are either:

- omit headline ratings; or
- bundle a dated, sourced rating snapshot as explicitly non-comparable
  editorial metadata with the tested engine build/configuration.

The second choice requires a separate product decision about the rating list
and update cadence. Package-description Elo claims, especially the stale
Komodo AUR description, are not suitable as the app's source of truth.

## Artwork and license boundaries

Stockfish is GPLv3 and Lc0 is GPLv3-or-later with an additional GPL section 7
permission for specified NVIDIA-linked backends.[^stockfish-license][^lc0-license]
Stockfish's maintainers say a GUI can communicate with Stockfish as a separate,
arm's-length UCI process; distributing Stockfish itself adds the obligation to
provide the applicable license and exact corresponding source.[^stockfish-integration]
Omachess v0.1 should depend on separately installed executables and should not
copy engine binaries or neural networks into its package.

Komodo's redistribution prohibition is direct and incompatible with bundling
its executable in Omachess.[^aur-komodo-copying] Automatic recognition of a
copy already installed by its user is materially different from
redistribution, but Omachess should not download it, mirror it, or imply that
it is supplied.

None of the audited Arch manifests supplies a reusable portrait/logo asset for
these profiles. Code licensing also does not by itself establish that a
particular logo or portrait is safe to redistribute. Until each desired asset
has a documented source and redistribution terms, v0.1 should ship
Omachess-owned generic engine glyphs (with the engine name in text), not scrape
images at runtime. This keeps automatic setup offline and avoids turning
artwork provenance into an engine availability dependency.

## Concrete v0.1 support matrix

| Capability | Stockfish | Lc0 | Komodo/Dragon | Custom UCI |
| --- | --- | --- | --- | --- |
| Automatic detection | Yes, known package/path plus UCI verification | Yes, known package/path plus UCI verification | Best effort if already installed | No; explicit user-selected path |
| Standard play/analysis | Yes after probe | Yes after network/backend readiness probe | Yes after probe | Yes after probe |
| Chess960 | Enable only when advertised | Enable only when advertised | Enable only when advertised | Enable only when advertised |
| Arbitrary variants | No assumption | No assumption | No assumption | No assumption; belongs to the variant-engine decision |
| Options UI | Generated from live UCI schema with curated shortcuts | Same, including backend/network diagnostics | Same | Generated from live UCI schema |
| Headline rating | Omit or sourced snapshot | Omit or configuration-specific sourced snapshot | Omit; stale package claim is insufficient | None |
| Artwork | Omachess-owned generic icon until audited | Same | Same | User-selected later; generic by default |
| Installation by Omachess | No | No | No | No |

## Newly surfaced decision questions

1. **Choose the v0.1 engine support contract:** Is UCI-only acceptable, and
   does v0.1 intentionally reject CECP/XBoard and commercial UCI registration
   flows?
2. **Choose the engine execution trust boundary:** May known, package-owned
   engines be probed automatically on first launch, or must every newly found
   executable wait for one user confirmation?
3. **Choose the rating presentation policy:** Omit headline ratings in v0.1,
   or select one rating list plus the exact provenance fields and release
   update cadence?
4. **Choose profile asset policy:** Use only Omachess-owned generic icons in
   v0.1, or fund a separate license audit for specific upstream logos and
   portraits?
5. **Choose Lc0 configuration depth:** Is recognizing the AUR-provided
   network/backend enough, or must v0.1 expose network selection and
   hardware-backend diagnostics as first-class setup?
6. **Choose Komodo's promise level:** Keep it as best-effort
   installed-if-present compatibility, or remove it from the named v0.1
   profiles and let it use the custom-UCI path?
7. **Choose resource governance:** What app-level CPU, memory, process-count,
   and analysis concurrency limits should override or constrain UCI options?

## Primary sources

[^arch-usrbin]: Arch Linux, ["Binaries move to /usr/bin requiring update intervention"](https://archlinux.org/news/binaries-move-to-usrbin-requiring-update-intervention/).
[^pacman]: Arch Linux manual pages, [`pacman(8)` query operations](https://man.archlinux.org/man/pacman.8.en).
[^arch-stockfish-search]: Arch Linux package API, [exact official-repository search for `stockfish`](https://archlinux.org/packages/search/json/?name=stockfish).
[^arch-lc0-search]: Arch Linux package API, [exact official-repository search for `lc0`](https://archlinux.org/packages/search/json/?name=lc0).
[^aur-stockfish]: Arch Linux User Repository, [`stockfish` package record](https://aur.archlinux.org/packages/stockfish).
[^aur-stockfish-pkgbuild]: Arch Linux User Repository, [`stockfish` PKGBUILD](https://aur.archlinux.org/cgit/aur.git/plain/PKGBUILD?h=stockfish).
[^aur-lc0]: Arch Linux User Repository, [`lc0` package record](https://aur.archlinux.org/packages/lc0).
[^aur-lc0-pkgbuild]: Arch Linux User Repository, [`lc0` PKGBUILD](https://aur.archlinux.org/cgit/aur.git/plain/PKGBUILD?h=lc0).
[^aur-komodo-rpc]: Arch Linux User Repository RPC, [`komodo-engine` package record](https://aur.archlinux.org/rpc/v5/info/komodo-engine).
[^aur-komodo-pkgbuild]: Arch Linux User Repository, [`komodo-engine` PKGBUILD](https://aur.archlinux.org/cgit/aur.git/plain/PKGBUILD?h=komodo-engine).
[^aur-komodo-copying]: Arch Linux User Repository, [`komodo-engine` redistribution notice](https://aur.archlinux.org/cgit/aur.git/plain/COPYING?h=komodo-engine).
[^komodo]: Komodo Chess, [official product and shutdown notice](https://www.komodochess.com/).
[^uci]: Rudolf Huber and Stefan Meyer-Kahlen, [Universal Chess Interface specification (April 2006 copy linked by Stockfish's official integration guide)](https://backscattering.de/chess/uci/).
[^stockfish-integration]: Stockfish, [official instructions for developers: using Stockfish in a project and terms](https://official-stockfish.github.io/docs/stockfish-wiki/Developers.html#using-stockfish-in-your-own-project).
[^stockfish-options]: Stockfish source, [current engine UCI option registration](https://github.com/official-stockfish/Stockfish/blob/master/src/engine.cpp).
[^stockfish-license]: Stockfish source, [official README and GPLv3 terms](https://github.com/official-stockfish/Stockfish#terms-of-use).
[^lc0-readme]: Leela Chess Zero, [official build/run documentation](https://github.com/LeelaChessZero/lc0#building-and-running-lc0).
[^lc0-uci]: Leela Chess Zero source, [UCI loop identity, options, and handshake implementation](https://github.com/LeelaChessZero/lc0/blob/master/src/chess/uciloop.cc).
[^lc0-flags]: Leela Chess Zero, [official engine option reference](https://lczero.org/play/flags/).
[^lc0-license]: Leela Chess Zero source, [official README licensing terms](https://github.com/LeelaChessZero/lc0#license).
