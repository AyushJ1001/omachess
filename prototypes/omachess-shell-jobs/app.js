/**
 * PROTOTYPE — Omachess shell background controls (throwaway)
 * Answers: https://github.com/AyushJ1001/omachess/issues/18
 *
 * Architecture under test (all variants):
 *   Worker (Omachess) owns jobs + engine work → survives shell restarts
 *   Shell plugin is a control surface → reattaches to worker status
 *   Workspace is a standalone window → not hosted in the shell
 */
(() => {
  const VARIANTS = {
    A: { key: "A", name: "Bar chip + popup", blurb: "Native Omarchy pattern" },
    B: { key: "B", name: "Inline job pills", blurb: "Controls live on the bar" },
    C: { key: "C", name: "Overlay jobs palette", blurb: "Keyboard-first summon" },
  };
  const ORDER = ["A", "B", "C"];

  const now = () => Date.now();
  let idSeq = 100;

  function seedJobs() {
    return [
      {
        id: "job-1",
        kind: "Computer Analysis",
        title: "Kasparov vs Topalov, 1999",
        detail: "Stockfish 17 · depth target 22 · move 18/41",
        progress: 44,
        status: "running", // running | paused | completing | done | failed
        openedFrom: "workspace",
        startedAt: now() - 1000 * 60 * 6,
      },
      {
        id: "job-2",
        kind: "Computer Analysis",
        title: "Study: Berlin endgames",
        detail: "Paused at move 12/28 · Fairy-Stockfish",
        progress: 43,
        status: "paused",
        openedFrom: "consent-close",
        startedAt: now() - 1000 * 60 * 22,
      },
      {
        id: "job-3",
        kind: "Computer Analysis",
        title: "Import batch · game 4",
        detail: "Engine exited non-zero after position 9",
        progress: 31,
        status: "failed",
        openedFrom: "workspace",
        startedAt: now() - 1000 * 60 * 40,
        error: "Engine process ended (code 1)",
      },
    ];
  }

  const state = {
    variant: readVariant(),
    // Worker is Omachess-owned — not the shell
    worker: {
      alive: true,
      pid: 42891,
      restarts: 0,
      lastHeartbeat: now(),
    },
    // Shell plugin lifecycle (independent of worker)
    shell: {
      alive: true,
      restarting: false,
      attachCount: 1,
      lastAttach: now(),
    },
    jobs: seedJobs(),
    popupOpen: false, // A
    detailJobId: null, // B
    overlayOpen: false, // C
    overlaySelected: 0,
    overlayFilter: "",
    helpOpen: false,
    toasts: [],
    workspaceFocus: "library", // visual only
    logs: [],
  };

  function readVariant() {
    const v = new URLSearchParams(location.search).get("variant");
    return ORDER.includes(v) ? v : "A";
  }

  function setVariant(key) {
    state.variant = key;
    state.popupOpen = false;
    state.detailJobId = null;
    state.overlayOpen = false;
    const url = new URL(location.href);
    url.searchParams.set("variant", key);
    history.replaceState(null, "", url);
    render();
  }

  function log(msg) {
    state.logs.unshift({ t: now(), msg });
    state.logs = state.logs.slice(0, 8);
  }

  function toast(title, body, kind = "ok") {
    const id = ++idSeq;
    state.toasts.push({ id, title, body, kind });
    render();
    setTimeout(() => {
      state.toasts = state.toasts.filter((t) => t.id !== id);
      render();
    }, 3200);
  }

  function activeJobs() {
    return state.jobs.filter((j) => j.status !== "done");
  }

  function runningCount() {
    return state.jobs.filter((j) => j.status === "running" || j.status === "completing").length;
  }

  function aggregateProgress() {
    const live = state.jobs.filter((j) =>
      ["running", "paused", "completing"].includes(j.status)
    );
    if (!live.length) return 0;
    return Math.round(live.reduce((s, j) => s + j.progress, 0) / live.length);
  }

  function primaryStatus() {
    if (state.jobs.some((j) => j.status === "failed")) return "failed";
    if (state.jobs.some((j) => j.status === "running" || j.status === "completing")) return "running";
    if (state.jobs.some((j) => j.status === "paused")) return "paused";
    return "idle";
  }

  // —— Worker commands (plugin never owns execution) ——
  function sendCommand(jobId, cmd) {
    if (!state.worker.alive) {
      toast("Worker offline", "Cannot control jobs until Omachess worker is up", "bad");
      return;
    }
    const job = state.jobs.find((j) => j.id === jobId);
    if (!job) return;

    if (cmd === "pause" && job.status === "running") {
      job.status = "paused";
      log(`worker ← pause ${job.id}`);
      toast("Paused", job.title);
    } else if (cmd === "resume" && job.status === "paused") {
      job.status = "running";
      log(`worker ← resume ${job.id}`);
      toast("Resumed", job.title);
    } else if (cmd === "cancel") {
      state.jobs = state.jobs.filter((j) => j.id !== jobId);
      log(`worker ← cancel ${jobId}`);
      toast("Cancelled", job.title, "bad");
      if (state.detailJobId === jobId) state.detailJobId = null;
    } else if (cmd === "dismiss" && (job.status === "done" || job.status === "failed")) {
      state.jobs = state.jobs.filter((j) => j.id !== jobId);
      log(`worker ← dismiss ${jobId}`);
      if (state.detailJobId === jobId) state.detailJobId = null;
    } else if (cmd === "open") {
      state.workspaceFocus = job.title;
      log(`workspace ← open ${job.id}`);
      toast("Open workspace", `Focusing standalone Omachess · ${job.title}`);
      state.popupOpen = false;
      state.overlayOpen = false;
      state.detailJobId = null;
    } else if (cmd === "retry" && job.status === "failed") {
      job.status = "running";
      job.progress = Math.max(5, job.progress - 10);
      job.error = null;
      job.detail = "Retrying from last checkpoint";
      log(`worker ← retry ${job.id}`);
      toast("Retrying", job.title);
    }
    render();
  }

  function injectJob() {
    if (!state.worker.alive) return;
    const n = state.jobs.length + 1;
    state.jobs.unshift({
      id: `job-${++idSeq}`,
      kind: "Computer Analysis",
      title: `Ad-hoc analysis #${n}`,
      detail: "Stockfish 17 · depth target 20 · move 1/30",
      progress: 2,
      status: "running",
      openedFrom: "workspace",
      startedAt: now(),
    });
    log("worker · new Computer Analysis job");
    toast("Job started", "Computer Analysis queued on worker");
    render();
  }

  function restartShell() {
    if (state.shell.restarting) return;
    state.shell.restarting = true;
    state.shell.alive = false;
    state.popupOpen = false;
    state.overlayOpen = false;
    state.detailJobId = null;
    log("shell · restarting (plugin process gone)");
    // Worker keeps running — jobs still tick
    render();
    setTimeout(() => {
      state.shell.restarting = false;
      state.shell.alive = true;
      state.shell.attachCount += 1;
      state.shell.lastAttach = now();
      log("shell · back · plugin reattached to worker status");
      toast("Shell reattached", `Plugin attach #${state.shell.attachCount} · jobs uninterrupted`);
      render();
    }, 1600);
  }

  function killWorker() {
    state.worker.alive = false;
    state.jobs.forEach((j) => {
      if (j.status === "running" || j.status === "completing") {
        j.status = "paused";
        j.detail = "Worker stopped · job checkpointed";
      }
    });
    log("worker · stopped (jobs checkpointed)");
    toast("Worker stopped", "Jobs checkpointed; shell still shows last known state", "bad");
    render();
  }

  function startWorker() {
    state.worker.alive = true;
    state.worker.pid = 40000 + Math.floor(Math.random() * 20000);
    state.worker.restarts += 1;
    state.worker.lastHeartbeat = now();
    log("worker · started");
    toast("Worker online", `pid ${state.worker.pid}`);
    render();
  }

  // Progress simulation on the worker only
  setInterval(() => {
    if (!state.worker.alive) return;
    state.worker.lastHeartbeat = now();
    let changed = false;
    state.jobs.forEach((j) => {
      if (j.status === "running") {
        j.progress = Math.min(100, j.progress + (0.4 + Math.random() * 0.9));
        if (j.progress >= 100) {
          j.progress = 100;
          j.status = "completing";
          j.detail = "Writing Analysis Record…";
        } else {
          const move = Math.min(40, Math.floor(j.progress / 2.5) + 1);
          j.detail = j.detail.replace(/move \d+\/\d+/, `move ${move}/41`) || j.detail;
        }
        changed = true;
      } else if (j.status === "completing") {
        j.status = "done";
        j.detail = "Analysis Record ready";
        toast("Analysis complete", j.title, "ok");
        log(`worker · completed ${j.id}`);
        changed = true;
      }
    });
    if (changed) render();
  }, 900);

  // —— Render helpers ——
  function esc(s) {
    return String(s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function pct(j) {
    return Math.round(j.progress);
  }

  function statusLabel(s) {
    return ({
      running: "Running",
      paused: "Paused",
      completing: "Finishing",
      done: "Complete",
      failed: "Failed",
    })[s] || s;
  }

  function jobActionsHtml(job, compact = false) {
    const bits = [];
    if (job.status === "running") {
      bits.push(`<button data-cmd="pause" data-id="${job.id}">Pause</button>`);
    }
    if (job.status === "paused") {
      bits.push(`<button data-cmd="resume" data-id="${job.id}" class="primary">Resume</button>`);
    }
    if (["running", "paused", "completing"].includes(job.status)) {
      bits.push(`<button data-cmd="cancel" data-id="${job.id}" class="danger">Cancel</button>`);
    }
    if (job.status === "failed") {
      bits.push(`<button data-cmd="retry" data-id="${job.id}" class="primary">Retry</button>`);
      bits.push(`<button data-cmd="dismiss" data-id="${job.id}">Dismiss</button>`);
    }
    if (job.status === "done") {
      bits.push(`<button data-cmd="dismiss" data-id="${job.id}">Dismiss</button>`);
    }
    bits.push(`<button data-cmd="open" data-id="${job.id}" class="primary">${compact ? "Open" : "Open workspace"}</button>`);
    return `<div class="job-actions">${bits.join("")}</div>`;
  }

  function progressClass(status) {
    if (status === "paused") return "paused";
    if (status === "failed") return "failed";
    if (status === "done") return "done";
    return "";
  }

  // —— Variant A ——
  function renderVariantA() {
    const status = primaryStatus();
    const count = activeJobs().length;
    const idle = status === "idle";
    return `
      <div class="bar-section right" style="position:relative">
        <button class="bar-chip ghost" title="mock">󰂚</button>
        <button class="bar-chip ghost" title="mock">󰖩</button>
        <button class="vA-chip ${status} ${state.popupOpen ? "open" : ""} ${idle ? "idle" : ""}"
                id="vA-toggle" title="Omachess jobs">
          ${idle ? `<span class="glyph">♟</span><span>Omachess</span>` : `
            <span class="ring"></span>
            <span>${count} job${count === 1 ? "" : "s"}</span>
            <span style="color:var(--fg-dim)">${aggregateProgress()}%</span>
          `}
        </button>
        <button class="bar-chip ghost" title="mock">󰥔 14:32</button>
        ${state.popupOpen ? renderAPopup() : ""}
      </div>`;
  }

  function renderAPopup() {
    const jobs = state.jobs;
    if (!jobs.length) {
      return `<div class="vA-popup"><div class="vA-empty">No background jobs.<br/>Worker is ${state.worker.alive ? "idle" : "offline"}.</div></div>`;
    }
    return `
      <div class="vA-popup">
        <header>
          <h4>Omachess jobs</h4>
          <span>worker ${state.worker.alive ? "· live" : "· offline"}</span>
        </header>
        ${jobs
          .map(
            (j) => `
          <div class="vA-job">
            <div class="vA-job-top">
              <span class="status-dot ${j.status}"></span>
              <div class="vA-job-title">
                <strong>${esc(j.title)}</strong>
                <em>${esc(j.kind)} · ${statusLabel(j.status)} · ${pct(j)}%</em>
                <em>${esc(j.detail)}${j.error ? " · " + esc(j.error) : ""}</em>
              </div>
            </div>
            <div class="progress ${progressClass(j.status)}" style="margin-bottom:8px">
              <i style="width:${pct(j)}%"></i>
            </div>
            ${jobActionsHtml(j)}
          </div>`
          )
          .join("")}
      </div>`;
  }

  // —— Variant B ——
  function renderVariantB() {
    const jobs = state.jobs;
    const detail = jobs.find((j) => j.id === state.detailJobId);
    return `
      <div class="bar-section right" style="position:relative; gap:8px">
        <div class="vB-strip" id="vB-strip">
          ${
            jobs.length
              ? jobs
                  .map(
                    (j) => `
            <div class="vB-pill ${j.status}" data-detail="${j.id}" title="${esc(j.detail)}">
              <span class="status-dot ${j.status}"></span>
              <span class="name">${esc(j.title.split(",")[0].split("·")[0].trim())}</span>
              <span class="mini-progress"><i style="width:${pct(j)}%"></i></span>
              <span class="pct">${pct(j)}%</span>
              ${
                j.status === "running"
                  ? `<button class="icon" data-cmd="pause" data-id="${j.id}" title="Pause">⏸</button>`
                  : ""
              }
              ${
                j.status === "paused"
                  ? `<button class="icon" data-cmd="resume" data-id="${j.id}" title="Resume">▶</button>`
                  : ""
              }
              ${
                ["running", "paused"].includes(j.status)
                  ? `<button class="icon danger" data-cmd="cancel" data-id="${j.id}" title="Cancel">✕</button>`
                  : ""
              }
              ${
                j.status === "failed"
                  ? `<button class="icon" data-cmd="retry" data-id="${j.id}" title="Retry">↻</button>`
                  : ""
              }
              <button class="icon" data-cmd="open" data-id="${j.id}" title="Open workspace">↗</button>
            </div>`
                  )
                  .join("")
              : `<span class="vB-empty">No Omachess jobs</span>`
          }
        </div>
        <button class="bar-chip ghost" title="mock">󰥔 14:32</button>
        ${
          detail
            ? `
          <div class="vB-detail">
            <h4>${esc(detail.title)}</h4>
            <p>${esc(detail.kind)} · ${statusLabel(detail.status)}<br/>${esc(detail.detail)}</p>
            <div class="progress ${progressClass(detail.status)}" style="margin-bottom:10px">
              <i style="width:${pct(detail)}%"></i>
            </div>
            ${jobActionsHtml(detail)}
          </div>`
            : ""
        }
      </div>`;
  }

  // —— Variant C ——
  function renderVariantC() {
    const status = primaryStatus();
    const count = activeJobs().length;
    const idle = status === "idle";
    return `
      <div class="bar-section right">
        <button class="bar-chip ghost" title="mock">󰂚</button>
        <button class="vC-badge ${status} ${state.overlayOpen ? "active" : ""} ${idle ? "idle" : ""}"
                id="vC-toggle" title="Omachess jobs (summon)">
          <span>♟</span>
          ${idle ? `<span>Jobs</span>` : `<span class="badge ${status === "failed" ? "bad" : ""}">${count}</span>`}
        </button>
        <button class="bar-chip ghost" title="mock">󰥔 14:32</button>
      </div>`;
  }

  function renderOverlay() {
    if (!state.overlayOpen || state.variant !== "C") return "";
    const q = state.overlayFilter.trim().toLowerCase();
    const jobs = state.jobs.filter(
      (j) =>
        !q ||
        j.title.toLowerCase().includes(q) ||
        j.kind.toLowerCase().includes(q) ||
        j.status.includes(q)
    );
    const sel = Math.min(state.overlaySelected, Math.max(0, jobs.length - 1));
    state.overlaySelected = sel;
    return `
      <div class="overlay-scrim" id="overlay-scrim">
        <div class="overlay-panel" role="dialog" aria-label="Omachess jobs">
          <header>
            <div>
              <h2>Omachess jobs</h2>
              <div class="hint">Worker ${state.worker.alive ? "live" : "offline"} · Esc close · ↑↓ select · Enter open · P pause · C cancel</div>
            </div>
            <button class="btn" id="overlay-close">Close</button>
          </header>
          <input class="overlay-search" id="overlay-search" placeholder="Filter jobs…" value="${esc(state.overlayFilter)}" />
          ${
            jobs.length
              ? jobs
                  .map(
                    (j, i) => `
            <div class="overlay-job ${i === sel ? "selected" : ""}" data-idx="${i}" data-id="${j.id}">
              <span class="status-dot ${j.status}"></span>
              <div>
                <div class="title">${esc(j.title)}</div>
                <div class="sub">${esc(j.kind)} · ${statusLabel(j.status)} · ${pct(j)}% · ${esc(j.detail)}</div>
                <div class="progress ${progressClass(j.status)}"><i style="width:${pct(j)}%"></i></div>
              </div>
              <div>${jobActionsHtml(j, true)}</div>
            </div>`
                  )
                  .join("")
              : `<div class="overlay-empty">No matching jobs.<br/>Background work is owned by the Omachess worker, not this overlay.</div>`
          }
        </div>
      </div>`;
  }

  function renderBarMiddle(rightHtml) {
    return `
      <header class="shell-bar ${state.shell.restarting ? "restarting" : ""}" id="shell-bar">
        <div class="bar-section left">
          <button class="bar-chip">󰣇 Omarchy</button>
          <button class="bar-chip ghost">1</button>
          <button class="bar-chip ghost">2</button>
          <button class="bar-chip ghost">3</button>
        </div>
        <div class="bar-section center">
          <span class="bar-chip ghost">Omachess prototype · shell plugin</span>
        </div>
        ${rightHtml}
      </header>`;
  }

  function renderWorkspace() {
    return `
      <div class="workspace-stage">
        <div class="workspace-window">
          <div class="workspace-titlebar">
            <div class="dots">
              <span class="dot r"></span><span class="dot y"></span><span class="dot g"></span>
            </div>
            <span>Omachess — standalone workspace</span>
            <span style="color:var(--fg-dim)">app_id · local.omachess</span>
          </div>
          <div class="workspace-body">
            <aside class="ws-rail">
              <p class="ws-note">Personal Library</p>
              <div class="ws-meta">Played Games<br/>Analysis Records<br/>Studies</div>
            </aside>
            <div class="ws-board">
              <div>
                <div class="board"></div>
                <div class="ws-label">Not a shell surface — ordinary Wayland window</div>
              </div>
            </div>
            <aside class="ws-side">
              <p class="ws-note">Focus</p>
              <div class="ws-meta">${esc(state.workspaceFocus)}</div>
              <p class="ws-note" style="margin-top:16px">Background</p>
              <div class="ws-meta">
                Computer Analysis may continue after this window closes (consent).
                Controls live in the Omarchy shell plugin above.
              </div>
            </aside>
          </div>
        </div>
      </div>`;
  }

  function renderArch() {
    const w = state.worker;
    const s = state.shell;
    return `
      <aside class="arch-strip">
        <h3>Lifecycle surface</h3>
        <div class="arch-row"><span class="k">Omachess worker</span><span class="v ${w.alive ? "ok" : "bad"}">${w.alive ? "alive · pid " + w.pid : "stopped"}</span></div>
        <div class="arch-row"><span class="k">Jobs owned by</span><span class="v">worker (not shell)</span></div>
        <div class="arch-row"><span class="k">omarchy-shell</span><span class="v ${s.alive ? "ok" : "warn"}">${s.restarting ? "restarting…" : s.alive ? "alive" : "down"}</span></div>
        <div class="arch-row"><span class="k">Plugin attaches</span><span class="v">${s.attachCount}</span></div>
        <div class="arch-row"><span class="k">Active jobs</span><span class="v">${activeJobs().length}</span></div>
        <div class="arch-actions">
          <button id="btn-restart-shell">Restart shell</button>
          <button id="btn-inject">+ Job</button>
          ${
            w.alive
              ? `<button id="btn-kill-worker" class="danger">Stop worker</button>`
              : `<button id="btn-start-worker">Start worker</button>`
          }
        </div>
        <div class="arch-hint">
          <strong style="color:var(--fg)">Judge:</strong> controls must reappear after shell restart without dropping jobs.
          Variant ${state.variant}: ${esc(VARIANTS[state.variant].name)}.
        </div>
        ${
          state.logs.length
            ? `<div class="arch-hint" style="margin-top:6px;max-height:72px;overflow:auto">${state.logs
                .map((l) => esc(l.msg))
                .join("<br/>")}</div>`
            : ""
        }
      </aside>`;
  }

  function renderPrototypeBar() {
    const v = VARIANTS[state.variant];
    const el = document.getElementById("prototype-bar");
    el.innerHTML = `
      <button type="button" id="proto-prev" aria-label="Previous variant">←</button>
      <div class="label"><strong>${v.key} — ${esc(v.name)}</strong><small>${esc(v.blurb)}</small></div>
      <button type="button" id="proto-next" aria-label="Next variant">→</button>
    `;
    el.querySelector("#proto-prev").onclick = () => cycle(-1);
    el.querySelector("#proto-next").onclick = () => cycle(1);
  }

  function cycle(dir) {
    const i = ORDER.indexOf(state.variant);
    setVariant(ORDER[(i + dir + ORDER.length) % ORDER.length]);
  }

  function renderToasts() {
    document.getElementById("toast-root").innerHTML = state.toasts
      .map(
        (t) => `
      <div class="toast ${t.kind}">
        <strong>${esc(t.title)}</strong>
        <span>${esc(t.body)}</span>
      </div>`
      )
      .join("");
  }

  function renderHelp() {
    if (!state.helpOpen) return "";
    return `
      <div class="help-scrim" id="help-scrim">
        <div class="help-card">
          <h2>Prototype keys</h2>
          <dl>
            <dt>← →</dt><dd>Cycle variants A / B / C</dd>
            <dt>R</dt><dd>Restart omarchy-shell (jobs must survive)</dd>
            <dt>N</dt><dd>Inject a new Computer Analysis job</dd>
            <dt>Space</dt><dd>A: toggle popup · C: toggle overlay</dd>
            <dt>?</dt><dd>Toggle this help</dd>
          </dl>
          <p>
            Shared decision under test: the shell plugin is only a control surface.
            Execution lives in the Omachess worker so analysis stays safe across shell restarts.
          </p>
        </div>
      </div>`;
  }

  function renderRight() {
    if (state.variant === "A") return renderVariantA();
    if (state.variant === "B") return renderVariantB();
    return renderVariantC();
  }

  function render() {
    const app = document.getElementById("app");
    app.innerHTML = `
      <div class="desktop">
        ${renderBarMiddle(renderRight())}
        ${renderWorkspace()}
      </div>
      ${renderArch()}
      ${renderOverlay()}
      ${state.shell.restarting ? `<div class="shell-restart-banner">omarchy-shell restarting… worker still running</div>` : ""}
      ${renderHelp()}
    `;
    renderToasts();
    renderPrototypeBar();
    bind();
  }

  function bind() {
    // Architecture controls
    const rs = document.getElementById("btn-restart-shell");
    if (rs) rs.onclick = restartShell;
    const inj = document.getElementById("btn-inject");
    if (inj) inj.onclick = injectJob;
    const kw = document.getElementById("btn-kill-worker");
    if (kw) kw.onclick = killWorker;
    const sw = document.getElementById("btn-start-worker");
    if (sw) sw.onclick = startWorker;

    // Shared command delegation
    document.querySelectorAll("[data-cmd]").forEach((btn) => {
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        sendCommand(btn.getAttribute("data-id"), btn.getAttribute("data-cmd"));
      });
    });

    // A: toggle
    const aToggle = document.getElementById("vA-toggle");
    if (aToggle) {
      aToggle.onclick = (e) => {
        e.stopPropagation();
        state.popupOpen = !state.popupOpen;
        render();
      };
    }

    // B: detail on pill click (not on action buttons)
    document.querySelectorAll(".vB-pill").forEach((pill) => {
      pill.addEventListener("click", (e) => {
        if (e.target.closest("[data-cmd]")) return;
        const id = pill.getAttribute("data-detail");
        state.detailJobId = state.detailJobId === id ? null : id;
        render();
      });
    });

    // C: overlay
    const cToggle = document.getElementById("vC-toggle");
    if (cToggle) {
      cToggle.onclick = () => {
        state.overlayOpen = !state.overlayOpen;
        render();
      };
    }
    const oc = document.getElementById("overlay-close");
    if (oc) oc.onclick = () => {
      state.overlayOpen = false;
      render();
    };
    const scrim = document.getElementById("overlay-scrim");
    if (scrim) {
      scrim.addEventListener("click", (e) => {
        if (e.target === scrim) {
          state.overlayOpen = false;
          render();
        }
      });
    }
    const search = document.getElementById("overlay-search");
    if (search) {
      search.addEventListener("input", () => {
        state.overlayFilter = search.value;
        state.overlaySelected = 0;
        // Avoid full re-render thrash: only update list would be nicer; keep simple
        const pos = search.selectionStart;
        render();
        const again = document.getElementById("overlay-search");
        if (again) {
          again.focus();
          again.setSelectionRange(pos, pos);
        }
      });
    }

    const help = document.getElementById("help-scrim");
    if (help) {
      help.onclick = (e) => {
        if (e.target === help) {
          state.helpOpen = false;
          render();
        }
      };
    }

    // Click outside closes A popup / B detail
    document.addEventListener(
      "click",
      (e) => {
        if (state.variant === "A" && state.popupOpen) {
          if (!e.target.closest(".vA-popup") && !e.target.closest("#vA-toggle")) {
            state.popupOpen = false;
            render();
          }
        }
        if (state.variant === "B" && state.detailJobId) {
          if (!e.target.closest(".vB-detail") && !e.target.closest(".vB-pill")) {
            state.detailJobId = null;
            render();
          }
        }
      },
      { once: true }
    );
  }

  document.addEventListener("keydown", (e) => {
    const tag = (e.target && e.target.tagName) || "";
    const typing = tag === "INPUT" || tag === "TEXTAREA" || e.target?.isContentEditable;

    if (e.key === "?" && !typing) {
      e.preventDefault();
      state.helpOpen = !state.helpOpen;
      render();
      return;
    }
    if (e.key === "Escape") {
      if (state.helpOpen) {
        state.helpOpen = false;
        render();
        return;
      }
      if (state.overlayOpen) {
        state.overlayOpen = false;
        render();
        return;
      }
      if (state.popupOpen) {
        state.popupOpen = false;
        render();
        return;
      }
      if (state.detailJobId) {
        state.detailJobId = null;
        render();
      }
      return;
    }
    if (typing) {
      // Overlay list nav still works when not in search? search is typing.
      return;
    }
    if (e.key === "ArrowLeft") {
      e.preventDefault();
      cycle(-1);
    } else if (e.key === "ArrowRight") {
      e.preventDefault();
      cycle(1);
    } else if (e.key === "r" || e.key === "R") {
      restartShell();
    } else if (e.key === "n" || e.key === "N") {
      injectJob();
    } else if (e.key === " ") {
      e.preventDefault();
      if (state.variant === "A") {
        state.popupOpen = !state.popupOpen;
        render();
      } else if (state.variant === "C") {
        state.overlayOpen = !state.overlayOpen;
        render();
      }
    } else if (state.variant === "C" && state.overlayOpen) {
      const jobs = state.jobs;
      if (e.key === "ArrowDown") {
        e.preventDefault();
        state.overlaySelected = Math.min(jobs.length - 1, state.overlaySelected + 1);
        render();
      } else if (e.key === "ArrowUp") {
        e.preventDefault();
        state.overlaySelected = Math.max(0, state.overlaySelected - 1);
        render();
      } else if (e.key === "Enter" && jobs[state.overlaySelected]) {
        sendCommand(jobs[state.overlaySelected].id, "open");
      } else if ((e.key === "p" || e.key === "P") && jobs[state.overlaySelected]) {
        const j = jobs[state.overlaySelected];
        sendCommand(j.id, j.status === "paused" ? "resume" : "pause");
      } else if ((e.key === "c" || e.key === "C") && jobs[state.overlaySelected]) {
        sendCommand(jobs[state.overlaySelected].id, "cancel");
      }
    }
  });

  log("prototype ready · worker owns jobs · shell is control surface");
  render();
})();
