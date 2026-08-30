"use strict";

const $ = (id) => document.getElementById(id);

// The only model in this build.
const MODEL_ID = "kokoro-82m";

// Human-friendly language labels for Kokoro's short codes.
const KOKORO_LANG_LABELS = { a: "US English", b: "UK English" };

let current = null;       // the kokoro model object from /api/models
let pollTimer = null;

// ---------------- model status ----------------
async function loadModel() {
  const res = await fetch("/api/models");
  const { models } = await res.json();
  current = models.find((m) => m.id === MODEL_ID) || models[0] || null;
  if (!current) {
    setStatus("No model found on the server.");
    return;
  }

  $("modelMeta").textContent =
    `${current.family} - ${current.repo_id} - ${current.approx_size}`;

  refreshDownloadUI();
  fillLanguages(current);
  loadVoices();
}

// Build the setup-needed message from the model's `needs` list.
function setupLabel(needs) {
  const hasPkg = needs.includes("package");
  const hasWeights = needs.includes("weights");
  if (hasPkg && hasWeights) return { msg: "Kokoro needs a one-time setup (install package + download weights).", btn: "Set up" };
  if (hasPkg) return { msg: "Kokoro needs its runtime package installed (one-time).", btn: "Install" };
  return { msg: "Kokoro needs to be downloaded first (one-time).", btn: "Download" };
}

function refreshDownloadUI() {
  const setupBox = $("setupBox");
  const readyMsg = $("readyMsg");
  const dl = current.download || { state: "idle" };
  const downloading = dl.state === "downloading";
  const ready = current.ready && !downloading;

  // Gate the Generate button on readiness.
  $("genBtn").disabled = !ready;
  $("genBtn").title = ready ? "" : "Set up the model before generating.";

  if (ready) {
    setupBox.classList.add("hidden");
    readyMsg.classList.remove("hidden");
    return;
  }
  readyMsg.classList.add("hidden");
  setupBox.classList.remove("hidden");

  const needs = current.needs || [];
  const lbl = setupLabel(needs);
  $("downloadBtn").disabled = downloading;
  $("downloadBtn").textContent = downloading ? "Working..." : lbl.btn;
  $("progWrap").classList.toggle("hidden", !downloading);
  if (downloading) {
    const stage = dl.stage ? dl.stage + " " : "";
    $("progBar").style.width = (dl.percent || 0) + "%";
    $("downloadStatus").textContent = dl.total_bytes
      ? `${stage}${dl.percent || 0}% (${fmtBytes(dl.downloaded_bytes)} / ${fmtBytes(dl.total_bytes)})`
      : (stage || "Working...");
  } else {
    $("setupMsg").textContent = lbl.msg;
    $("downloadStatus").textContent = `${current.approx_size} download.`;
  }
}

function fillLanguages(model) {
  const sel = $("language");
  sel.innerHTML = "";
  const langs = model.languages && model.languages.length ? model.languages : ["a"];
  for (const l of langs) {
    const opt = document.createElement("option");
    opt.value = l;
    opt.textContent = KOKORO_LANG_LABELS[l] || l;
    if (l === "a") opt.selected = true;
    sel.appendChild(opt);
  }
}

async function loadVoices() {
  try {
    const res = await fetch("/api/models/" + MODEL_ID + "/options");
    const data = await res.json();
    const items = data.voices || [];
    const sel = $("voice");
    sel.innerHTML = "";
    for (const it of items) {
      const opt = document.createElement("option");
      opt.value = it;
      opt.textContent = it;
      sel.appendChild(opt);
    }
  } catch (e) {
    $("voice").innerHTML = "<option value=''>Could not load</option>";
  }
}

// ---------------- download ----------------
$("downloadBtn").addEventListener("click", async () => {
  if (!current) return;
  await fetch("/api/models/" + MODEL_ID + "/download", { method: "POST" });
  startPolling();
});

function startPolling() {
  stopPolling();
  pollTimer = setInterval(async () => {
    const res = await fetch("/api/models/" + MODEL_ID + "/download");
    const status = await res.json();
    if (current) {
      current.download = status;
      if ("downloaded" in status) current.downloaded = status.downloaded;
      if ("ready" in status) current.ready = status.ready;
      if ("needs" in status) current.needs = status.needs;
    }
    refreshDownloadUI();

    if (status.state === "done" || status.state === "error") {
      stopPolling();
      if (status.state === "error") {
        $("downloadStatus").textContent = "Download failed: " + (status.error || "");
      } else {
        await loadModel(); // refresh readiness + voices
      }
    }
  }, 1000);
}

