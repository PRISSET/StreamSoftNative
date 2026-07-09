const el = (id) => document.getElementById(id);

const EVENT_KIND_LABELS = {
  follow: "Фоллоу",
  subscribe: "Подписка",
  gift_sub: "Подарочная подписка",
  raid: "Рейд",
  cheer: "Донат битсами",
};

const MEDIA_EXT_LABELS = { gif: "Гифка", mp3: "Звук" };

function updateRangeFill(input) {
  const min = parseFloat(input.min || 0);
  const max = parseFloat(input.max || 100);
  const pct = ((parseFloat(input.value) - min) / (max - min)) * 100;
  input.style.setProperty("--val", `${pct}%`);
}

async function loadSettings() {
  const res = await fetch("/api/settings");
  const data = await res.json();

  el("theme").value = data.theme || "minimal";
  const overlayScale = Math.round((data.overlay_scale ?? 1) * 100);
  el("overlay_scale").value = overlayScale;
  el("overlay_scale_value").textContent = `${overlayScale}%`;
  updateRangeFill(el("overlay_scale"));
  el("tts_voice_ru").value = data.tts_voice_ru || "";
  el("tts_voice_en").value = data.tts_voice_en || "";
  el("tts_rate").value = data.tts_rate || "+0%";
  el("tts_volume").value = data.tts_volume ?? 100;
  el("tts_volume_value").textContent = `${data.tts_volume ?? 100}%`;
  updateRangeFill(el("tts_volume"));
  el("tts_say_author").checked = !!data.tts_say_author;
  el("event_volume").value = data.event_volume ?? 100;
  el("event_volume_value").textContent = `${data.event_volume ?? 100}%`;
  updateRangeFill(el("event_volume"));

  el("rvc_enabled").checked = !!data.rvc_enabled;
  el("rvc_scope").value = data.rvc_scope || "alerts";
  if (data.rvc_model) el("rvc_model").value = data.rvc_model;

  const pitch = data.rvc_pitch ?? 12;
  el("rvc_pitch").value = pitch;
  el("rvc_pitch_value").textContent = pitch > 0 ? `+${pitch}` : `${pitch}`;
  updateRangeFill(el("rvc_pitch"));

  const indexRate = data.rvc_index_rate ?? 0.3;
  el("rvc_index_rate").value = indexRate;
  el("rvc_index_rate_value").textContent = indexRate.toFixed(2);
  updateRangeFill(el("rvc_index_rate"));

  const protect = data.rvc_protect ?? 0.5;
  el("rvc_protect").value = protect;
  el("rvc_protect_value").textContent = protect.toFixed(2);
  updateRangeFill(el("rvc_protect"));

  el("rvc_f0method").value = data.rvc_f0method || "rmvpe";

  renderMuted(data.muted || []);
}

async function loadRvcModels() {
  try {
    const res = await fetch("/api/rvc/models");
    const data = await res.json();
    const select = el("rvc_model");
    const current = select.value;
    select.innerHTML = "";
    for (const name of data.models || []) {
      const opt = document.createElement("option");
      opt.value = name;
      opt.textContent = name;
      select.appendChild(opt);
    }
    if (current) select.value = current;
  } catch {
    // RVC-сервис недоступен — оставляем список пустым
  }
}

async function refreshRvcStatus() {
  const dot = el("rvc-status-dot");
  const text = el("rvc-status-text");
  try {
    const res = await fetch("/api/rvc/health");
    const data = await res.json();
    dot.classList.toggle("on", !!data.available);
    text.textContent = data.available ? "RVC-сервис доступен" : "RVC-сервис недоступен (запусти rvc_service/start.bat)";
  } catch {
    dot.classList.remove("on");
    text.textContent = "RVC-сервис недоступен";
  }
}

function renderMuted(names) {
  const list = el("muted-list");
  list.innerHTML = "";
  if (!names.length) {
    list.innerHTML = `<span class="muted-empty">Никого не замьючено</span>`;
    return;
  }
  for (const name of names) {
    const chip = document.createElement("div");
    chip.className = "muted-chip";
    chip.innerHTML = `<span>${name}</span>`;
    const btn = document.createElement("button");
    btn.textContent = "✕";
    btn.onclick = async () => {
      await postSettings({ unmute: name });
      loadSettings();
    };
    chip.appendChild(btn);
    list.appendChild(chip);
  }
}

