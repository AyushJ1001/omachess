/* PROTOTYPE — core chess workspace variants for wayfinder ticket #10.
 * Three layouts share mock state + scenario-driven flows.
 * Question is in README.md. Throwaway code.
 */
(() => {
  "use strict";

  // ── Domain mock data ─────────────────────────────────────────
  const PIECES = {
    K: "♔", Q: "♕", R: "♖", B: "♗", N: "♘", P: "♙",
    k: "♚", q: "♛", r: "♜", b: "♝", n: "♞", p: "♟",
  };

  const START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  const MID_FEN = "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4";

  const RECORDS = [
    {
      id: "g1",
      kind: "played",
      title: "vs Stockfish · evening blitz",
      sub: "In progress · move 14 · you are White",
      result: "*",
      status: "in_progress",
      studyIds: [],
      dirty: false,
    },
    {
      id: "g2",
      kind: "played",
      title: "Ayush vs Guest · Sicilian",
      sub: "Completed 1-0 · yesterday",
      result: "1-0",
      status: "completed",
      studyIds: ["s1"],
      dirty: false,
    },
    {
      id: "g3",
      kind: "played",
      title: "Suspended · Berlin endgame",
      sub: "Suspended · clocks frozen · move 31",
      result: "*",
      status: "suspended",
      studyIds: [],
      dirty: false,
    },
    {
      id: "a1",
      kind: "analysis",
      title: "Computer Analysis · Sicilian",
      sub: "Default Analysis of Ayush vs Guest · Stockfish depth 22",
      relation: "analysis of g2",
      sourceId: "g2",
      studyIds: ["s1"],
      independent: false,
      dirty: false,
    },
    {
      id: "a2",
      kind: "analysis",
      title: "Najdorf ideas — independent",
      sub: "Independent Analysis Record · 3 Pinned Engine Lines",
      relation: null,
      sourceId: null,
      studyIds: ["s1", "s2"],
      independent: true,
      dirty: true,
    },
    {
      id: "a3",
      kind: "analysis",
      title: "Alternate line after 12…d5",
      sub: "analysis of g2 · manual exploration",
      relation: "analysis of g2",
      sourceId: "g2",
      studyIds: ["s1"],
      independent: false,
      dirty: false,
    },
  ];

  const STUDIES = [
    {
      id: "s1",
      title: "Sicilian week",
      items: ["g2", "a1", "a3", "a2"],
    },
    {
      id: "s2",
      title: "Najdorf notebook",
      items: ["a2"],
    },
  ];

  const MOVES = [
    { n: 1, w: "e4", b: "e5" },
    { n: 2, w: "Nf3", b: "Nc6" },
    { n: 3, w: "Bc4", b: "Nf6" },
    { n: 4, w: "d3", b: "Bc5" },
    { n: 5, w: "c3", b: "d6" },
    { n: 6, w: "O-O", b: "a6" },
    { n: 7, w: "a4", b: "Ba7" },
  ];

  const PVS = [
    { score: "+0.42", depth: 18, line: "8. Be3 Bxe3 9. fxe3 O-O 10. Nbd2 Be6 11. Bxe6 fxe6" },
    { score: "+0.18", depth: 18, line: "8. h3 h6 9. Re1 O-O 10. Nbd2 Be6 11. Bb3" },
    { score: "0.00", depth: 17, line: "8. b4 Bb6 9. a5 Ba7 10. Be3 Bxe3 11. fxe3" },
  ];

  const PGN_IMPORT = [
    { entry: 1, title: "Morphy vs Duke", status: "ok", kind: "Completed Game + linked Analysis Record", reason: null },
    { entry: 2, title: "Unfinished study line", status: "ok", kind: "Analysis Record (result *)", reason: null },
    { entry: 3, title: "(no tags)", status: "fail", kind: null, reason: "Malformed move 12: 'Nge2' ambiguous after …Bg4" },
    { entry: 4, title: "Rapid vs Engine", status: "ok", kind: "Completed Game", reason: null },
    { entry: 5, title: "Corrupted export", status: "fail", kind: null, reason: "Unexpected token at byte 1842; FEN side-to-move missing" },
  ];

  const PALETTE_CMDS = [
    { id: "lib", label: "Open Personal Library", keys: "L" },
    { id: "engine", label: "Toggle Live Position Analysis", keys: "E" },
    { id: "setup", label: "Position Setup", keys: "S" },
    { id: "import", label: "Import PGN…", keys: "I" },
    { id: "save", label: "Save record", keys: "Ctrl+S" },
    { id: "close", label: "Close record", keys: "Ctrl+W" },
    { id: "quit", label: "Quit Omachess", keys: "Ctrl+Q" },
    { id: "resume", label: "Resume game (explicit)", keys: "R" },
    { id: "study", label: "Go to Studies", keys: "T" },
    { id: "new-game", label: "New Played Game", keys: "N" },
    { id: "help", label: "Keyboard cheatsheet", keys: "?" },
  ];

  // ── App state ────────────────────────────────────────────────
  const VARIANTS = {
    A: { key: "A", name: "Three-pane cockpit" },
    B: { key: "B", name: "Board-first palette" },
    C: { key: "C", name: "Study desk" },
  };

  const SCENARIOS = [
    { id: "play", label: "Play workspace" },
    { id: "restore", label: "Return restore" },
    { id: "dirty", label: "Manual save dirty" },
    { id: "import", label: "Partial PGN import" },
    { id: "bg-consent", label: "Analysis BG consent" },
    { id: "setup", label: "Position Setup" },
    { id: "study", label: "Study graph" },
  ];

  const state = {
    variant: "A",
    scenario: "play",
    libraryTab: "games",
    selectedRecordId: "g1",
    selectedStudyId: "s1",
    openTabs: ["g1", "a2"],
    moveIndex: MOVES.length * 2 - 1,
    selectedSq: null,
    legalHint: null,
    fen: MID_FEN,
    positionMode: "rule-valid", // rule-valid | freeform
    setupPiece: "N",
    lpaOn: true,
    engineName: "Stockfish 18",
    multipv: 3,
    saveMode: "autosave", // autosave | manual
    dirty: false,
    clocks: { white: "04:12", black: "03:58", active: "white", frozen: false },
    restoreDismissed: false,
    overlay: null, // library | engine | setup | palette | import | study | help | null
    paletteQuery: "",
    paletteIndex: 0,
    analysisJob: {
      running: true,
      progress: 62,
      title: "Computer Analysis · Sicilian",
      engine: "Stockfish 18",
    },
    toast: null,
    boardOrientation: "white",
  };

  // ── Utilities ────────────────────────────────────────────────
  function $(sel, root = document) {
    return root.querySelector(sel);
  }

  function el(tag, attrs = {}, kids = []) {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === "class") node.className = v;
      else if (k === "text") node.textContent = v;
      else if (k === "html") node.innerHTML = v;
      else if (k.startsWith("on") && typeof v === "function") node.addEventListener(k.slice(2), v);
      else if (v === true) node.setAttribute(k, "");
      else if (v !== false && v != null) node.setAttribute(k, v);
    }
    for (const kid of [].concat(kids)) {
      if (kid == null || kid === false) continue;
      node.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
    }
    return node;
  }

  function recordById(id) {
    return RECORDS.find((r) => r.id === id);
  }

  function parseFen(fen) {
    const board = Array.from({ length: 8 }, () => Array(8).fill(null));
    const [rows] = fen.split(" ");
    rows.split("/").forEach((row, r) => {
      let c = 0;
      for (const ch of row) {
        if (/\d/.test(ch)) c += Number(ch);
        else {
          board[r][c] = ch;
          c += 1;
        }
      }
    });
    return board;
  }

  function fenFromBoard(board, side = "w") {
    const rows = board.map((row) => {
      let s = "";
      let empty = 0;
      for (const cell of row) {
        if (!cell) empty += 1;
        else {
          if (empty) {
            s += empty;
            empty = 0;
          }
          s += cell;
        }
      }
      if (empty) s += empty;
      return s;
    });
    return `${rows.join("/")} ${side} - - 0 1`;
  }

  function toast(msg) {
    state.toast = msg;
    render();
    setTimeout(() => {
      if (state.toast === msg) {
        state.toast = null;
        render();
      }
    }, 2200);
  }

  function setVariant(key) {
    if (!VARIANTS[key]) return;
    state.variant = key;
    state.overlay = null;
    const url = new URL(location.href);
    url.searchParams.set("variant", key);
    history.replaceState(null, "", url);
    render();
  }

  function setScenario(id) {
    state.scenario = id;
    state.overlay = null;
    state.restoreDismissed = false;
    // Scenario-driven defaults
    if (id === "play") {
      state.selectedRecordId = "g1";
      state.saveMode = "autosave";
      state.dirty = false;
      state.clocks = { white: "04:12", black: "03:58", active: "white", frozen: false };
      state.lpaOn = true;
      state.fen = MID_FEN;
    } else if (id === "restore") {
      state.selectedRecordId = "g3";
      state.clocks = { white: "02:40", black: "01:55", active: null, frozen: true };
      state.lpaOn = false;
      state.dirty = false;
      state.saveMode = "autosave";
    } else if (id === "dirty") {
      state.selectedRecordId = "a2";
      state.saveMode = "manual";
      state.dirty = true;
      state.lpaOn = true;
      state.clocks = { white: "—", black: "—", active: null, frozen: true };
    } else if (id === "import") {
      state.overlay = state.variant === "B" ? "import" : null;
    } else if (id === "bg-consent") {
      state.selectedRecordId = "g2";
      state.analysisJob.running = true;
      state.analysisJob.progress = 62;
    } else if (id === "setup") {
      state.fen = START_FEN;
      state.selectedSq = null;
      state.setupPiece = "N";
      state.positionMode = "rule-valid";
      if (state.variant === "B") state.overlay = "setup";
    } else if (id === "study") {
      state.selectedStudyId = "s1";
      state.selectedRecordId = "a1";
      state.libraryTab = "studies";
      if (state.variant === "B") state.overlay = "study";
    }
    render();
  }

  function currentRecord() {
    return recordById(state.selectedRecordId);
  }

  function markDirty() {
    if (state.saveMode === "manual") state.dirty = true;
    else state.dirty = false; // autosave advances Saved Snapshot immediately
  }

  function saveRecord() {
    state.dirty = false;
    const r = currentRecord();
    if (r) r.dirty = false;
    toast(state.saveMode === "manual" ? "Saved Snapshot advanced" : "Already autosaved");
    render();
  }

  function requestCloseRecord() {
    if (state.saveMode === "manual" && state.dirty) {
      showModal(unsavedCloseModal("record"));
      return;
    }
    toast("Record closed · Saved Snapshot retained");
  }

  function requestQuit() {
    if (state.scenario === "bg-consent" && state.analysisJob.running) {
      showModal(bgConsentModal());
      return;
    }
    if (state.saveMode === "manual" && state.dirty) {
      showModal(unsavedCloseModal("app"));
      return;
    }
    toast("Quit (prototype — no process exit)");
  }

  // ── Board ────────────────────────────────────────────────────
  function boardGrid(opts = {}) {
    const { interactive = true, setup = false } = opts;
    const board = parseFen(state.fen);
    const ranks = state.boardOrientation === "white" ? [0, 1, 2, 3, 4, 5, 6, 7] : [7, 6, 5, 4, 3, 2, 1, 0];
    const files = state.boardOrientation === "white" ? [0, 1, 2, 3, 4, 5, 6, 7] : [7, 6, 5, 4, 3, 2, 1, 0];

    const root = el("div", { class: "board", role: "grid", "aria-label": "Chess board" });
    for (const r of ranks) {
      for (const c of files) {
        const light = (r + c) % 2 === 1;
        const sq = `${"abcdefgh"[c]}${8 - r}`;
        const piece = board[r][c];
        const classes = ["sq", light ? "light" : "dark"];
        if (state.selectedSq === sq) classes.push("hl");
        if (state.legalHint === sq) classes.push("dest");
        // mock last move e4/e5
        if (sq === "e4" || sq === "e5") classes.push("last");

        const cell = el("div", {
          class: classes.join(" "),
          "data-sq": sq,
          role: "gridcell",
          tabindex: interactive ? "0" : "-1",
          onclick: interactive
            ? () => onSquareClick(sq, r, c, board, setup)
            : undefined,
        });
        if (piece) cell.append(el("span", { text: PIECES[piece] || piece, "aria-label": piece }));
        if (c === files[0]) cell.append(el("span", { class: "coord rank", text: String(8 - r) }));
        if (r === ranks[ranks.length - 1]) cell.append(el("span", { class: "coord file", text: "abcdefgh"[c] }));
        root.append(cell);
      }
    }
    return root;
  }

  function onSquareClick(sq, r, c, board, setup) {
    if (setup || state.scenario === "setup") {
      if (state.setupPiece === "x") {
        board[r][c] = null;
      } else if (state.selectedSq === sq && board[r][c]) {
        board[r][c] = null;
      } else {
        board[r][c] = state.setupPiece;
      }
      state.fen = fenFromBoard(board);
      state.selectedSq = sq;
      // naive freeform detection: two kings missing or extra pieces of same type — demo only
      const flat = board.flat().filter(Boolean);
      const kings = flat.filter((p) => p === "K" || p === "k").length;
      state.positionMode = kings === 2 ? "rule-valid" : "freeform";
      markDirty();
      render();
      return;
    }

    if (!state.selectedSq) {
      if (!board[r][c]) return;
      state.selectedSq = sq;
      // mock one legal destination
      state.legalHint = sq[0] === "e" ? "e5" : "e4";
    } else if (state.selectedSq === sq) {
      state.selectedSq = null;
      state.legalHint = null;
    } else {
      // mock move
      state.selectedSq = null;
      state.legalHint = null;
      markDirty();
      toast("Move played (mock) · LPA retargets to new Rule-valid Position");
    }
    render();
  }

  function evalBar() {
    const whitePct = 58;
    return el("div", { class: "eval-bar", title: "Live Position Analysis evaluation bar" }, [
      el("div", { class: "fill", style: `height:${whitePct}%` }),
      el("div", { class: "score", text: "+0.42" }),
    ]);
  }

  function clocksRow() {
    const c = state.clocks;
    return el("div", { class: "clocks" }, [
      el("div", { class: `clock ${c.active === "black" ? "active" : ""} ${c.frozen ? "frozen" : ""}` }, [
        el("span", { class: "who", text: "Black" }),
        c.black,
      ]),
      el("div", { class: `clock ${c.active === "white" ? "active" : ""} ${c.frozen ? "frozen" : ""}` }, [
        el("span", { class: "who", text: "White" }),
        c.white,
      ]),
    ]);
  }

  function moveList() {
    const items = [];
    let idx = 0;
    for (const m of MOVES) {
      items.push(el("span", { class: "mn", text: `${m.n}.` }));
      const wi = idx;
      items.push(
        el("span", {
          class: `mv ${state.moveIndex === wi ? "sel" : ""}`,
          text: m.w,
          onclick: () => {
            state.moveIndex = wi;
            toast("Navigated main line · LPA follows selected position");
            render();
          },
        })
      );
      idx += 1;
      if (m.b) {
        const bi = idx;
        items.push(
          el("span", {
            class: `mv ${state.moveIndex === bi ? "sel" : ""}`,
            text: m.b,
            onclick: () => {
              state.moveIndex = bi;
              render();
            },
          })
        );
        idx += 1;
      }
    }
    return el("div", { class: "move-list", "aria-label": "Move list" }, items);
  }

  // ── Engine / LPA ─────────────────────────────────────────────
  function enginePanel(compact = false) {
    if (!state.lpaOn) {
      return el("div", { class: "engine-block" }, [
        el("div", { class: "empty-hint", text: "Live Position Analysis is off. Engines do not auto-start on restore." }),
        el("button", { class: "primary", text: "Start Live Position Analysis", onclick: () => { state.lpaOn = true; render(); } }),
      ]);
    }
    return el("div", { class: "engine-block" }, [
      el("div", { class: "engine-head" }, [
        el("span", { class: "chip info", text: state.engineName }),
        el("span", { class: "chip", text: `MultiPV ${state.multipv}` }),
        el("span", { class: "chip ok", text: "depth 18" }),
      ]),
      !compact && el("div", { class: "section-label", text: "Principal variations" }),
      el("div", { class: "pv-list" }, PVS.slice(0, state.multipv).map((pv, i) =>
        el("div", { class: "pv" }, [
          el("span", { class: "pv-depth", text: `d${pv.depth}` }),
          el("span", { class: "pv-score", text: pv.score }),
          el("span", { text: `PV${i + 1}` }),
          el("div", { class: "pv-line", text: pv.line }),
          el("div", { style: "margin-top:6px" }, [
            el("button", {
              class: "ghost",
              text: "Pin line",
              onclick: () => toast("Pinned Engine Line saved into Analysis Record (explicit)"),
            }),
          ]),
        ])
      )),
      el("div", { class: "engine-controls" }, [
        el("button", { text: "Stop", onclick: () => { state.lpaOn = false; render(); } }),
        el("button", {
          text: "MultiPV −",
          onclick: () => { state.multipv = Math.max(1, state.multipv - 1); render(); },
        }),
        el("button", {
          text: "MultiPV +",
          onclick: () => { state.multipv = Math.min(5, state.multipv + 1); render(); },
        }),
      ]),
    ]);
  }

  // ── Library lists ────────────────────────────────────────────
  function libraryList() {
    if (state.libraryTab === "studies") return studyList();
    const kinds = state.libraryTab === "games" ? ["played"] : state.libraryTab === "analyses" ? ["analysis"] : ["played", "analysis"];
    const items = RECORDS.filter((r) => kinds.includes(r.kind));
    return el("div", {}, items.map((r) => libraryItem(r)));
  }

  function libraryItem(r) {
    const selected = r.id === state.selectedRecordId;
    return el("div", {
      class: `list-item ${selected ? "selected" : ""}`,
      onclick: () => selectRecord(r.id),
      role: "button",
      tabindex: "0",
    }, [
      el("div", { class: "li-title", text: r.title }),
      el("div", { class: "li-sub", text: r.sub }),
      el("div", { class: "li-meta" }, [
        el("span", { class: `chip ${r.kind === "analysis" ? "info" : "ok"}`, text: r.kind === "analysis" ? "Analysis" : "Played" }),
        r.independent && el("span", { class: "chip", text: "independent" }),
        r.relation && el("span", { class: "chip", text: r.relation }),
        (r.dirty || (r.id === state.selectedRecordId && state.dirty)) && el("span", { class: "chip dirty", text: "unsaved" }),
      ]),
    ]);
  }

  function studyList() {
    return el("div", {}, STUDIES.map((s) => {
      const open = s.id === state.selectedStudyId;
      return el("div", { style: "margin-bottom:8px" }, [
        el("div", {
          class: `list-item ${open ? "selected" : ""}`,
          onclick: () => { state.selectedStudyId = s.id; render(); },
        }, [
          el("div", { class: "li-title", text: s.title }),
          el("div", { class: "li-sub", text: `${s.items.length} records · ordered collection` }),
        ]),
        open && el("div", { class: "tree-children" }, s.items.map((id) => {
          const r = recordById(id);
          return el("div", {
            class: `tree-node ${state.selectedRecordId === id ? "selected" : ""}`,
            onclick: (e) => { e.stopPropagation(); selectRecord(id); },
          }, [
            el("span", { class: `tree-badge ${r.kind === "analysis" ? "analysis" : "game"}`, text: r.kind === "analysis" ? "A" : "G" }),
            el("span", { text: r.title }),
            r.independent
              ? el("span", { class: "tree-badge indep", text: "indep" })
              : r.sourceId
                ? el("span", { class: "tree-badge derived", text: "derived" })
                : null,
          ]);
        })),
      ]);
    }));
  }

  function selectRecord(id) {
    state.selectedRecordId = id;
    if (!state.openTabs.includes(id)) state.openTabs = [...state.openTabs, id];
    const r = recordById(id);
    if (r?.status === "suspended" || r?.status === "completed") {
      state.clocks.frozen = true;
      state.clocks.active = null;
    }
    if (r?.kind === "analysis") {
      state.clocks = { white: "—", black: "—", active: null, frozen: true };
    }
    render();
  }

  // ── Shared chrome ────────────────────────────────────────────
  function saveChip() {
    if (state.saveMode === "autosave") {
      return el("span", { class: "chip ok", title: "Autosave Mode", text: "Autosave · saved" });
    }
    if (state.dirty) {
      return el("span", { class: "chip dirty", title: "Manual Save Mode — unsaved changes", text: "Manual · unsaved" });
    }
    return el("span", { class: "chip ok", text: "Manual · saved" });
  }

  function topbar(extraActions = []) {
    const rec = currentRecord();
    return el("div", { class: "topbar" }, [
      el("div", { class: "brand", html: "oma<span>chess</span>" }),
      el("div", { class: "record-title", text: rec ? rec.title : "No record" }),
      el("div", { class: "meta-chips" }, [
        saveChip(),
        rec && el("span", { class: "chip", text: rec.kind === "analysis" ? "Analysis Record" : "Played Game" }),
        rec?.status === "suspended" && el("span", { class: "chip warn", text: "Suspended" }),
        state.clocks.frozen && el("span", { class: "chip warn", text: "Clocks frozen" }),
        !state.lpaOn && el("span", { class: "chip", text: "Engine idle" }),
        state.scenario === "bg-consent" && state.analysisJob.running &&
          el("span", { class: "chip info", text: `Analysis ${state.analysisJob.progress}%` }),
      ]),
      el("div", { class: "spacer" }),
      el("div", { class: "actions" }, [
        ...extraActions,
        el("button", {
          text: state.saveMode === "autosave" ? "Save mode: Auto" : "Save mode: Manual",
          onclick: () => {
            state.saveMode = state.saveMode === "autosave" ? "manual" : "autosave";
            if (state.saveMode === "autosave") state.dirty = false;
            render();
          },
        }),
        el("button", { text: "Save", onclick: saveRecord }),
        el("button", { text: "Close", onclick: requestCloseRecord }),
        el("button", { class: "danger", text: "Quit", onclick: requestQuit }),
      ]),
    ]);
  }

  function statusline(hints) {
    return el("div", { class: "statusline" }, [
      el("span", { html: `<strong>Scenario:</strong> ${SCENARIOS.find((s) => s.id === state.scenario)?.label}` }),
      el("span", { html: `<strong>Variant ${state.variant}:</strong> ${VARIANTS[state.variant].name}` }),
      el("span", { text: hints }),
    ]);
  }

  function scenarioStrip() {
    return el("div", { class: "scenario-strip" }, [
      el("span", { class: "tag", text: "Scenario" }),
      ...SCENARIOS.map((s) =>
        el("button", {
          class: state.scenario === s.id ? "active" : "",
          text: s.label,
          onclick: () => setScenario(s.id),
        })
      ),
    ]);
  }

  // ── Setup UI ─────────────────────────────────────────────────
  function setupChrome() {
    const tray = ["K", "Q", "R", "B", "N", "P", "k", "q", "r", "b", "n", "p", "x"];
    return el("div", { class: "setup-layout" }, [
      el("div", {}, [
        el("div", { class: "section-label", text: "Position Setup" }),
        el("div", {
          class: `validity ${state.positionMode === "rule-valid" ? "ok" : "freeform"}`,
          text: state.positionMode === "rule-valid"
            ? "Rule-valid Position — play, clocks, engines available"
            : "Freeform Position — manual exploration only; no Played Game / clocks / guaranteed engine",
        }),
        el("div", { class: "fen-row" }, [
          el("input", {
            value: state.fen,
            "aria-label": "FEN",
            onchange: (e) => {
              state.fen = e.target.value.trim() || START_FEN;
              markDirty();
              render();
            },
          }),
          el("button", {
            text: "Apply FEN",
            onclick: () => toast("FEN applied · validity rechecked"),
          }),
          el("button", {
            text: "Start pos",
            onclick: () => { state.fen = START_FEN; state.positionMode = "rule-valid"; render(); },
          }),
        ]),
      ]),
      el("div", {}, [
        el("div", { class: "section-label", text: "Piece tray" }),
        el("div", { class: "piece-tray" }, tray.map((p) =>
          el("button", {
            class: state.setupPiece === p ? "active primary" : "",
            text: p === "x" ? "⌫" : PIECES[p],
            title: p === "x" ? "Remove piece" : p,
            onclick: () => { state.setupPiece = p; render(); },
          })
        )),
      ]),
    ]);
  }

  // ── Import UI ────────────────────────────────────────────────
  function importPanel() {
    const ok = PGN_IMPORT.filter((x) => x.status === "ok").length;
    const fail = PGN_IMPORT.filter((x) => x.status === "fail").length;
    return el("div", {}, [
      el("div", { class: "section-label", text: "PGN import results" }),
      el("div", { class: "meta-chips", style: "margin-bottom:8px" }, [
        el("span", { class: "chip ok", text: `${ok} imported` }),
        el("span", { class: "chip err", text: `${fail} failed` }),
        el("span", { class: "chip", text: "Study created in file order" }),
      ]),
      el("p", { class: "empty-hint", text: "Valid entries survive malformed neighbors. Failures keep actionable parse reasons; nothing is silently merged." }),
      el("table", { class: "import-table" }, [
        el("thead", {}, [
          el("tr", {}, [
            el("th", { text: "#" }),
            el("th", { text: "Entry" }),
            el("th", { text: "Result" }),
            el("th", { text: "Detail" }),
          ]),
        ]),
        el("tbody", {}, PGN_IMPORT.map((row) =>
          el("tr", {}, [
            el("td", { text: String(row.entry) }),
            el("td", { text: row.title }),
            el("td", {
              class: row.status === "ok" ? "ok" : "fail",
              text: row.status === "ok" ? "Imported" : "Failed",
            }),
            el("td", {}, [
              row.kind && el("div", { text: row.kind }),
              row.reason && el("div", { class: "reason", text: row.reason }),
              row.status === "ok" && el("button", {
                class: "ghost",
                style: "margin-top:4px",
                text: "Open",
                onclick: () => toast(`Opened imported record (entry ${row.entry})`),
              }),
              row.status === "fail" && el("button", {
                class: "ghost",
                style: "margin-top:4px",
                text: "Copy reason",
                onclick: () => toast("Parse reason copied (mock)"),
              }),
            ]),
          ])
        )),
      ]),
      el("div", { class: "engine-controls", style: "margin-top:10px" }, [
        el("button", { class: "primary", text: "Done", onclick: () => { state.overlay = null; toast("Import session closed · library updated"); render(); } }),
        el("button", { text: "Retry failed…", onclick: () => toast("Retry would re-parse only failed entries") }),
      ]),
    ]);
  }

  // ── Restore card ─────────────────────────────────────────────
  function restoreBanner() {
    if (state.scenario !== "restore" || state.restoreDismissed) return null;
    return el("div", { class: "restore-card", style: "margin:12px" }, [
      el("h3", { text: "Welcome back" }),
      el("p", { text: "Your previous workspace was restored. Records and board positions are back — clocks stay frozen and engines stay idle until you act." }),
      el("div", { class: "restore-list" }, [
        el("div", { class: "list-item selected" }, [
          el("div", { class: "li-title", text: "Suspended · Berlin endgame" }),
          el("div", { class: "li-sub", text: "Position at move 31 · clocks 2:40 / 1:55 frozen" }),
          el("div", { class: "li-meta" }, [
            el("span", { class: "chip warn", text: "Not resumed" }),
            el("span", { class: "chip", text: "Engine idle" }),
          ]),
        ]),
        el("div", { class: "list-item" }, [
          el("div", { class: "li-title", text: "Najdorf ideas — independent" }),
          el("div", { class: "li-sub", text: "Analysis Record · last ply selected" }),
        ]),
      ]),
      el("div", { class: "engine-controls" }, [
        el("button", {
          class: "primary",
          text: "Resume game",
          onclick: () => {
            state.clocks.frozen = false;
            state.clocks.active = "white";
            state.restoreDismissed = true;
            toast("Resume is explicit · side-to-move clock starts when board & engine ready");
            render();
          },
        }),
        el("button", {
          text: "Browse only",
          onclick: () => { state.restoreDismissed = true; render(); },
        }),
        el("button", {
          text: "Start Live Position Analysis",
          onclick: () => { state.lpaOn = true; state.restoreDismissed = true; render(); },
        }),
      ]),
    ]);
  }

  // ── Variant A ────────────────────────────────────────────────
  function renderVariantA() {
    const showImport = state.scenario === "import";
    const showSetup = state.scenario === "setup";

    const left = el("div", { class: "panel" }, [
      el("div", { class: "panel-head" }, [el("h2", { text: "Personal Library" })]),
      el("div", { class: "panel-tabs" }, [
        ["games", "Games"],
        ["analyses", "Analyses"],
        ["studies", "Studies"],
      ].map(([id, label]) =>
        el("button", {
          class: state.libraryTab === id ? "active" : "",
          text: label,
          onclick: () => { state.libraryTab = id; render(); },
        })
      )),
      el("div", { class: "panel-body" }, [libraryList()]),
    ]);

    const center = el("div", { class: "panel", style: "border-right:none;background:var(--bg-darker)" }, [
      restoreBanner(),
      showImport && el("div", { class: "panel-body" }, [importPanel()]),
      !showImport && el("div", { class: "board-stage" }, [
        clocksRow(),
        el("div", { class: "board-wrap" }, [evalBar(), boardGrid({ setup: showSetup })]),
        showSetup ? setupChrome() : moveList(),
      ]),
    ]);

    const right = el("div", { class: "panel" }, [
      el("div", { class: "panel-head" }, [
        el("h2", { text: "Live Position Analysis" }),
        el("button", {
          class: "ghost",
          text: state.lpaOn ? "On" : "Off",
          onclick: () => { state.lpaOn = !state.lpaOn; render(); },
        }),
      ]),
      el("div", { class: "panel-body" }, [
        enginePanel(),
        state.scenario === "bg-consent" && el("div", {}, [
          el("div", { class: "section-label", text: "Computer Analysis job" }),
          el("div", { class: "pv" }, [
            el("div", { text: state.analysisJob.title }),
            el("div", { class: "li-sub", text: `${state.analysisJob.engine} · ${state.analysisJob.progress}% · Pause / Resume / Cancel` }),
            el("div", { class: "engine-controls", style: "margin-top:8px" }, [
              el("button", { text: "Pause", onclick: () => toast("Job paused") }),
              el("button", { text: "Cancel", onclick: () => { state.analysisJob.running = false; toast("Job cancelled"); render(); } }),
            ]),
          ]),
        ]),
      ]),
    ]);

    return el("div", { class: "workspace var-a" }, [
      scenarioStrip(),
      topbar([
        el("button", { text: "Import PGN", onclick: () => setScenario("import") }),
        el("button", { text: "Setup", onclick: () => setScenario("setup") }),
      ]),
      el("div", { class: "main" }, [left, center, right]),
      statusline("Library tabs · board center · LPA rail  ·  ? help"),
    ]);
  }

  // ── Variant B ────────────────────────────────────────────────
  function renderVariantB() {
    const stage = el("div", { class: "board-stage" }, [
      el("div", { class: "thin-strip" }, [
        el("span", { class: "brand", html: "oma<span>chess</span>" }),
        el("span", { class: "record-title", text: currentRecord()?.title || "" }),
        saveChip(),
        state.clocks.frozen
          ? el("span", { class: "chip warn", text: `Clocks ${state.clocks.white} / ${state.clocks.black} frozen` })
          : el("span", { class: "chip", text: `${state.clocks.white} · ${state.clocks.black}` }),
        el("button", { text: "⌘K", title: "Command palette", onclick: () => { state.overlay = "palette"; render(); } }),
      ]),
      restoreBanner(),
      clocksRow(),
      el("div", { class: "board-wrap" }, [
        state.lpaOn && evalBar(),
        boardGrid({ setup: state.overlay === "setup" || state.scenario === "setup" }),
      ]),
      (state.overlay === "setup" || state.scenario === "setup") && setupChrome(),
      state.scenario !== "setup" && moveList(),
      state.lpaOn && el("div", { class: `float-lpa ${state.overlay && state.overlay !== "engine" ? "hidden" : ""}` }, [
        el("div", { class: "section-label", text: "Live Position Analysis" }),
        enginePanel(true),
      ]),
      el("div", { class: "corner-hints", html:
        "<div><kbd>Ctrl</kbd>+<kbd>K</kbd> command palette</div>" +
        "<div><kbd>L</kbd> library · <kbd>E</kbd> engine · <kbd>S</kbd> setup · <kbd>T</kbd> studies</div>" +
        "<div><kbd>R</kbd> resume · <kbd>Ctrl</kbd>+<kbd>S</kbd> save · <kbd>Esc</kbd> dismiss</div>"
      }),
    ]);

    const root = el("div", { class: "workspace var-b" }, [
      scenarioStrip(),
      el("div", { class: "main" }, [stage, renderOverlayB()]),
      statusline("Board-first · overlays via palette  ·  ←/→ variants"),
    ]);
    return root;
  }

  function renderOverlayB() {
    if (!state.overlay) return null;
    if (state.overlay === "palette") return paletteOverlay();
    if (state.overlay === "library" || state.overlay === "study") {
      return el("div", { class: "overlay", onclick: (e) => { if (e.target === e.currentTarget) { state.overlay = null; render(); } } }, [
        el("div", { class: "overlay-panel" }, [
          el("div", { class: "panel-head" }, [
            el("h2", { text: state.overlay === "study" ? "Studies" : "Personal Library" }),
            el("button", { class: "ghost", text: "Esc", onclick: () => { state.overlay = null; render(); } }),
          ]),
          state.overlay === "study"
            ? el("div", { class: "panel-body" }, [studyList()])
            : el("div", {}, [
                el("div", { class: "panel-tabs" }, [
                  ["games", "Games"],
                  ["analyses", "Analyses"],
                  ["studies", "Studies"],
                ].map(([id, label]) =>
                  el("button", {
                    class: state.libraryTab === id ? "active" : "",
                    text: label,
                    onclick: () => { state.libraryTab = id; if (id === "studies") state.overlay = "study"; render(); },
                  })
                )),
                el("div", { class: "panel-body" }, [libraryList()]),
              ]),
        ]),
      ]);
    }
    if (state.overlay === "import") {
      return el("div", { class: "overlay center", onclick: (e) => { if (e.target === e.currentTarget) { state.overlay = null; render(); } } }, [
        el("div", { class: "overlay-panel", style: "width:min(640px,94vw);max-height:85vh;overflow:auto;padding:12px" }, [
          importPanel(),
        ]),
      ]);
    }
    if (state.overlay === "help") {
      return el("div", { class: "overlay center", onclick: (e) => { if (e.target === e.currentTarget) { state.overlay = null; render(); } } }, [
        el("div", { class: "overlay-panel", style: "padding:16px" }, [
          el("h3", { text: "Keyboard — Variant B", style: "margin-top:0" }),
          helpGridB(),
          el("button", { style: "margin-top:12px", text: "Close", onclick: () => { state.overlay = null; render(); } }),
        ]),
      ]);
    }
    return null;
  }

  function paletteOverlay() {
    const q = state.paletteQuery.toLowerCase();
    const items = PALETTE_CMDS.filter((c) => c.label.toLowerCase().includes(q));
    const idx = Math.min(state.paletteIndex, Math.max(0, items.length - 1));
    return el("div", { class: "overlay center" }, [
      el("div", { class: "palette" }, [
        el("input", {
          placeholder: "Type a command…",
          value: state.paletteQuery,
          autofocus: true,
          oninput: (e) => { state.paletteQuery = e.target.value; state.paletteIndex = 0; render(); },
        }),
        el("div", { class: "palette-results" }, items.map((c, i) =>
          el("div", {
            class: `palette-item ${i === idx ? "sel" : ""}`,
            onclick: () => runPalette(c.id),
          }, [
            el("span", { class: "pi-cmd", text: c.label }),
            el("span", { class: "pi-keys", text: c.keys }),
          ])
        )),
      ]),
    ]);
  }

  function runPalette(id) {
    state.overlay = null;
    state.paletteQuery = "";
    if (id === "lib") state.overlay = "library";
    else if (id === "engine") { state.lpaOn = !state.lpaOn; }
    else if (id === "setup") { state.scenario = "setup"; state.overlay = "setup"; }
    else if (id === "import") { state.scenario = "import"; state.overlay = "import"; }
    else if (id === "save") saveRecord();
    else if (id === "close") requestCloseRecord();
    else if (id === "quit") requestQuit();
    else if (id === "resume") {
      state.clocks.frozen = false;
      state.clocks.active = "white";
      toast("Explicit Resume game");
    } else if (id === "study") { state.overlay = "study"; state.libraryTab = "studies"; }
    else if (id === "new-game") toast("New Played Game (mock)");
    else if (id === "help") state.overlay = "help";
    render();
  }

  function helpGridB() {
    return el("div", { class: "help-grid" }, PALETTE_CMDS.flatMap((c) => [
      el("div", { class: "k", text: c.keys }),
      el("div", { class: "d", text: c.label }),
    ]));
  }

  // ── Variant C ────────────────────────────────────────────────
  function renderVariantC() {
    const study = STUDIES.find((s) => s.id === state.selectedStudyId) || STUDIES[0];

    const rail = el("div", { class: "study-rail" }, [
      el("div", { class: "panel-head" }, [el("h2", { text: "Studies" })]),
      el("div", { class: "panel-body" }, [
        ...STUDIES.map((s) =>
          el("div", {
            class: `list-item ${s.id === state.selectedStudyId ? "selected" : ""}`,
            onclick: () => { state.selectedStudyId = s.id; render(); },
          }, [
            el("div", { class: "li-title", text: s.title }),
            el("div", { class: "li-sub", text: `${s.items.length} records` }),
          ])
        ),
        el("div", { class: "section-label", text: "Study contents" }),
        el("div", { class: "tree" }, study.items.map((id) => {
          const r = recordById(id);
          return el("div", {
            class: `tree-node ${state.selectedRecordId === id ? "selected" : ""}`,
            onclick: () => selectRecord(id),
          }, [
            el("span", { class: `tree-badge ${r.kind === "analysis" ? "analysis" : "game"}`, text: r.kind === "analysis" ? "An" : "Gm" }),
            el("span", { style: "flex:1", text: r.title }),
            r.independent
              ? el("span", { class: "tree-badge indep", text: "indep" })
              : r.sourceId
                ? el("span", { class: "tree-badge derived", text: "← src" })
                : null,
          ]);
        })),
        el("div", { class: "section-label", text: "Record Graph" }),
        el("div", { class: "empty-hint", text: graphHint() }),
        el("div", { class: "section-label", text: "Library (all)" }),
        el("button", {
          class: "ghost",
          text: "Show independent analyses outside studies…",
          onclick: () => toast("Independent Analysis Records remain first-class library items"),
        }),
      ]),
    ]);

    const tabs = el("div", { class: "tab-bar" }, state.openTabs.map((id) => {
      const r = recordById(id);
      const dirty = (id === state.selectedRecordId && state.dirty) || r?.dirty;
      return el("button", {
        class: `tab ${id === state.selectedRecordId ? "active" : ""}`,
        onclick: () => selectRecord(id),
      }, [
        r?.title?.slice(0, 28) || id,
        dirty ? el("span", { class: "dirty-dot", title: "Unsaved" }) : null,
      ]);
    }));

    const showImport = state.scenario === "import";
    const showSetup = state.scenario === "setup";

    const center = el("div", { class: "desk-center" }, [
      restoreBanner(),
      showImport
        ? el("div", { class: "panel-body" }, [importPanel()])
        : el("div", { class: "board-stage", style: "min-height:0" }, [
            clocksRow(),
            el("div", { class: "board-wrap" }, [evalBar(), boardGrid({ setup: showSetup })]),
            showSetup && setupChrome(),
          ]),
      el("div", { class: "bottom-split" }, [
        el("div", { class: "pane" }, [
          el("div", { class: "section-label", text: "Move tree" }),
          moveList(),
        ]),
        el("div", { class: "pane" }, [
          el("div", { class: "section-label", text: "Annotations / links" }),
          el("div", { class: "empty-hint", html: annotationHint() }),
        ]),
      ]),
    ]);

    const engine = el("div", { class: "desk-engine" }, [
      el("div", { class: "panel-head" }, [el("h2", { text: "Live Position Analysis" })]),
      el("div", { class: "panel-body" }, [enginePanel()]),
    ]);

    return el("div", { class: "workspace var-c" }, [
      scenarioStrip(),
      topbar([
        el("button", { text: "Import", onclick: () => setScenario("import") }),
        el("button", { text: "Setup", onclick: () => setScenario("setup") }),
      ]),
      el("div", { class: "main" }, [rail, tabs, center, engine]),
      statusline("Study rail · record tabs · tree + LPA  ·  identities never merge"),
    ]);
  }

  function graphHint() {
    const r = currentRecord();
    if (!r) return "Select a record.";
    if (r.kind === "played" && r.status === "completed") {
      return `Completed Game «${r.title}» has derived analyses a1, a3 (analysis of) and Default Analysis a1.`;
    }
    if (r.kind === "analysis" && r.sourceId) {
      return `«${r.title}» is derived (${r.relation}). Source Snapshot retained if source purged. Also in Studies: ${(r.studyIds || []).join(", ") || "none"}.`;
    }
    if (r.independent) {
      return `«${r.title}» is an independent Analysis Record — first-class library item, also filed in Studies without losing identity.`;
    }
    return "Unfinished Played Games cannot belong to a Study.";
  }

  function annotationHint() {
    const r = currentRecord();
    if (r?.kind === "analysis") {
      return "Glyphs · comments · Pinned Engine Lines · sidelines.<br>Computer Analysis material stays on this Analysis Record, not on the Completed Game history.";
    }
    return "Played Game main line is authoritative.<br>Open or create an Analysis Record for durable annotations.";
  }

  // ── Modals ───────────────────────────────────────────────────
  function showModal(node) {
    const root = $("#modal-root");
    root.innerHTML = "";
    root.append(node);
  }

  function clearModal() {
    $("#modal-root").innerHTML = "";
  }

  function unsavedCloseModal(scope) {
    const title = scope === "app" ? "Quit with unsaved changes?" : "Close record with unsaved changes?";
    const rec = currentRecord();
    return el("div", { class: "modal-backdrop", onclick: (e) => { if (e.target === e.currentTarget) clearModal(); } }, [
      el("div", { class: "modal", role: "dialog", "aria-modal": "true" }, [
        el("h3", { text: title }),
        el("p", { text: "Manual Save Mode: the Saved Snapshot advances only when you save. Closing will discard unsaved changes." }),
        el("div", { class: "detail" }, [
          el("div", { html: `<strong>Record:</strong> ${rec?.title || "—"}` }),
          el("div", { html: "<strong>Unsaved:</strong> move tree edits, annotations, Position Setup changes" }),
          el("div", { html: "<strong>After discard:</strong> reopen returns to the last Saved Snapshot" }),
        ]),
        el("div", { class: "modal-actions" }, [
          el("button", { text: "Cancel", onclick: clearModal }),
          el("button", {
            class: "danger",
            text: "Discard changes",
            onclick: () => {
              state.dirty = false;
              clearModal();
              toast(scope === "app" ? "Discarded · quit (mock)" : "Discarded · record closed · Saved Snapshot restored");
            },
          }),
          el("button", {
            class: "primary",
            text: "Save & continue",
            onclick: () => {
              state.dirty = false;
              clearModal();
              toast(scope === "app" ? "Saved · quit (mock)" : "Saved · record closed");
            },
          }),
        ]),
      ]),
    ]);
  }

  function bgConsentModal() {
    let remember = false;
    const backdrop = el("div", { class: "modal-backdrop" });
    const modal = el("div", { class: "modal", role: "dialog", "aria-modal": "true" });
    modal.append(
      el("h3", { text: "Computer Analysis still running" }),
      el("p", { text: "A Computer Analysis job can continue through an Omachess background worker after the workspace closes. Choose whether it should keep going." }),
      el("div", { class: "detail" }, [
        el("div", { html: `<strong>Job:</strong> ${state.analysisJob.title}` }),
        el("div", { html: `<strong>Engine:</strong> ${state.analysisJob.engine}` }),
        el("div", { html: `<strong>Progress:</strong> ${state.analysisJob.progress}%` }),
        el("div", { html: "<strong>Controls:</strong> also available from the Omarchy shell plugin" }),
      ]),
      el("label", { class: "check" }, [
        el("input", {
          type: "checkbox",
          onchange: (e) => { remember = e.target.checked; },
        }),
        el("span", { text: "Remember this choice for Computer Analysis jobs (prototype preference)" }),
      ]),
      el("div", { class: "modal-actions" }, [
        el("button", {
          text: "Cancel close",
          onclick: () => clearModal(),
        }),
        el("button", {
          class: "danger",
          text: "Stop analysis & quit",
          onclick: () => {
            state.analysisJob.running = false;
            clearModal();
            toast("Analysis cancelled · workspace closed" + (remember ? " · preference saved" : ""));
          },
        }),
        el("button", {
          class: "primary",
          text: "Continue in background",
          onclick: () => {
            clearModal();
            toast("Consent granted · worker continues · shell plugin can Pause/Resume/Cancel" + (remember ? " · preference saved" : ""));
          },
        }),
      ])
    );
    backdrop.append(modal);
    backdrop.addEventListener("click", (e) => { if (e.target === backdrop) clearModal(); });
    return backdrop;
  }

  // ── Prototype bar ────────────────────────────────────────────
  function renderPrototypeBar() {
    const keys = Object.keys(VARIANTS);
    const i = keys.indexOf(state.variant);
    const bar = $("#prototype-bar");
    bar.innerHTML = "";
    bar.append(
      el("span", { class: "proto-label", text: "Prototype" }),
      el("button", {
        text: "←",
        "aria-label": "Previous variant",
        onclick: () => setVariant(keys[(i - 1 + keys.length) % keys.length]),
      }),
      el("span", { class: "variant-name", text: `${state.variant} — ${VARIANTS[state.variant].name}` }),
      el("button", {
        text: "→",
        "aria-label": "Next variant",
        onclick: () => setVariant(keys[(i + 1) % keys.length]),
      }),
      el("button", {
        text: "?",
        title: "Help",
        onclick: () => {
          if (state.variant === "B") {
            state.overlay = "help";
            render();
          } else {
            showModal(el("div", { class: "modal-backdrop", onclick: (e) => { if (e.target === e.currentTarget) clearModal(); } }, [
              el("div", { class: "modal" }, [
                el("h3", { text: `Keyboard — Variant ${state.variant}` }),
                el("div", { class: "help-grid" }, [
                  el("div", { class: "k", text: "← / →" }), el("div", { class: "d", text: "Switch prototype variant" }),
                  el("div", { class: "k", text: "1–7" }), el("div", { class: "d", text: "Jump scenario" }),
                  el("div", { class: "k", text: "Ctrl+S" }), el("div", { class: "d", text: "Save" }),
                  el("div", { class: "k", text: "Ctrl+W" }), el("div", { class: "d", text: "Close record (unsaved guard)" }),
                  el("div", { class: "k", text: "Ctrl+Q" }), el("div", { class: "d", text: "Quit (BG consent / unsaved)" }),
                  el("div", { class: "k", text: "R" }), el("div", { class: "d", text: "Explicit Resume game" }),
                  el("div", { class: "k", text: "E" }), el("div", { class: "d", text: "Toggle Live Position Analysis" }),
                ]),
                el("button", { style: "margin-top:12px", text: "Close", onclick: clearModal }),
              ]),
            ]));
          }
        },
      })
    );
  }

  // ── Render root ──────────────────────────────────────────────
  function render() {
    const app = $("#app");
    app.innerHTML = "";
    let view;
    if (state.variant === "A") view = renderVariantA();
    else if (state.variant === "B") view = renderVariantB();
    else view = renderVariantC();
    app.append(view);
    if (state.toast) {
      app.append(el("div", { class: "toast", text: state.toast }));
    }
    renderPrototypeBar();

    // Autofocus palette input
    if (state.overlay === "palette") {
      const input = app.querySelector(".palette input");
      if (input) {
        input.focus();
        input.selectionStart = input.selectionEnd = input.value.length;
      }
    }
  }

  // ── Keyboard ─────────────────────────────────────────────────
  function isTypingTarget(t) {
    if (!t) return false;
    const tag = t.tagName;
    return tag === "INPUT" || tag === "TEXTAREA" || t.isContentEditable;
  }

  window.addEventListener("keydown", (e) => {
    if (isTypingTarget(e.target) && e.key !== "Escape") {
      if (state.overlay === "palette" && (e.key === "ArrowDown" || e.key === "ArrowUp" || e.key === "Enter")) {
        // handled below
      } else {
        return;
      }
    }

    // Variant switcher arrows (prototype chrome)
    if (e.key === "ArrowLeft" && !e.metaKey && !e.ctrlKey && !isTypingTarget(e.target)) {
      const keys = Object.keys(VARIANTS);
      const i = keys.indexOf(state.variant);
      setVariant(keys[(i - 1 + keys.length) % keys.length]);
      e.preventDefault();
      return;
    }
    if (e.key === "ArrowRight" && !e.metaKey && !e.ctrlKey && !isTypingTarget(e.target)) {
      const keys = Object.keys(VARIANTS);
      const i = keys.indexOf(state.variant);
      setVariant(keys[(i + 1) % keys.length]);
      e.preventDefault();
      return;
    }

    if (e.key === "Escape") {
      clearModal();
      state.overlay = null;
      render();
      return;
    }

    if (e.key === "?" && !e.ctrlKey && !e.metaKey) {
      e.preventDefault();
      $("#prototype-bar button[title='Help']")?.click();
      return;
    }

    // Scenario hotkeys 1-7
    if (!e.ctrlKey && !e.metaKey && !e.altKey && e.key >= "1" && e.key <= "7") {
      const s = SCENARIOS[Number(e.key) - 1];
      if (s) setScenario(s.id);
      return;
    }

    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "k" && state.variant === "B") {
      e.preventDefault();
      state.overlay = "palette";
      render();
      return;
    }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
      e.preventDefault();
      saveRecord();
      return;
    }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "w") {
      e.preventDefault();
      requestCloseRecord();
      return;
    }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "q") {
      e.preventDefault();
      requestQuit();
      return;
    }

    if (state.overlay === "palette") {
      const q = state.paletteQuery.toLowerCase();
      const items = PALETTE_CMDS.filter((c) => c.label.toLowerCase().includes(q));
      if (e.key === "ArrowDown") {
        state.paletteIndex = Math.min(items.length - 1, state.paletteIndex + 1);
        render();
        e.preventDefault();
        return;
      }
      if (e.key === "ArrowUp") {
        state.paletteIndex = Math.max(0, state.paletteIndex - 1);
        render();
        e.preventDefault();
        return;
      }
      if (e.key === "Enter") {
        const c = items[state.paletteIndex];
        if (c) runPalette(c.id);
        e.preventDefault();
        return;
      }
    }

    if (isTypingTarget(e.target)) return;

    const k = e.key.toLowerCase();
    if (k === "l") {
      if (state.variant === "B") { state.overlay = "library"; render(); }
      else { state.libraryTab = "games"; render(); }
    } else if (k === "e") {
      state.lpaOn = !state.lpaOn;
      render();
    } else if (k === "s" && !e.ctrlKey) {
      setScenario("setup");
    } else if (k === "t") {
      state.libraryTab = "studies";
      if (state.variant === "B") state.overlay = "study";
      if (state.variant === "C") state.selectedStudyId = "s1";
      render();
    } else if (k === "r") {
      state.clocks.frozen = false;
      state.clocks.active = "white";
      toast("Explicit Resume game · clock starts when ready");
      render();
    } else if (k === "i") {
      setScenario("import");
    }
  });

  // ── Boot ─────────────────────────────────────────────────────
  const params = new URLSearchParams(location.search);
  const v = (params.get("variant") || "A").toUpperCase();
  state.variant = VARIANTS[v] ? v : "A";
  render();
})();