function stopPolling() {
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = null;
}

// ---------------- status helper ----------------
function setStatus(msg) { $("statusMsg").textContent = msg || ""; }
function showError(msg) { setStatus("Error: " + msg); }

async function postForm(url, fd) {
  const res = await fetch(url, { method: "POST", body: fd });
  if (!res.ok) {
    let detail = res.statusText;
    try { detail = (await res.json()).detail || detail; } catch (_) {}
    throw new Error(detail);
  }
  return res.json();
}

// ---------------- generate ----------------
$("genBtn").addEventListener("click", async () => {
  if (!current) return showError("Model not loaded yet.");
  if (!current.ready) return showError("Set up the model first (see the button above).");

  const text = $("text").value.trim();
  if (!text) return showError("Enter text to speak.");
  if (!$("voice").value) return showError("Pick a voice.");

  const fd = new FormData();
  fd.append("model_id", MODEL_ID);
  fd.append("text", text);
  fd.append("language", $("language").value);
  fd.append("voice", $("voice").value);
  fd.append("speed", $("speed").value);

  try {
    const job = await postForm("/api/synthesize", fd);
    setStatus("Queued: " + (job.label || job.id));
    refreshQueue();
    ensureQueuePolling();
  } catch (e) {
    showError(e.message);
  }
});

// ---------------- queue ----------------
const resultUrls = {};   // job id -> object URL (revoked on removal)
let queuePollTimer = null;
let lastQueueSig = "";

async function refreshQueue() {
  let jobs = [];
  try {
    jobs = (await (await fetch("/api/jobs")).json()).jobs || [];
  } catch (_) {
    return;
  }

  // Signature of everything that affects rendering. If only elapsed/percent of
  // a running job changed, update those in place to avoid interrupting audio
  // playback of finished jobs by rebuilding the DOM.
  const sig = jobs.map((j) => `${j.id}:${j.state}:${j.position || 0}:${j.has_result}`).join("|");
  if (sig === lastQueueSig) {
    updateRunningInPlace(jobs);
    return;
  }
  lastQueueSig = sig;

  const list = $("queueList");
  $("queueEmpty").classList.toggle("hidden", jobs.length > 0);

  // Track which job ids are still present so we can revoke stale URLs.
  const present = new Set(jobs.map((j) => j.id));
  for (const id of Object.keys(resultUrls)) {
    if (!present.has(id)) { URL.revokeObjectURL(resultUrls[id]); delete resultUrls[id]; }
  }

  list.innerHTML = "";
  for (const j of jobs) list.appendChild(renderJob(j));

  // Keep polling while anything is active.
  const active = jobs.some((j) =>
    ["queued", "running", "canceling"].includes(j.state));
  if (!active) stopQueuePolling();
}

// Update just the timer/percentage of running jobs without rebuilding the DOM.
function updateRunningInPlace(jobs) {
  for (const j of jobs) {
    if (j.state !== "running" && j.state !== "canceling") continue;
    const li = document.querySelector('.job[data-id="' + j.id + '"]');
    if (!li) { lastQueueSig = ""; return; }  // force rebuild if missing
    const meta = li.querySelector(".job-meta");
    if (meta) {
      let s;
      if (j.state === "canceling") {
        s = "canceling...";
      } else {
        s = "rendering... " + fmtDur(j.elapsed);
        if (j.eta_seconds) s += " / ~" + fmtDur(j.eta_seconds);
      }
      meta.textContent = s;
    }
    const bar = li.querySelector(".job-prog .prog-bar");
    const wrap = li.querySelector(".job-prog");
    if (bar && j.est_percent) {
      if (wrap) wrap.classList.remove("indeterminate");
      bar.style.width = j.est_percent + "%";
    }
  }
}