async function postSettings(payload) {
  const res = await fetch("/api/settings", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  return res.json();
}

function showStatus(text) {
  const status = el("status");
  status.textContent = text;
  status.classList.add("show");
  setTimeout(() => status.classList.remove("show"), 2000);
}

function collectSettingsPayload() {
  return {
    theme: el("theme").value,
    overlay_scale: parseInt(el("overlay_scale").value, 10) / 100,
    tts_voice_ru: el("tts_voice_ru").value,
    tts_voice_en: el("tts_voice_en").value,
    tts_rate: el("tts_rate").value,
    tts_volume: parseInt(el("tts_volume").value, 10),
    tts_say_author: el("tts_say_author").checked,
    event_volume: parseInt(el("event_volume").value, 10),
    rvc_enabled: el("rvc_enabled").checked,
    rvc_model: el("rvc_model").value,
    rvc_scope: el("rvc_scope").value,
    rvc_pitch: parseInt(el("rvc_pitch").value, 10),
    rvc_index_rate: parseFloat(el("rvc_index_rate").value),
    rvc_protect: parseFloat(el("rvc_protect").value),
    rvc_f0method: el("rvc_f0method").value,
  };
}

let autoSaveTimer = null;
function scheduleAutoSave(delay = 500) {
  clearTimeout(autoSaveTimer);
  autoSaveTimer = setTimeout(async () => {
    await postSettings(collectSettingsPayload());
    showStatus("Сохранено ✓");
  }, delay);
}

el("tts_volume").addEventListener("input", (e) => {
  el("tts_volume_value").textContent = `${e.target.value}%`;
  updateRangeFill(e.target);
  scheduleAutoSave();
});

el("overlay_scale").addEventListener("input", (e) => {
  el("overlay_scale_value").textContent = `${e.target.value}%`;
  updateRangeFill(e.target);
  scheduleAutoSave();
});

el("event_volume").addEventListener("input", (e) => {
  el("event_volume_value").textContent = `${e.target.value}%`;
  updateRangeFill(e.target);
  scheduleAutoSave();
});

el("rvc_pitch").addEventListener("input", (e) => {
  const v = parseInt(e.target.value, 10);
  el("rvc_pitch_value").textContent = v > 0 ? `+${v}` : `${v}`;
  updateRangeFill(e.target);
  scheduleAutoSave();
});

el("rvc_index_rate").addEventListener("input", (e) => {
  el("rvc_index_rate_value").textContent = parseFloat(e.target.value).toFixed(2);
  updateRangeFill(e.target);
  scheduleAutoSave();
});

el("rvc_protect").addEventListener("input", (e) => {
  el("rvc_protect_value").textContent = parseFloat(e.target.value).toFixed(2);
  updateRangeFill(e.target);
  scheduleAutoSave();
});

el("rvc-test-btn").addEventListener("click", async () => {
  await fetch("/api/test-event", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ kind: el("test-kind").value }),
  });
});

for (const id of ["tts_voice_ru", "tts_voice_en", "tts_rate"]) {
  el(id).addEventListener("input", () => scheduleAutoSave(700));
}

for (const id of ["tts_say_author", "rvc_enabled", "rvc_model", "rvc_scope", "rvc_f0method"]) {
  el(id).addEventListener("change", () => scheduleAutoSave(0));
}

el("mute-add").addEventListener("click", async () => {
  const input = el("mute-input");
  const name = input.value.trim();
  if (!name) return;
  await postSettings({ mute: name });
  input.value = "";
  loadSettings();
});

el("mute-input").addEventListener("keydown", (e) => {
  if (e.key === "Enter") el("mute-add").click();
});

document.querySelectorAll(".nav-btn").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".nav-btn").forEach((b) => b.classList.remove("active"));
    document.querySelectorAll(".tab").forEach((t) => t.classList.remove("active"));
    btn.classList.add("active");
    el(`tab-${btn.dataset.tab}`).classList.add("active");
  });
});

document.querySelectorAll(".preview-btn").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".preview-btn").forEach((b) => b.classList.remove("active"));
    btn.classList.add("active");
    el("preview-frame").src = btn.dataset.src;
  });
});

el("theme").addEventListener("change", () => scheduleAutoSave(0));

el("test-event-btn").addEventListener("click", async () => {
  await fetch("/api/test-event", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ kind: el("test-kind").value }),
  });
});

el("test-chat-btn").addEventListener("click", async () => {
  await fetch("/api/test-chat", { method: "POST" });
});

