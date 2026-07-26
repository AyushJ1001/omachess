/* PROTOTYPE — Variant Workshop loop, wayfinder ticket #11.
 * Three structurally different workshop interaction models over one mock
 * definition, driven through the same nine scenarios. Question is in README.md.
 * Throwaway code: no tests, no persistence, no error handling.
 */
(() => {
  "use strict";

  // ── Variants and scenarios ───────────────────────────────────
  const VARIANTS = {
    A: { key: "A", name: "Cockpit inspector" },
    B: { key: "B", name: "Guided build stepper" },
    C: { key: "C", name: "Definition + console" },
  };
  const VARIANT_KEYS = ["A", "B", "C"];

  const SCENARIOS = [
    { id: "entry", label: "Library & create" },
    { id: "board", label: "Board & geometry" },
    { id: "pieces", label: "Pieces & Betza" },
    { id: "position", label: "Starting position" },
    { id: "rules", label: "Rule families" },
    { id: "validate", label: "Validate → Playable" },
    { id: "play", label: "Play the variant" },
    { id: "analysis", label: "Variant analysis" },
    { id: "gate", label: "Capability gate & snapshot" },
  ];
  const SCENARIO_IDS = SCENARIOS.map((s) => s.id);

  // ── Mock domain ──────────────────────────────────────────────
  const ENGINE_BUILDS = {
    full: {
      id: "full",
      short: "Fairy-Stockfish 14.0.1 · largeboards",
      label: "Fairy-Stockfish 14.0.1 (largeboards, allvars)",
      where: "App Engine Store · probed 14:02",
      maxFile: 12,
      maxRank: 10,
      present: true,
    },
    small: {
      id: "small",
      short: "Fairy-Stockfish 14.0.1 · stock build",
      label: "Fairy-Stockfish 14.0.1 (stock build)",
      where: "System install /usr/bin/fairy-stockfish",
      maxFile: 8,
      maxRank: 8,
      present: true,
    },
    none: {
      id: "none",
      short: "No fairy build detected",
      label: "No Fairy-Stockfish detected",
      where: "Stockfish 17 is present but cannot host variants",
      maxFile: 0,
      maxRank: 0,
      present: false,
    },
  };

  const PRESETS = [
    { id: "std-8x8", name: "Standard 8×8", files: 8, ranks: 8, note: "Orthodox geometry" },
    { id: "grand-10x8", name: "Grand 10×8", files: 10, ranks: 8, note: "Two extra files, orthodox depth" },
    { id: "wide-10x10", name: "Wide 10×10", files: 10, ranks: 10, note: "Deeper board, slower games" },
    { id: "max-12x10", name: "Max 12×10", files: 12, ranks: 10, note: "Engine ceiling" },
  ];

  const CATALOG = [
    { code: "K", name: "King", betza: "K", glyph: "♚", royal: true, locked: true },
    { code: "Q", name: "Queen", betza: "Q", glyph: "♛" },
    { code: "R", name: "Rook", betza: "R", glyph: "♜" },
    { code: "B", name: "Bishop", betza: "B", glyph: "♝" },
    { code: "N", name: "Knight", betza: "N", glyph: "♞" },
    { code: "P", name: "Pawn", betza: "fmWfceF", glyph: "♟", locked: true },
    { code: "A", name: "Archbishop", betza: "BN", chip: true },
    { code: "C", name: "Chancellor", betza: "RN", chip: true },
    { code: "M", name: "Amazon", betza: "QN", chip: true },
    { code: "F", name: "Ferz", betza: "F", chip: true },
    { code: "W", name: "Wazir", betza: "W", chip: true },
    { code: "G", name: "Grasshopper", betza: "gQ", chip: true },
    { code: "O", name: "Cannon", betza: "mRcpR", chip: true },
  ];

  const WHITE_GLYPH = { "♚": "♔", "♛": "♕", "♜": "♖", "♝": "♗", "♞": "♘", "♟": "♙" };

  const RULES = [
    { id: "royal", label: "Royal king & checkmate", note: "King is royal; checkmate ends the game", core: true },
    { id: "promotion", label: "Promotion", note: "Pawn-like pieces promote on the far rank" },
    { id: "castling", label: "Castling", note: "Standard-style; needs king and rooks on the home rank" },
    { id: "doubleStep", label: "Double step & en passant", note: "Pawn-like first move of two, capture in passing" },
    { id: "extinction", label: "Extinction win", note: "Losing every piece of a named type loses the game" },
    { id: "flag", label: "Flag / goal region", note: "Reaching a square set wins" },
    { id: "mandatory", label: "Mandatory capture", note: "Captures are forced when available" },
    { id: "drops", label: "Capture-to-hand drops", note: "Captured pieces return to a pocket and can be dropped", pocket: true },
  ];

  const RULES_OUT = [
    "Walling", "Atomic blast", "Petrification", "Enclosure / flipping",
    "Gating", "Multi-board", "Regional counting & chasing",
  ];

  const STAGES = [
    { id: "schema", label: "Omachess schema validation", detail: "Definition v1 fields, piece letters, geometry bounds" },
    { id: "compile", label: "Adapter compile", detail: "Deterministic Fairy-Stockfish INI generated from the definition" },
    { id: "check", label: "Engine consistency check", detail: "fairy-stockfish check, throwaway process" },
    { id: "smoke", label: "Bounded smoke test", detail: "Load · start FEN · legal moves · 200 ms search, throwaway process" },
    { id: "gate", label: "Capability gate", detail: "Detected build must host this geometry and these rule families" },
  ];

  const LIBRARY_GAMES = [
    { id: "g1", title: "vs Stockfish · evening blitz", sub: "Standard chess · in progress" },
    { id: "g2", title: "Ayush vs Guest · Sicilian", sub: "Standard chess · 1-0" },
    { id: "a1", title: "Najdorf ideas", sub: "Analysis Record · standard chess" },
  ];

  const LIBRARY_VARIANTS = [
    { id: "std", name: "Standard chess", status: "builtin", sub: "Built-in · always playable" },
    { id: "wayfarer", name: "Wayfarer Chess", status: "draft", sub: "Draft v3 · 1 blocker" },
    { id: "sentinel", name: "Sentinel Duel", status: "playable", sub: "Playable v1 · 4 records" },
    { id: "kotm", name: "King of the Middle", status: "draft", sub: "Draft v1 · never validated" },
  ];

  const START_ROWS_10x8 = [
    "rnbsqksbnr",
    "pppppppppp",
    "..........",
    "..........",
    "..........",
    "..........",
    "PPPPPPPPPP",
    "RNBSQKSBNR",
  ];

  const MID_ROWS_10x8 = [
    "rnbsqksb.r",
    "ppppp.pppp",
    "........n.",
    ".....p....",
    ".....P....",
    "........N.",
    "PPPPP.PPPP",
    "RNBSQKSB.R",
  ];

  const MOVES = [
    { n: 1, w: "f4", b: "f5" },
    { n: 2, w: "Nj3", b: "Nj6" },
    { n: 3, w: "Sc3", b: "Sh6" },
  ];

  const PVS = [
    { score: "+0.31", depth: 14, line: "4. e3 e6 5. Sd2 Sc6 6. Qe2 Qe7" },
    { score: "+0.12", depth: 14, line: "4. d4 exd4 5. Sxd4 Sc6 6. Sb5" },
    { score: "-0.08", depth: 13, line: "4. g3 Bg7 5. Bg2 O-O 6. O-O d6" },
  ];

  const BETZA_CHARS = new Set("WFDNACZGBRQKHXfblrmcipgshe0123456789".split(""));

  // ── Working definition ───────────────────────────────────────
  const initialDef = () => ({
    name: "Wayfarer Chess",
    version: 3,
    status: "draft",
    presetId: "grand-10x8",
    builtins: ["K", "Q", "R", "B", "N", "P"],
    custom: { letter: "S", name: "Sentinel", betza: "BW", mg: 380, eg: 400 },
    betzaDraft: "BW",
    rows: START_ROWS_10x8.slice(),
    rules: {
      royal: true, promotion: true, castling: true, doubleStep: true,
      extinction: false, flag: false, mandatory: false, drops: false,
    },
    usedBy: 2,
  });

  const state = {
    variant: "A",
    scenario: "entry",
    engine: "full",
    def: initialDef(),
    open: { board: false, pieces: false, position: false, rules: false, validation: true },
    step: 1,
    consoleTab: "problems",
    run: null,
    letterFixed: false,
    letterFailed: false,
    attempts: 0,
    tray: "S",
    modal: null,
    help: false,
    outline: { board: true, pieces: true, position: true, rules: true },
  };

  // ── Derived helpers ──────────────────────────────────────────
  const engine = () => ENGINE_BUILDS[state.engine];
  const preset = () => PRESETS.find((p) => p.id === state.def.presetId);
  const customLetter = () => (state.letterFixed ? "Y" : state.def.custom.letter);

  function presetAllowed(p) {
    const e = engine();
    return e.present && p.files <= e.maxFile && p.ranks <= e.maxRank;
  }

  function blockers() {
    const out = [];
    const d = state.def;
    const e = engine();
    if (d.rules.extinction && d.rules.royal) {
      out.push({
        kind: "blocker",
        where: "Rules",
        text: "Royal checkmate and Extinction both decide the game end.",
        fix: "Turn off Extinction, or make no piece royal.",
      });
    }
    if (d.rules.drops && d.rules.flag) {
      out.push({
        kind: "blocker",
        where: "Rules",
        text: "Capture-to-hand drops combined with a flag region is rejected by the engine's own consistency check.",
        fix: "Drop one of the two rule families.",
      });
    }
    if (!e.present) {
      out.push({
        kind: "blocker",
        where: "Engine",
        text: "No Fairy-Stockfish build is available to host workshop variants.",
        fix: "Install Fairy-Stockfish from the Engine Catalog.",
      });
    } else if (!presetAllowed(preset())) {
      out.push({
        kind: "blocker",
        where: "Board",
        text: `${preset().name} needs a build that reaches ${preset().files}×${preset().ranks}; the detected build stops at ${e.maxFile}×${e.maxRank}.`,
        fix: "Install a largeboards build, or move to Standard 8×8.",
      });
    }
    if (state.letterFailed && !state.letterFixed) {
      out.push({
        kind: "blocker",
        where: "Pieces",
        text: "Piece letter S is already taken by the built-in Silver General.",
        fix: "Rename the Sentinel's letter to Y.",
      });
    }
    if (state.def.rules.castling && preset().files !== 8) {
      out.push({
        kind: "warning",
        where: "Rules",
        text: `Castling on a ${preset().files}-file board uses generated king and rook files (c / i).`,
        fix: "Check the castling files before you publish.",
      });
    }
    return out;
  }

  const hardBlockers = () => blockers().filter((b) => b.kind === "blocker");
  const isPlayable = () => state.def.status === "playable" && hardBlockers().length === 0;

  function fen(rows) {
    return rows
      .map((r) => {
        let out = "";
        let empty = 0;
        for (const ch of r) {
          if (ch === ".") { empty += 1; continue; }
          if (empty) { out += empty; empty = 0; }
          out += ch === "S" || ch === "s" ? (ch === "S" ? customLetter() : customLetter().toLowerCase()) : ch;
        }
        if (empty) out += empty;
        return out;
      })
      .join("/") + " w KQkq - 0 1";
  }

  function compiledIni() {
    const d = state.def;
    const p = preset();
    const letter = customLetter().toLowerCase();
    const on = (id) => (d.rules[id] ? "true" : "false");
    return `[${d.name.toLowerCase().replace(/\s+/g, "")}:chess]
maxRank = ${p.ranks}
maxFile = ${p.files}
customPiece1 = ${letter}:${d.custom.betza}
startFen = ${fen(d.rows)}
promotionPieceTypes = qrbn${letter}
doubleStep = ${on("doubleStep")}
castling = ${on("castling")}
${p.files !== 8 ? "castlingKingsideFile = i\ncastlingQueensideFile = c\n" : ""}extinctionValue = ${d.rules.extinction ? "loss" : "none"}
mustCapture = ${on("mandatory")}
pieceDrops = ${on("drops")}
capturesToHand = ${on("drops")}`;
  }

  function positionProblems(rows) {
    const flat = rows.join("");
    const out = [];
    if (!flat.includes("k")) out.push("Black has no royal piece.");
    if (!flat.includes("K")) out.push("White has no royal piece.");
    return out;
  }

  // ── Tiny HTML helpers ────────────────────────────────────────
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  const cls = (...xs) => xs.filter(Boolean).join(" ");

  function pieceCell(ch) {
    if (ch === ".") return "";
    const white = ch === ch.toUpperCase();
    const code = ch.toUpperCase();
    if (code === "S") {
      return `<span class="chip-piece ${white ? "w" : "b"}">${esc(customLetter())}</span>`;
    }
    const entry = CATALOG.find((c) => c.code === code);
    if (!entry) return `<span class="chip-piece ${white ? "w" : "b"}">${esc(code)}</span>`;
    if (entry.chip || !entry.glyph) return `<span class="chip-piece ${white ? "w" : "b"}">${esc(code)}</span>`;
    return `<span class="glyph ${white ? "w" : "b"}">${white ? WHITE_GLYPH[entry.glyph] : entry.glyph}</span>`;
  }

  function boardHTML(opts = {}) {
    const p = preset();
    const rows = opts.rows || state.def.rows;
    const editable = !!opts.editable;
    const files = p.files;
    const cells = rows
      .map((row, r) =>
        row
          .split("")
          .map((ch, f) => {
            const dark = (r + f) % 2 === 1;
            const last = opts.last && opts.last.includes(`${r}-${f}`);
            return `<div class="${cls("sq", dark ? "dark" : "light", last && "last")}"
              ${editable ? `data-act="place" data-r="${r}" data-f="${f}"` : ""}>${pieceCell(ch)}</div>`;
          })
          .join("")
      )
      .join("");
    const badge = opts.badge
      ? `<div class="board-badge">${opts.badge}</div>`
      : "";
    return `<div class="board-wrap ${opts.size || "fit"}">
      ${badge}
      <div class="board" style="--files:${files};--ranks:${p.ranks}">${cells}</div>
      <div class="board-foot">${esc(p.name)} · ${p.files}×${p.ranks}${opts.footNote ? " · " + opts.footNote : ""}</div>
    </div>`;
  }

  function pocketHTML() {
    if (!state.def.rules.drops) return "";
    return `<div class="pocket-row">
      <div class="pocket"><span class="pocket-label">Black pocket</span>${pieceCell("n")}${pieceCell("p")}</div>
      <div class="pocket"><span class="pocket-label">White pocket</span>${pieceCell("P")}</div>
      <div class="pocket-hint">Pocket only appears when capture-to-hand is on — judge the cost of this row</div>
    </div>`;
  }

  function statusPill(extra) {
    const d = state.def;
    const playable = isPlayable();
    return `<span class="pill ${playable ? "ok" : "draft"}">${playable ? "Playable" : "Draft"} v${d.version}</span>${extra || ""}`;
  }

  function engineChip() {
    const e = engine();
    return `<span class="chip ${e.present ? "" : "bad"}" title="${esc(e.where)}">${esc(e.short)}</span>`;
  }

  function evalRailHTML(compact) {
    const rows = PVS.slice(0, compact ? 2 : 3)
      .map(
        (pv) => `<div class="pv">
          <span class="pv-score">${pv.score}</span>
          <span class="pv-depth">d${pv.depth}</span>
          <span class="pv-line">${esc(pv.line)}</span>
          <button data-act="pin" class="ghost tiny">Pin</button>
        </div>`
      )
      .join("");
    return `<div class="lpa">
      <div class="lpa-head">
        <strong>Live Position Analysis</strong>
        <button class="tiny" data-act="noop">Stop</button>
      </div>
      <div class="disclosure">
        Generic evaluation · no Wayfarer network
        <button class="link" data-act="why-generic">why?</button>
      </div>
      <div class="engine-line">${esc(engine().label)} · MultiPV 3 · 2 threads</div>
      ${rows}
      <div class="lpa-foot">
        <button data-act="computer-analysis">Run Computer Analysis…</button>
        <span class="hint">finite pass → Analysis Record</span>
      </div>
    </div>`;
  }

  function pipelineHTML(opts = {}) {
    const run = state.run;
    const rows = STAGES.map((s, i) => {
      let status = "idle";
      if (run) {
        if (run.failedAt === s.id) status = "fail";
        else if (run.index > i) status = "pass";
        else if (run.index === i) status = "run";
        else if (run.done) status = "skip";
      } else if (state.def.status === "playable") status = "pass";
      const icon = { idle: "○", run: "◐", pass: "✓", fail: "✕", skip: "–" }[status];
      return `<div class="stage ${status}">
        <span class="stage-icon">${icon}</span>
        <span class="stage-label">${esc(s.label)}</span>
        <span class="stage-detail">${esc(s.detail)}</span>
      </div>`;
    }).join("");
    const failure = run && run.failedAt
      ? `<div class="diagnostic">
          <div class="diag-head">Stopped at ${esc(STAGES.find((s) => s.id === run.failedAt).label)}</div>
          <div class="diag-text">${esc(run.message)}</div>
          <div class="diag-fix">${esc(run.fix)}</div>
          <details><summary>Engine output</summary><pre>${esc(run.raw)}</pre></details>
          <div class="diag-actions">
            ${run.failedAt === "check"
              ? `<button class="primary" data-act="fix-letter">Rename piece letter to Y and revalidate</button>
                 <button data-act="goto-pieces">Open Pieces</button>`
              : run.failedAt === "gate"
                ? `<button class="primary" data-act="goto-board">Open Board</button>
                   <button data-act="engine:full">Use a largeboards build</button>`
                : `<button class="primary" data-act="goto-rules">Open Rules</button>`}
          </div>
        </div>`
      : "";
    const success = run && run.done && !run.failedAt
      ? `<div class="diagnostic ok">
          <div class="diag-head">Playable v${state.def.version}</div>
          <div class="diag-text">Compiled, checked, and smoke-tested against ${esc(engine().short)} in a throwaway process.</div>
          <div class="diag-actions">
            <button class="primary" data-act="scenario:play">Start Played Game</button>
            <button data-act="scenario:analysis">Open Live Position Analysis</button>
          </div>
        </div>`
      : "";
    const runBtn = opts.hideRun
      ? ""
      : `<button class="primary" data-act="run-validation" ${run && !run.done ? "disabled" : ""}>
          ${run && !run.done ? "Validating…" : "Validate → Playable"}
        </button>`;
    return `<div class="pipeline">${rows}${failure}${success}
      <div class="pipeline-actions">${runBtn}</div></div>`;
  }

  function blockerListHTML() {
    const bs = blockers();
    if (!bs.length) return `<div class="clean">No blockers. Ready to validate.</div>`;
    return bs
      .map(
        (b) => `<div class="issue ${b.kind}">
          <span class="issue-where">${esc(b.where)}</span>
          <span class="issue-text">${esc(b.text)}</span>
          <span class="issue-fix">${esc(b.fix)}</span>
        </div>`
      )
      .join("");
  }

  function presetGridHTML() {
    return `<div class="preset-grid">${PRESETS.map((p) => {
      const ok = presetAllowed(p);
      const on = p.id === state.def.presetId;
      return `<button class="${cls("preset", on && "on", !ok && "gated")}" ${ok ? `data-act="preset:${p.id}"` : "disabled"}>
        <span class="preset-name">${esc(p.name)}</span>
        <span class="preset-note">${esc(p.note)}</span>
        ${ok ? "" : `<span class="preset-gate">needs ${p.files}×${p.ranks} build</span>`}
      </button>`;
    }).join("")}</div>
    <div class="capacity">Detected capacity: ${engine().present ? `${engine().maxFile}×${engine().maxRank}` : "none"} · ${esc(engine().where)}</div>`;
  }

  function pieceListHTML() {
    const d = state.def;
    const rows = CATALOG.map((c) => {
      const on = d.builtins.includes(c.code);
      return `<label class="piece-row ${on ? "on" : ""}">
        <input type="checkbox" ${on ? "checked" : ""} ${c.locked ? "disabled" : ""} data-act="piece:${c.code}" />
        <span class="piece-mark">${c.glyph ? WHITE_GLYPH[c.glyph] : `<span class="chip-piece w">${c.code}</span>`}</span>
        <span class="piece-name">${esc(c.name)}</span>
        <code>${esc(c.betza)}</code>
        ${c.royal ? `<span class="tag">royal</span>` : ""}
      </label>`;
    }).join("");
    const rejected = state.def.betzaDraft.split("").filter((c) => !BETZA_CHARS.has(c));
    return `<div class="piece-list">${rows}</div>
      <div class="custom-piece">
        <div class="custom-head">Custom piece <span class="tag">Betza subset only</span></div>
        <div class="custom-grid">
          <label>Letter<input value="${esc(customLetter())}" data-act="noop" /></label>
          <label>Name<input value="${esc(state.def.custom.name)}" data-act="noop" /></label>
          <label>Betza<input value="${esc(state.def.betzaDraft)}" data-act="betza" /></label>
          <label>Value mg / eg<input value="${state.def.custom.mg} / ${state.def.custom.eg}" data-act="noop" /></label>
        </div>
        ${rejected.length
          ? `<div class="diag-text small">Betza atom “${esc(rejected[0])}” is not in the subset Omachess supports. Supported atoms: W F D N A C Z G B R Q K, with f/b/l/r directions, m/c modalities, i for initial moves, and hopper prefixes.</div>`
          : `<div class="hint small"><code>${esc(state.def.betzaDraft)}</code> → moves as a bishop plus one orthogonal step. Type <code>yQ</code> to see a rejected atom.</div>`}
      </div>`;
  }

  function rulesHTML() {
    const d = state.def;
    const rows = RULES.map(
      (r) => `<label class="rule-row ${d.rules[r.id] ? "on" : ""}">
        <input type="checkbox" ${d.rules[r.id] ? "checked" : ""} ${r.core ? "" : ""} data-act="rule:${r.id}" />
        <span class="rule-label">${esc(r.label)}</span>
        <span class="rule-note">${esc(r.note)}</span>
        ${r.pocket ? `<span class="tag warn">needs pocket UI</span>` : ""}
      </label>`
    ).join("");
    return `<div class="rule-list">${rows}</div>
      <div class="rules-out">
        <span class="tag">Not in v0.1</span>
        ${RULES_OUT.map((r) => `<span class="out-item">${esc(r)}</span>`).join("")}
        <span class="hint small">Engine can express these; the workshop does not.</span>
      </div>`;
  }

  function positionHTML(compact) {
    const problems = positionProblems(state.def.rows);
    const tray = ["K", "Q", "R", "B", "N", "P", "S"]
      .map(
        (c) => `<button class="tray-piece ${state.tray === c ? "on" : ""}" data-act="tray:${c}">${pieceCell(c)}</button>`
      )
      .join("");
    return `<div class="setup ${compact ? "compact" : ""}">
      <div class="setup-tray">
        <span class="tray-label">Place</span>${tray}
        <button class="tray-piece ${state.tray === "." ? "on" : ""}" data-act="tray:.">⌫</button>
        <button class="tiny" data-act="reset-position">Reset</button>
      </div>
      <div class="fen-row">
        <span class="tray-label">FEN</span>
        <code class="fen">${esc(fen(state.def.rows))}</code>
      </div>
      <div class="validity ${problems.length ? "bad" : "ok"}">
        ${problems.length
          ? `Not yet a Rule-valid starting position — ${esc(problems.join(" "))}`
          : "Rule-valid starting position for this definition"}
      </div>
    </div>`;
  }

  function recordStageHTML(mode) {
    const analysis = mode === "analysis";
    return `<div class="stage-col">
      ${boardHTML({
        rows: MID_ROWS_10x8,
        last: ["5-8", "2-8"],
        badge: `<span class="pill ok">${analysis ? "Analysis Record" : "Played Game"} · Snapshot v${state.def.version}</span>`,
      })}
      ${pocketHTML()}
      <div class="under-board">
        ${analysis
          ? `<span class="hint">Derived from “Wayfarer · evening test” — source keeps its own snapshot</span>`
          : `<div class="clocks"><span class="clock">04:12</span><span class="clock live">03:48</span></div>`}
        <div class="movelist">${MOVES.map((m) => `<span class="mv"><b>${m.n}.</b> ${m.w} ${m.b}</span>`).join("")}</div>
      </div>
      ${analysis
        ? `<div class="pinned">
            <div class="pinned-head">Pinned Engine Lines</div>
            <div class="pin-row"><code>+0.31 d14</code> 4. e3 e6 5. Sd2 <span class="tag warn">generic eval</span></div>
            <div class="pin-row"><code>-0.08 d13</code> 4. g3 Bg7 5. Bg2 <span class="tag warn">generic eval</span></div>
          </div>`
        : `<div class="under-board"><button data-act="noop">Offer draw</button><button data-act="noop">Resign</button><span class="hint">Result detection uses the snapshot's rules</span></div>`}
    </div>`;
  }

  function snapshotNoteHTML() {
    return `<div class="snapshot-note">
      <strong>Variant Snapshot v${state.def.version}</strong> is frozen into every record started under it.
      ${state.def.usedBy} existing records keep their snapshot when you edit the definition.
      <button class="link" data-act="edit-used">Edit anyway…</button>
    </div>`;
  }

  // ── Variant A: cockpit inspector ─────────────────────────────
  function renderA() {
    const sc = state.scenario;
    const inPlay = sc === "play" || sc === "analysis";
    const libVariants = LIBRARY_VARIANTS.map(
      (v) => `<div class="lib-row ${v.id === "wayfarer" ? "on" : ""}" data-act="noop">
        <span class="lib-name">${esc(v.name)}</span>
        <span class="lib-sub">${esc(v.sub)}</span>
        ${v.status === "playable" ? `<span class="dot ok"></span>` : v.status === "draft" ? `<span class="dot draft"></span>` : ""}
      </div>`
    ).join("");
    const libGames = LIBRARY_GAMES.map(
      (g) => `<div class="lib-row" data-act="noop"><span class="lib-name">${esc(g.title)}</span><span class="lib-sub">${esc(g.sub)}</span></div>`
    ).join("");

    const sections = [
      { id: "board", title: "Board", summary: `${preset().name} · ${preset().files}×${preset().ranks}`, body: presetGridHTML() },
      { id: "pieces", title: "Pieces", summary: `${state.def.builtins.length} built-in + 1 custom (${customLetter()})`, body: pieceListHTML() },
      { id: "position", title: "Starting position", summary: positionProblems(state.def.rows).length ? "incomplete" : "Rule-valid", body: positionHTML(true) },
      { id: "rules", title: "Rules", summary: RULES.filter((r) => state.def.rules[r.id]).length + " families on", body: rulesHTML() },
      { id: "validation", title: "Validation", summary: isPlayable() ? "Playable" : `${hardBlockers().length} blocker(s)`, body: pipelineHTML() },
    ]
      .map(
        (s) => `<section class="acc ${state.open[s.id] ? "open" : ""}">
          <button class="acc-head" data-act="section:${s.id}">
            <span class="acc-title">${esc(s.title)}</span>
            <span class="acc-summary">${esc(s.summary)}</span>
            <span class="acc-caret">${state.open[s.id] ? "▾" : "▸"}</span>
          </button>
          ${state.open[s.id] ? `<div class="acc-body">${s.body}</div>` : ""}
        </section>`
      )
      .join("");

    const stage = inPlay
      ? recordStageHTML(sc)
      : `<div class="stage-col">
          ${boardHTML({ editable: sc === "position", badge: sc === "position" ? `<span class="pill draft">Position Setup</span>` : "" })}
          ${pocketHTML()}
          ${sc === "entry" ? `<div class="under-board"><button class="primary" data-act="scenario:board">New Variant Definition</button><span class="hint">Same New… menu as a Played Game or Study</span></div>` : ""}
          ${sc === "gate" ? snapshotNoteHTML() : ""}
        </div>`;

    return `<div class="wsA">
      <header class="topbar">
        <span class="brand">Omachess</span>
        <div class="tabs">
          <button class="tab">Sicilian week</button>
          <button class="tab on">${esc(state.def.name)}${inPlay ? ` · ${sc === "analysis" ? "analysis" : "played game"}` : ""} ${statusPill()}</button>
          <button class="tab ghost">+</button>
        </div>
        <div class="topbar-right">
          <span class="chip">Manual · saved</span>${engineChip()}
          <button class="tiny" data-act="palette">⌘K</button>
        </div>
      </header>
      <div class="panes">
        <aside class="rail left">
          <div class="rail-head">Personal Library</div>
          <div class="rail-group">Variants</div>
          ${libVariants}
          <div class="rail-group">Games & analysis</div>
          ${libGames}
        </aside>
        <main class="center">${stage}</main>
        <aside class="rail right">
          ${inPlay
            ? evalRailHTML()
            : `<div class="rail-head">Definition inspector <span class="hint">${esc(state.def.name)}</span></div>${sections}`}
        </aside>
      </div>
      <footer class="statusbar ${isPlayable() ? "ok" : ""}">
        <span>${isPlayable() ? "Playable" : "Draft"} v${state.def.version}</span>
        <span class="sep">·</span>
        <span>${hardBlockers().length ? `${hardBlockers().length} blocker(s)` : "no blockers"}</span>
        <span class="grow"></span>
        ${isPlayable()
          ? `<button class="primary" data-act="scenario:play">Start Played Game</button>`
          : `<button class="primary" data-act="run-validation">Make Playable</button>`}
      </footer>
    </div>`;
  }

  // ── Variant B: guided build stepper ──────────────────────────
  const STEPS = [
    { n: 1, id: "board", title: "Board", hint: "Pick a rectangular preset" },
    { n: 2, id: "pieces", title: "Pieces", hint: "Catalogue + one custom piece" },
    { n: 3, id: "position", title: "Position", hint: "Rule-valid start" },
    { n: 4, id: "rules", title: "Rules", hint: "Curated families" },
    { n: 5, id: "validate", title: "Validate", hint: "Draft → Playable" },
    { n: 6, id: "handoff", title: "Play", hint: "Leave the workshop" },
  ];

  function renderB() {
    const sc = state.scenario;
    const map = { entry: 0, board: 1, pieces: 2, position: 3, rules: 4, validate: 5, play: 6, analysis: 6, gate: 5 };
    const step = map[sc];

    if (step === 0) {
      return `<div class="wsB">
        <header class="topbar">
          <span class="brand">Omachess</span>
          <div class="tabs"><button class="tab on">Personal Library</button></div>
          <div class="topbar-right">${engineChip()}</div>
        </header>
        <div class="lib-page">
          <h1>Variants</h1>
          <p class="hint">Workshop entry is a library section, but building takes over the window.</p>
          <div class="card-grid">
            ${LIBRARY_VARIANTS.map(
              (v) => `<div class="var-card ${v.status}">
                <div class="var-name">${esc(v.name)}</div>
                <div class="var-sub">${esc(v.sub)}</div>
                <div class="var-actions">
                  ${v.status === "playable" || v.status === "builtin"
                    ? `<button class="primary tiny" data-act="scenario:play">Play</button><button class="tiny" data-act="scenario:board">Edit</button>`
                    : `<button class="tiny" data-act="scenario:board">Continue building</button>`}
                </div>
              </div>`
            ).join("")}
            <button class="var-card new" data-act="scenario:board">＋ New variant<span class="hint">opens the builder</span></button>
          </div>
        </div>
      </div>`;
    }

    if (step === 6) {
      const analysis = sc === "analysis";
      return `<div class="wsB">
        <header class="topbar">
          <span class="brand">Omachess</span>
          <div class="tabs"><button class="tab on">${esc(state.def.name)} · ${analysis ? "analysis" : "played game"}</button></div>
          <div class="topbar-right"><span class="chip">Autosave · saved</span>${engineChip()}</div>
        </header>
        <div class="handoff-banner">
          Left the builder — this is the ordinary workspace, running ${esc(state.def.name)} under
          <strong>Variant Snapshot v${state.def.version}</strong>.
          <button class="tiny" data-act="scenario:validate">Back to builder</button>
        </div>
        <div class="play-cols">
          ${recordStageHTML(sc)}
          <aside class="rail right">${evalRailHTML()}</aside>
        </div>
      </div>`;
    }

    const cur = STEPS[step - 1];
    const bodies = {
      board: `${presetGridHTML()}<div class="step-preview">${boardHTML({ size: "sm" })}</div>`,
      pieces: `<div class="two-col">${pieceListHTML()}<div class="step-preview">${boardHTML({ size: "sm" })}</div></div>`,
      position: `<div class="two-col">${boardHTML({ editable: true, size: "md" })}${positionHTML()}</div>`,
      rules: rulesHTML(),
      validate: `${sc === "gate" ? snapshotNoteHTML() : ""}<div class="two-col">${pipelineHTML()}<div class="issues">${blockerListHTML()}</div></div>`,
    }[cur.id];

    const rail = STEPS.map((s) => {
      const done = s.n < step;
      const on = s.n === step;
      return `<button class="step ${cls(done && "done", on && "on")}" data-act="step:${s.id}">
        <span class="step-n">${done ? "✓" : s.n}</span>
        <span class="step-title">${esc(s.title)}</span>
        <span class="step-hint">${esc(s.hint)}</span>
      </button>`;
    }).join("");

    const nextId = STEPS[Math.min(step, STEPS.length - 1)].id;
    const prevId = STEPS[Math.max(step - 2, 0)].id;
    return `<div class="wsB">
      <header class="topbar builder">
        <span class="brand">Variant Workshop</span>
        <span class="builder-name">${esc(state.def.name)} ${statusPill()}</span>
        <div class="topbar-right">${engineChip()}<button class="tiny" data-act="scenario:entry">Exit to library</button></div>
      </header>
      <div class="builder-body">
        <nav class="step-rail">${rail}</nav>
        <main class="step-main">
          <h2>${step}. ${esc(cur.title)}</h2>
          <p class="hint">${esc(cur.hint)}</p>
          ${bodies}
        </main>
      </div>
      <footer class="step-foot">
        <button data-act="step:${prevId}">Back</button>
        <span class="foot-note">${hardBlockers().length ? `${hardBlockers().length} blocker(s) before Playable` : "no blockers"}</span>
        <span class="grow"></span>
        ${step === 5
          ? `<button class="primary" data-act="scenario:play" ${isPlayable() ? "" : "disabled"}>Play ${esc(state.def.name)}</button>`
          : `<button class="primary" data-act="step:${nextId}">Continue</button>`}
      </footer>
    </div>`;
  }

  // ── Variant C: definition document + console ─────────────────
  function renderC() {
    const sc = state.scenario;
    const inPlay = sc === "play" || sc === "analysis";
    const d = state.def;

    const outlineSection = (id, title, summary, body) => `
      <section class="doc-sec ${state.outline[id] ? "open" : ""}">
        <button class="doc-head" data-act="outline:${id}">
          <span class="doc-caret">${state.outline[id] ? "▾" : "▸"}</span>
          <span class="doc-title">${esc(title)}</span>
          <span class="doc-summary">${esc(summary)}</span>
        </button>
        ${state.outline[id] ? `<div class="doc-body">${body}</div>` : ""}
      </section>`;

    const doc = `
      <div class="doc-meta">
        <div><span class="k">name</span><span class="v">${esc(d.name)}</span></div>
        <div><span class="k">schema</span><span class="v">omachess.variant/1</span></div>
        <div><span class="k">version</span><span class="v">${d.version} · ${d.usedBy} records bound</span></div>
      </div>
      ${outlineSection("board", "board", `${preset().files}×${preset().ranks} ${preset().name}`, presetGridHTML())}
      ${outlineSection("pieces", "pieces", `6 built-in + ${customLetter()}:${esc(d.custom.betza)}`, pieceListHTML())}
      ${outlineSection("position", "startPosition", positionProblems(d.rows).length ? "incomplete" : "rule-valid", positionHTML(true))}
      ${outlineSection("rules", "rules", RULES.filter((r) => d.rules[r.id]).map((r) => r.id).join(" · "), rulesHTML())}`;

    const problemRows = blockers().length
      ? blockers()
          .map(
            (b) => `<div class="con-row ${b.kind}">
              <span class="con-kind">${b.kind === "blocker" ? "error" : "warn"}</span>
              <span class="con-where">${esc(b.where.toLowerCase())}</span>
              <span class="con-text">${esc(b.text)} <em>${esc(b.fix)}</em></span>
            </div>`
          )
          .join("")
      : `<div class="con-row ok"><span class="con-kind">ok</span><span class="con-text">0 problems</span></div>`;

    const runLog = state.run
      ? STAGES.slice(0, state.run.index + 1)
          .map((s, i) => {
            const failed = state.run.failedAt === s.id;
            return `<div class="log-line ${failed ? "fail" : "pass"}">[${String(i + 1).padStart(2, "0")}] ${esc(s.label)} … ${failed ? "FAILED" : state.run.index > i ? "ok" : "running"}</div>`;
          })
          .join("") + (state.run.failedAt ? `<pre class="log-raw">${esc(state.run.raw)}</pre>` : "")
      : `<div class="log-line dim">No run yet. Validate to compile, check, and smoke-test in throwaway processes.</div>`;

    const tabs = [
      ["problems", `Problems (${blockers().length})`],
      ["log", "Engine log"],
      ["ini", "Compiled INI"],
      ["engine", inPlay ? "Analysis" : "Analysis"],
    ]
      .map(
        ([id, label]) => `<button class="con-tab ${state.consoleTab === id ? "on" : ""}" data-act="tab:${id}">${esc(label)}</button>`
      )
      .join("");

    const consoleBody = {
      problems: problemRows,
      log: runLog,
      ini: `<pre class="ini">${esc(compiledIni())}</pre>
        <div class="hint small">Adapter artifact, read-only. Never the save format, never importable.</div>`,
      engine: evalRailHTML(true),
    }[state.consoleTab];

    const docTabs = LIBRARY_VARIANTS.map(
      (v) => `<button class="doc-tab ${v.id === "wayfarer" ? "on" : ""} ${v.status}" data-act="noop">
        ${esc(v.name)}${v.status === "draft" ? " *" : ""}</button>`
    ).join("");

    return `<div class="wsC">
      <header class="runbar">
        <span class="brand">Omachess</span>
        <span class="sep">/</span>
        <span class="doc-name">${esc(d.name)}</span>
        ${statusPill()}
        <span class="grow"></span>
        ${engineChip()}
        <button data-act="run-validation">▶ Validate</button>
        <button class="primary" data-act="scenario:play" ${isPlayable() ? "" : "disabled"}>▶ Play</button>
      </header>
      <div class="doc-tabs">${docTabs}<button class="doc-tab new" data-act="noop">＋</button>
        <span class="grow"></span><span class="hint small">Standard chess is read-only</span></div>
      <div class="c-cols">
        <section class="doc">${sc === "gate" ? snapshotNoteHTML() : ""}${doc}</section>
        <section class="preview">
          <div class="preview-board">
            ${inPlay
              ? boardHTML({ rows: MID_ROWS_10x8, last: ["5-8", "2-8"], badge: `<span class="pill ok">Snapshot v3</span>` })
              : boardHTML({ editable: sc === "position" })}
            ${pocketHTML()}
          </div>
          <div class="console">
            <div class="con-tabs">${tabs}<span class="grow"></span>
              <span class="hint small">${esc(engine().short)}</span>
            </div>
            <div class="con-body">${consoleBody}</div>
          </div>
        </section>
      </div>
      <footer class="statusbar ${isPlayable() ? "ok" : ""}">
        <span>${isPlayable() ? "Playable" : "Draft"} v${d.version}</span>
        <span class="sep">·</span><span>${blockers().length} problem(s)</span>
        <span class="grow"></span>
        <span class="hint">Workshop reads as build tooling; play is “run”.</span>
      </footer>
    </div>`;
  }

  // ── Chrome ───────────────────────────────────────────────────
  function scenarioStripHTML() {
    return `<div class="scenario-strip">
      <span class="tag">Scenario</span>
      ${SCENARIOS.map(
        (s, i) => `<button class="${state.scenario === s.id ? "active" : ""}" data-act="scenario:${s.id}">
          ${i + 1}. ${esc(s.label)}</button>`
      ).join("")}
      <span class="grow"></span>
      <span class="tag">Engine build</span>
      ${Object.values(ENGINE_BUILDS)
        .map((e) => `<button class="${state.engine === e.id ? "active" : ""}" data-act="engine:${e.id}">${esc(e.id)}</button>`)
        .join("")}
      <button data-act="help">?</button>
    </div>`;
  }

  function modalHTML() {
    if (state.help) {
      return `<div class="scrim" data-act="close-modal"><div class="modal">
        <h3>Prototype keys</h3>
        <ul>
          <li><kbd>1</kbd>–<kbd>9</kbd> scenarios</li>
          <li><kbd>←</kbd> <kbd>→</kbd> switch variant</li>
          <li><kbd>V</kbd> run validation · <kbd>E</kbd> cycle engine build</li>
          <li><kbd>?</kbd> this sheet · <kbd>Esc</kbd> close</li>
        </ul>
        <p class="hint">Everything is mock data. Judge structure, not polish.</p>
        <button class="primary" data-act="close-modal">Close</button>
      </div></div>`;
    }
    if (!state.modal) return "";
    const M = {
      "edit-used": {
        title: "Edit a definition that records already use?",
        body: `<p><strong>${esc(state.def.name)}</strong> is bound into ${state.def.usedBy} records as Variant Snapshot v${state.def.version}.</p>
          <p>Editing returns the library definition to <strong>Draft</strong> and bumps it to v${state.def.version + 1} once it validates again. Existing records keep playing and analysing under their frozen snapshot — nothing rewrites their history.</p>`,
        actions: `<button class="primary" data-act="confirm-edit">Edit as Draft v${state.def.version + 1}</button><button data-act="close-modal">Cancel</button>`,
      },
      "why-generic": {
        title: "Generic evaluation",
        body: `<p>No neural network exists for ${esc(state.def.name)}, so ${esc(engine().label)} is searching with its handcrafted evaluator.</p>
          <p>Your piece values (Sentinel ${state.def.mg || state.def.custom.mg}/${state.def.custom.eg}) are inputs to that evaluator. An evaluation here says what this engine finds in this search — not that the variant is balanced, and not a rating.</p>`,
        actions: `<button class="primary" data-act="close-modal">Got it</button>`,
      },
      "computer-analysis": {
        title: "Run Computer Analysis",
        body: `<p>A finite pass over the game produces an Analysis Record with per-move evaluations and better-line sidelines, under Variant Snapshot v${state.def.version}.</p>
          <p>It runs as a Background Job, so it survives closing the workspace and appears in the Omarchy shell's Background Controls.</p>`,
        actions: `<button class="primary" data-act="close-modal">Start job</button><button data-act="close-modal">Cancel</button>`,
      },
      palette: {
        title: "Command palette",
        body: `<div class="palette-list">
          ${["New Variant Definition…", "Open Variant Workshop on Wayfarer Chess", "Validate definition", "Start Played Game under a variant", "Position Setup", "Compare with standard chess"]
            .map((c) => `<div class="palette-row">${esc(c)}</div>`)
            .join("")}
        </div>`,
        actions: `<button class="primary" data-act="close-modal">Close</button>`,
      },
      pin: {
        title: "Pinned Engine Line",
        body: `<p>Saved into the Analysis Record with the engine, depth, and the note that evaluation was generic for this variant.</p>`,
        actions: `<button class="primary" data-act="close-modal">Close</button>`,
      },
    }[state.modal];
    if (!M) return "";
    return `<div class="scrim" data-act="close-modal"><div class="modal" data-stop="1">
      <h3>${M.title}</h3>${M.body}<div class="modal-actions">${M.actions}</div>
    </div></div>`;
  }

  function prototypeBarHTML() {
    const v = VARIANTS[state.variant];
    return `<span class="proto-label">Prototype</span>
      <button data-act="prev-variant">←</button>
      <span class="variant-name">${v.key} — ${esc(v.name)}</span>
      <button data-act="next-variant">→</button>
      <span class="proto-label">#11</span>`;
  }

  // ── Render ───────────────────────────────────────────────────
  function render() {
    const body = { A: renderA, B: renderB, C: renderC }[state.variant]();
    document.getElementById("app").innerHTML = scenarioStripHTML() + body;
    document.getElementById("modal-root").innerHTML = modalHTML();
    document.getElementById("prototype-bar").innerHTML = prototypeBarHTML();
    fitBoards();
  }

  // A board with a definite container fills it: square size comes from the
  // space the layout actually gives the stage, not a fixed pixel constant.
  const MIN_SQ = 18;
  const MAX_SQ = 140;

  function fitBoards() {
    document.querySelectorAll(".board-wrap.fit").forEach((wrap) => {
      const board = wrap.querySelector(".board");
      if (!board) return;
      const files = +board.style.getPropertyValue("--files");
      const ranks = +board.style.getPropertyValue("--ranks");
      wrap.style.setProperty("--sq", "0px");
      const foot = wrap.querySelector(".board-foot");
      const availW = wrap.clientWidth;
      const availH = wrap.clientHeight - (foot ? foot.offsetHeight : 0);
      const sq = Math.floor(Math.min(availW / files, availH / ranks));
      wrap.style.setProperty("--sq", `${Math.max(MIN_SQ, Math.min(MAX_SQ, sq))}px`);
    });
  }

  let fitPending = false;
  window.addEventListener("resize", () => {
    if (fitPending) return;
    fitPending = true;
    requestAnimationFrame(() => { fitPending = false; fitBoards(); });
  });

  // ── Actions ──────────────────────────────────────────────────
  function runValidation() {
    state.attempts += 1;
    state.run = { index: 0, done: false, failedAt: null, message: "", fix: "", raw: "" };
    render();
    const fail = (stage, message, fix, raw) => {
      Object.assign(state.run, { failedAt: stage, done: true, message, fix, raw });
      state.def.status = "draft";
      render();
    };
    const tick = () => {
      const s = STAGES[state.run.index];
      const ruleBlockers = hardBlockers().filter((b) => b.where === "Rules");
      if (s.id === "schema" && ruleBlockers.length) {
        fail("schema", ruleBlockers[0].text, ruleBlockers[0].fix,
          "omachess schema: rejected before the engine ever saw the definition");
        return;
      }
      if (s.id === "check" && !state.letterFixed) {
        state.letterFailed = true;
        fail(
          "check",
          `Piece letter "S" is already used by the built-in Silver General, so the engine cannot tell your Sentinel apart from it.`,
          "Rename the Sentinel's letter, then validate again. Nothing else in the definition changes.",
          `# fairy-stockfish check /tmp/omachess-XXXX/wayfarer.ini\nwayfarer:chess - Ambiguous piece char: S\nwayfarer:chess - Invalid startFen: rnbsqksbnr/...`
        );
        return;
      }
      if (s.id === "gate" && (!engine().present || !presetAllowed(preset()))) {
        fail(
          "gate",
          engine().present
            ? `${preset().name} needs a build that reaches ${preset().files}×${preset().ranks}; the detected build stops at ${engine().maxFile}×${engine().maxRank}.`
            : "No Fairy-Stockfish build is installed, so no workshop variant can be made Playable.",
          "Install a largeboards Fairy-Stockfish from the Engine Catalog, or move the definition to Standard 8×8.",
          `probe: largeboards=${engine().maxFile >= 10} allvars=${engine().maxFile >= 10}\nrequired: ${preset().files}x${preset().ranks}`
        );
        return;
      }
      state.run.index += 1;
      if (state.run.index >= STAGES.length) {
        state.run.done = true;
        state.def.status = "playable";
        render();
        return;
      }
      render();
      setTimeout(tick, 420);
    };
    setTimeout(tick, 420);
  }

  const ACTIONS = {
    "prev-variant": () => cycleVariant(-1),
    "next-variant": () => cycleVariant(1),
    "run-validation": runValidation,
    "fix-letter": () => {
      state.letterFixed = true;
      state.def.custom.letter = "Y";
      state.run = null;
      runValidation();
    },
    "goto-pieces": () => applyScenario("pieces"),
    "goto-board": () => applyScenario("board"),
    "goto-rules": () => applyScenario("rules"),
    "reset-position": () => { state.def.rows = START_ROWS_10x8.slice(); },
    "edit-used": () => { state.modal = "edit-used"; },
    "confirm-edit": () => {
      state.modal = null;
      state.def.status = "draft";
      state.def.version += 1;
      state.run = null;
    },
    "why-generic": () => { state.modal = "why-generic"; },
    "computer-analysis": () => { state.modal = "computer-analysis"; },
    palette: () => { state.modal = "palette"; },
    pin: () => { state.modal = "pin"; },
    help: () => { state.help = true; },
    "close-modal": () => { state.modal = null; state.help = false; },
    noop: () => {},
  };

  function cycleVariant(dir) {
    const i = VARIANT_KEYS.indexOf(state.variant);
    state.variant = VARIANT_KEYS[(i + dir + VARIANT_KEYS.length) % VARIANT_KEYS.length];
    syncUrl();
  }

  function applyScenario(id) {
    state.scenario = id;
    const openMap = {
      entry: "validation", board: "board", pieces: "pieces", position: "position",
      rules: "rules", validate: "validation", play: "validation", analysis: "validation", gate: "validation",
    };
    Object.keys(state.open).forEach((k) => (state.open[k] = false));
    state.open[openMap[id]] = true;
    const focused = ["board", "pieces", "position", "rules"].includes(id);
    Object.keys(state.outline).forEach((k) => (state.outline[k] = focused ? k === id : true));
    if (id === "play" || id === "analysis") {
      state.letterFixed = true;
      state.def.custom.letter = "Y";
      state.def.status = "playable";
      state.run = { index: STAGES.length, done: true, failedAt: null, message: "", fix: "", raw: "" };
    }
    if (id === "gate") {
      // the story is a definition that validated once, now sitting on a weaker build
      state.letterFixed = true;
      state.def.custom.letter = "Y";
      if (state.engine === "full") state.engine = "small";
    }
    if (id === "analysis") state.consoleTab = "engine";
    else if (id === "validate") state.consoleTab = "log";
    else state.consoleTab = "problems";
    syncUrl();
  }

  function handle(act) {
    if (ACTIONS[act]) { ACTIONS[act](); render(); return; }
    const [kind, arg] = act.split(":");
    switch (kind) {
      case "scenario": applyScenario(arg); break;
      case "engine":
        state.engine = arg;
        if (state.def.status === "playable" && (!engine().present || !presetAllowed(preset()))) state.def.status = "draft";
        break;
      case "preset":
        state.def.presetId = arg;
        state.def.rows = resizeRows(state.def.rows, PRESETS.find((p) => p.id === arg));
        state.def.status = "draft";
        state.run = null;
        break;
      case "section": state.open[arg] = !state.open[arg]; break;
      case "outline": state.outline[arg] = !state.outline[arg]; break;
      case "tab": state.consoleTab = arg; break;
      case "step": {
        const toScenario = { board: "board", pieces: "pieces", position: "position", rules: "rules", validate: "validate", handoff: "play" };
        applyScenario(toScenario[arg]);
        break;
      }
      case "rule":
        state.def.rules[arg] = !state.def.rules[arg];
        state.def.status = "draft";
        state.run = null;
        break;
      case "piece": {
        const i = state.def.builtins.indexOf(arg);
        if (i >= 0) state.def.builtins.splice(i, 1);
        else state.def.builtins.push(arg);
        state.def.status = "draft";
        break;
      }
      case "tray": state.tray = arg; break;
      default: break;
    }
    render();
  }

  function resizeRows(rows, p) {
    const out = [];
    for (let r = 0; r < p.ranks; r += 1) {
      const src = rows[r] || "";
      out.push((src + ".".repeat(p.files)).slice(0, p.files));
    }
    return out;
  }

  function placePiece(r, f) {
    const row = state.def.rows[r].split("");
    const black = r < state.def.rows.length / 2;
    row[f] = state.tray === "." ? "." : black ? state.tray.toLowerCase() : state.tray;
    state.def.rows[r] = row.join("");
    state.def.status = "draft";
    state.run = null;
    render();
  }

  function syncUrl() {
    const u = new URL(location.href);
    u.searchParams.set("variant", state.variant);
    u.searchParams.set("scenario", state.scenario);
    history.replaceState(null, "", u);
  }

  // ── Events ───────────────────────────────────────────────────
  document.addEventListener("click", (ev) => {
    const target = ev.target.closest("[data-act]");
    if (!target) return;
    const act = target.dataset.act;
    if (act === "place") { placePiece(+target.dataset.r, +target.dataset.f); return; }
    if (act === "close-modal" && target.classList.contains("scrim") && ev.target !== target) return;
    ev.preventDefault();
    handle(act);
  });

  document.addEventListener("input", (ev) => {
    const t = ev.target.closest("[data-act='betza']");
    if (!t) return;
    state.def.betzaDraft = t.value;
    const pos = t.selectionStart;
    render();
    const again = document.querySelector("[data-act='betza']");
    if (again) { again.focus(); again.setSelectionRange(pos, pos); }
  });

  document.addEventListener("keydown", (ev) => {
    const tag = (ev.target.tagName || "").toLowerCase();
    if (tag === "input" || tag === "textarea") return;
    if (ev.key === "Escape") { state.modal = null; state.help = false; render(); return; }
    if (ev.key === "ArrowLeft") { cycleVariant(-1); render(); return; }
    if (ev.key === "ArrowRight") { cycleVariant(1); render(); return; }
    if (ev.key === "?") { state.help = true; render(); return; }
    if (ev.key.toLowerCase() === "v") { runValidation(); return; }
    if (ev.key.toLowerCase() === "e") {
      const ids = Object.keys(ENGINE_BUILDS);
      state.engine = ids[(ids.indexOf(state.engine) + 1) % ids.length];
      render();
      return;
    }
    const n = parseInt(ev.key, 10);
    if (n >= 1 && n <= SCENARIOS.length) { applyScenario(SCENARIO_IDS[n - 1]); render(); }
  });

  // ── Boot ─────────────────────────────────────────────────────
  const params = new URLSearchParams(location.search);
  if (VARIANTS[params.get("variant")]) state.variant = params.get("variant");
  if (SCENARIO_IDS.includes(params.get("scenario"))) applyScenario(params.get("scenario"));
  render();
})();