function renderJob(j) {
  const li = document.createElement("li");
  li.className = "job job-" + j.state;
  li.setAttribute("data-id", j.id);

  const main = document.createElement("div");
  main.className = "job-main";

  const title = document.createElement("div");
  title.className = "job-label";
  title.textContent = j.label || j.id;
  main.appendChild(title);

  const meta = document.createElement("div");
  meta.className = "job-meta";
  let state = j.state;
  if (j.state === "queued" && j.position) state = "queued #" + j.position;
  if (j.state === "running") {
    state = "rendering... " + fmtDur(j.elapsed);
    if (j.eta_seconds) state += " / ~" + fmtDur(j.eta_seconds);
  }
  if (j.state === "canceling") state = "canceling...";
  if (j.state === "done" && j.elapsed != null) state = "done in " + fmtDur(j.elapsed);
  meta.textContent = state + (j.error ? (" - " + j.error) : "");
  main.appendChild(meta);

  // Estimated progress bar for the running job.
  if (j.state === "running") {
    const wrap = document.createElement("div");
    wrap.className = "prog job-prog";
    const bar = document.createElement("div");
    bar.className = "prog-bar";
    if (j.est_percent) {
      bar.style.width = j.est_percent + "%";
    } else {
      // No estimate yet (first render of this model): indeterminate stripes.
      wrap.classList.add("indeterminate");
      bar.style.width = "100%";
    }
    wrap.appendChild(bar);
    main.appendChild(wrap);
    if (j.est_percent) {
      const pct = document.createElement("div");
      pct.className = "job-meta";
      pct.textContent = "~" + j.est_percent + "% (estimated)";
      main.appendChild(pct);
    }
  }

  li.appendChild(main);

  // Result player for done jobs.
  if (j.state === "done" && j.has_result) {
    const audio = document.createElement("audio");
    audio.controls = true;
    audio.className = "audio";
    if (!resultUrls[j.id]) {
      // Lazy-load the WAV once and cache the object URL.
      fetch("/api/jobs/" + j.id + "/result")
        .then((r) => r.blob())
        .then((b) => { resultUrls[j.id] = URL.createObjectURL(b); audio.src = resultUrls[j.id]; });
    } else {
      audio.src = resultUrls[j.id];
    }
    li.appendChild(audio);
  }

  // Actions.
  const actions = document.createElement("div");
  actions.className = "job-actions";

  // Cancel works for both queued and running jobs.
  if (j.state === "queued" || j.state === "running") {
    actions.appendChild(mkBtn("Cancel", "ts-btn-danger", async (btn) => {
      btn.disabled = true;
      await fetch("/api/jobs/" + j.id + "/cancel", { method: "POST" });
      refreshQueue();
    }));
  }
  if (j.state === "canceling") {
    const s = document.createElement("span");
    s.className = "job-meta";
    s.textContent = "stopping render...";
    actions.appendChild(s);
  }
  if (j.state === "done" && j.has_result) {
    const a = document.createElement("a");
    a.className = "ts-btn ts-btn-ghost text-xs py-1.5";
    a.textContent = "Download";
    a.href = "/api/jobs/" + j.id + "/result";
    a.download = "tts-" + j.id + ".wav";
    actions.appendChild(a);
  }
  if (["done", "error", "canceled"].includes(j.state)) {
    actions.appendChild(mkBtn("Remove", "ts-btn-ghost", async (btn) => {
      await fetch("/api/jobs/" + j.id, { method: "DELETE" });
      refreshQueue();
    }));
  }
  li.appendChild(actions);
  return li;
}

function mkBtn(label, kind, onClick) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = "ts-btn " + kind + " text-xs py-1.5";
  b.textContent = label;
  b.addEventListener("click", () => onClick(b));
  return b;
}

function ensureQueuePolling() {
  if (queuePollTimer) return;
  queuePollTimer = setInterval(refreshQueue, 1000);
}
function stopQueuePolling() {
  if (queuePollTimer) clearInterval(queuePollTimer);
  queuePollTimer = null;
}

$("clearBtn").addEventListener("click", async () => {
  await fetch("/api/jobs/clear", { method: "POST" });
  refreshQueue();
});

// ---------------- settings (Ollama) ----------------
async function loadSettings() {
  try {
    const s = await (await fetch("/api/settings")).json();
    if (s.ollama_host) $("ollamaHost").value = s.ollama_host;
    if (s.ollama_model) $("ollamaModel").value = s.ollama_model;
    if (s.ollama_system_prompt) $("ollamaPrompt").value = s.ollama_system_prompt;
  } catch (_) {}
}

function setOllamaStatus(reachable) {
  const badge = $("ollamaStatus");
  if (reachable) {
    badge.textContent = "connected";
    badge.className = "text-xs px-2.5 py-1 rounded-full bg-emerald-500/15 text-emerald-300 border border-emerald-500/30";
  } else {
    badge.textContent = "not reachable";
    badge.className = "text-xs px-2.5 py-1 rounded-full bg-amber-500/15 text-amber-300 border border-amber-500/30";
  }
}