async function loadMediaGrid() {
  const res = await fetch("/api/media/status");
  const status = await res.json();
  const grid = el("media-grid");
  grid.innerHTML = "";

  for (const kind of Object.keys(EVENT_KIND_LABELS)) {
    const row = document.createElement("div");
    row.className = "media-row";

    const label = document.createElement("div");
    label.className = "kind-name";
    label.textContent = EVENT_KIND_LABELS[kind];
    row.appendChild(label);

    row.appendChild(mediaSlot(kind, "gif", MEDIA_EXT_LABELS.gif, status[kind]?.gif));
    row.appendChild(mediaSlot(kind, "mp3", MEDIA_EXT_LABELS.mp3, status[kind]?.mp3));

    grid.appendChild(row);
  }
}

function mediaSlot(kind, ext, label, exists) {
  const wrap = document.createElement("div");
  wrap.className = "media-slot";

  const dot = document.createElement("span");
  dot.className = `dot ${exists ? "on" : ""}`;
  wrap.appendChild(dot);

  const text = document.createElement("span");
  text.textContent = `${label}${exists ? " ✓" : ""}`;
  wrap.appendChild(text);

  const inputId = `file-${kind}-${ext}`;
  const fileLabel = document.createElement("label");
  fileLabel.className = "file-label";
  fileLabel.textContent = exists ? "Заменить" : "Загрузить";
  fileLabel.setAttribute("for", inputId);
  wrap.appendChild(fileLabel);

  const input = document.createElement("input");
  input.type = "file";
  input.id = inputId;
  input.accept = ext === "gif" ? ".gif" : ".mp3";
  input.addEventListener("change", async () => {
    const file = input.files[0];
    if (!file) return;
    await fetch(`/api/media/${kind}/${ext}`, { method: "POST", body: file });
    loadMediaGrid();
  });
  wrap.appendChild(input);

  if (exists) {
    const del = document.createElement("button");
    del.className = "delete-btn";
    del.textContent = "✕";
    del.addEventListener("click", async () => {
      await fetch(`/api/media/${kind}/${ext}`, { method: "DELETE" });
      loadMediaGrid();
    });
    wrap.appendChild(del);
  }

  return wrap;
}

async function loadCommands() {
  const res = await fetch("/api/commands");
  const data = await res.json();
  const list = el("commands-list");
  list.innerHTML = "";

  const commands = data.commands || [];
  if (!commands.length) {
    list.innerHTML = `<span class="commands-empty">Команд пока нет</span>`;
    return;
  }

  for (const cmd of commands) {
    const row = document.createElement("div");
    row.className = "command-row";

    const trigger = document.createElement("span");
    trigger.className = "cmd-trigger";
    trigger.textContent = cmd.trigger;
    row.appendChild(trigger);

    const response = document.createElement("span");
    response.className = "cmd-response";
    response.textContent = cmd.response;
    response.title = cmd.response;
    row.appendChild(response);

    const cooldown = document.createElement("span");
    cooldown.className = "cmd-cooldown";
    cooldown.textContent = `${cmd.cooldown}с`;
    row.appendChild(cooldown);

    const toggle = document.createElement("input");
    toggle.type = "checkbox";
    toggle.className = "mini-switch";
    toggle.checked = !!cmd.enabled;
    toggle.addEventListener("change", async () => {
      await fetch(`/api/commands/${encodeURIComponent(cmd.trigger)}/toggle`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enabled: toggle.checked }),
      });
    });
    row.appendChild(toggle);

    const del = document.createElement("button");
    del.className = "delete-btn";
    del.textContent = "✕";
    del.addEventListener("click", async () => {
      await fetch(`/api/commands/${encodeURIComponent(cmd.trigger)}`, { method: "DELETE" });
      loadCommands();
    });
    row.appendChild(del);

    list.appendChild(row);
  }
}

el("cmd-add").addEventListener("click", async () => {
  const trigger = el("cmd-trigger").value.trim();
  const response = el("cmd-response").value.trim();
  const cooldown = parseInt(el("cmd-cooldown").value, 10) || 0;
  if (!trigger || !response) return;

  await fetch("/api/commands", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ trigger, response, cooldown }),
  });

  el("cmd-trigger").value = "";
  el("cmd-response").value = "";
  el("cmd-cooldown").value = "15";
  loadCommands();
});

loadSettings();
loadMediaGrid();
loadRvcModels();
refreshRvcStatus();
setInterval(refreshRvcStatus, 5000);
loadCommands();
