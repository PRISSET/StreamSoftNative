const PLATFORM_BADGE = { youtube: "🔴", twitch: "💜" };

const chatFeed = document.getElementById("chat-feed");
const eventLayer = document.getElementById("event-layer");
const pollWidget = document.getElementById("poll-widget");
const MAX_BUBBLES = 8;

const params = new URLSearchParams(location.search);
const volumeOverride = params.get("volume");
let eventVolume = volumeOverride !== null ? Math.min(1, Math.max(0, parseFloat(volumeOverride) || 0)) : 1;

function applyConfig(cfg) {
  document.body.className = `theme-${cfg.theme || "minimal"}`;
  if (volumeOverride === null && typeof cfg.eventVolume === "number") {
    eventVolume = Math.min(1, Math.max(0, cfg.eventVolume));
  }
  if (typeof cfg.chatScale === "number") {
    document.documentElement.style.setProperty("--ov-zoom-chat", cfg.chatScale);
  }
  if (typeof cfg.alertScale === "number") {
    document.documentElement.style.setProperty("--ov-zoom-alert", cfg.alertScale);
  }
}

function escapeHtml(str) {
  const div = document.createElement("div");
  div.textContent = str ?? "";
  return div.innerHTML;
}

function addChatBubble(msg) {
  if (!chatFeed) return;

  const el = document.createElement("div");
  el.className = `bubble ${msg.platform}`;
  const badge = PLATFORM_BADGE[msg.platform] || "💬";
  el.innerHTML = `<span class="badge">${badge}</span><span><span class="author">${escapeHtml(msg.author)}:</span>${escapeHtml(msg.text)}</span>`;
  chatFeed.insertBefore(el, chatFeed.firstChild);

  while (chatFeed.children.length > MAX_BUBBLES) {
    chatFeed.removeChild(chatFeed.lastChild);
  }

  setTimeout(() => {
    el.classList.add("fade-out");
    setTimeout(() => el.remove(), 550);
  }, 9000);
}

function playEventSound(kind) {
  const audio = new Audio(`/media/${kind}.mp3`);
  audio.volume = eventVolume;
  audio.addEventListener("error", () => {});
  audio.play().catch(() => {});
}

function addEventAlert(evt) {
  if (!eventLayer) return;

  const el = document.createElement("div");
  el.className = "event-card";

  const img = document.createElement("img");
  img.src = `/media/${evt.kind}.gif`;
  img.onerror = () => img.remove();
  el.appendChild(img);

  const nick = document.createElement("div");
  nick.className = "nickname";
  nick.textContent = evt.user;
  el.appendChild(nick);

  eventLayer.appendChild(el);
  playEventSound(evt.kind);

  setTimeout(() => el.remove(), 5200);
}

function renderPoll(poll) {
  if (!pollWidget) return;

  if (!poll.active) {
    pollWidget.classList.remove("visible");
    pollWidget.innerHTML = "";
    return;
  }

  pollWidget.classList.add("visible");
  const total = poll.total_votes || 0;
  const maxVotes = Math.max(1, ...poll.options.map((o) => o.votes));

  const rows = poll.options
    .map((o) => {
      const pct = total > 0 ? Math.round((o.votes / total) * 100) : 0;
      const barPct = Math.round((o.votes / maxVotes) * 100);
      return `<div class="poll-row">
        <div class="poll-row-label"><span>${escapeHtml(o.text)}</span><span>${pct}% (${o.votes})</span></div>
        <div class="poll-bar-track"><div class="poll-bar-fill" style="width:${barPct}%"></div></div>
      </div>`;
    })
    .join("");

  pollWidget.innerHTML = `<div class="poll-question">${escapeHtml(poll.question)}</div>${rows}`;
}

function connect() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    if (data.type === "chat") addChatBubble(data);
    else if (data.type === "event") addEventAlert(data);
    else if (data.type === "config") applyConfig(data);
    else if (data.type === "poll") renderPoll(data);
  };
  ws.onclose = () => setTimeout(connect, 2000);
  ws.onerror = () => ws.close();
}

connect();