$("saveSettingsBtn").addEventListener("click", async () => {
  const btn = $("saveSettingsBtn");
  btn.disabled = true;
  $("settingsStatus").textContent = "Saving...";
  const fd = new FormData();
  fd.append("ollama_host", $("ollamaHost").value.trim());
  fd.append("ollama_model", $("ollamaModel").value.trim());
  fd.append("ollama_system_prompt", $("ollamaPrompt").value.trim());
  try {
    const s = await postForm("/api/settings", fd);
    if (s.ollama_host) $("ollamaHost").value = s.ollama_host;
    if (s.ollama_model) $("ollamaModel").value = s.ollama_model;
    $("settingsStatus").textContent = "Saved.";
    loadHealth();
  } catch (e) {
    $("settingsStatus").textContent = "Save failed: " + e.message;
  } finally {
    btn.disabled = false;
  }
});

$("listModelsBtn").addEventListener("click", async () => {
  const btn = $("listModelsBtn");
  btn.disabled = true;
  $("settingsStatus").textContent = "Fetching models...";
  try {
    const res = await fetch("/api/ollama/models");
    if (!res.ok) {
      let detail = res.statusText;
      try { detail = (await res.json()).detail || detail; } catch (_) {}
      throw new Error(detail);
    }
    const { models } = await res.json();
    const dl = $("ollamaModelList");
    dl.innerHTML = "";
    (models || []).forEach((m) => {
      const o = document.createElement("option");
      o.value = m;
      dl.appendChild(o);
    });
    if (models && models.length) {
      const cur = $("ollamaModel").value.trim();
      if (!cur || !models.includes(cur)) $("ollamaModel").value = models[0];
    }
    $("settingsStatus").textContent = `Found ${(models || []).length} models. Pick one, then Save.`;
  } catch (e) {
    $("settingsStatus").textContent = "List failed: " + e.message;
  } finally {
    btn.disabled = false;
  }
});

$("resetChatBtn").addEventListener("click", async () => {
  try {
    await fetch("/api/chat/reset", { method: "POST" });
    $("settingsStatus").textContent = "Conversation reset.";
  } catch (_) {}
});

async function sendChatTest() {
  const text = $("chatTest").value.trim();
  if (!text) return;
  const btn = $("chatTestBtn");
  btn.disabled = true;
  $("chatTestReply").textContent = "Thinking...";
  const fd = new FormData();
  fd.append("text", text);
  try {
    const { reply } = await postForm("/api/chat", fd);
    $("chatTestReply").textContent = reply || "(empty reply)";
  } catch (e) {
    $("chatTestReply").textContent = "Error: " + e.message;
  } finally {
    btn.disabled = false;
  }
}
$("chatTestBtn").addEventListener("click", sendChatTest);
$("chatTest").addEventListener("keydown", (e) => { if (e.key === "Enter") sendChatTest(); });

// ---------------- misc ----------------
function fmtBytes(n) {
  if (!n) return "0 B";
  const u = ["B", "KB", "MB", "GB"];
  let i = 0, x = n;
  while (x >= 1024 && i < u.length - 1) { x /= 1024; i++; }
  return x.toFixed(1) + " " + u[i];
}

function fmtDur(s) {
  if (s == null) return "0s";
  s = Math.round(s);
  if (s < 60) return s + "s";
  const m = Math.floor(s / 60), r = s % 60;
  return m + "m " + (r < 10 ? "0" : "") + r + "s";
}

$("speed").addEventListener("input", () => { $("speedVal").textContent = $("speed").value; });

// Update the device badge.
async function loadHealth() {
  try {
    const h = await (await fetch("/api/health")).json();
    if (h.device) $("deviceBadge").textContent = h.device;
    setOllamaStatus(!!h.ollama_reachable);
  } catch (_) {}
}

// ---------------- received audio (debug) ----------------
async function refreshRecordings() {
  try {
    const { recordings } = await (await fetch("/api/recordings")).json();
    const list = $("recordingsList");
    $("recordingsEmpty").classList.toggle("hidden", (recordings || []).length > 0);
    list.innerHTML = "";
    (recordings || []).forEach((r) => {
      const li = document.createElement("li");
      li.className = "job";
      const meta = document.createElement("div");
      meta.className = "job-meta";
      meta.textContent = new Date(r.mtime * 1000).toLocaleTimeString() + "  \u2022  " + fmtBytes(r.size);
      const audio = document.createElement("audio");
      audio.controls = true;
      audio.className = "audio";
      audio.src = "/api/recordings/" + r.name;
      li.appendChild(meta);
      li.appendChild(audio);
      list.appendChild(li);
    });
  } catch (_) {}
}
$("refreshRecordingsBtn").addEventListener("click", refreshRecordings);

// init
loadHealth();
loadSettings();
loadModel();
refreshQueue();
refreshRecordings();
