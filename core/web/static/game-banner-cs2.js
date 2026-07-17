// CS2/Faceit half of the shared game banner widget (core/web/faceit.html —
// the same OBS source also shows Dota, see game-banner-dota.js). Split into
// its own file so each game's rendering logic lives and grows independently
// instead of one script handling both; the host page just wires the two
// modules to the WebSocket and its own poll timers.
window.GameBannerCS2 = (function () {
  const KNOWN_MAPS = ["mirage", "dust2", "inferno", "nuke", "ancient", "anubis", "vertigo", "train", "overpass", "cache"];

  // Pushed by core over the same /ws every overlay page connects to (see
  // overlay_server.hpp's "cs2_live" broadcasts) — this is CS2's own Game
  // State Integration, not a Faceit API poll, so it updates the instant the
  // game reports a change instead of waiting for the next 20s /refresh.
  let liveState = null;
  let lastMatches = [];
  let lastEloValue = null;
  let valid = false;

  function isValid() {
    return valid;
  }

  function mapIconUrl(rawMap) {
    if (!rawMap) return null;
    const key = rawMap.toLowerCase().replace(/^de_/, "");
    if (!KNOWN_MAPS.includes(key)) return null;
    return "/static/map-de_" + key + ".png";
  }

  function drawSparkline(points) {
    const svg = document.getElementById("sparkline");
    svg.innerHTML = "";
    if (!points || points.length < 2) return;

    const elos = points.map((p) => p.elo);
    const min = Math.min(...elos);
    const max = Math.max(...elos);
    const range = Math.max(1, max - min);
    const stepX = 100 / (points.length - 1);

    const coords = points.map((p, i) => {
      const x = i * stepX;
      const y = 28 - ((p.elo - min) / range) * 26;
      return x + "," + y;
    });

    const fillPoints = "0,30 " + coords.join(" ") + " 100,30";
    const fill = document.createElementNS("http://www.w3.org/2000/svg", "polygon");
    fill.setAttribute("points", fillPoints);
    fill.setAttribute("fill", "#ff5500");
    fill.setAttribute("opacity", "0.15");
    svg.appendChild(fill);

    const line = document.createElementNS("http://www.w3.org/2000/svg", "polyline");
    line.setAttribute("points", coords.join(" "));
    line.setAttribute("fill", "none");
    line.setAttribute("stroke", "#ff5500");
    line.setAttribute("stroke-width", "2.5");
    line.setAttribute("stroke-linecap", "round");
    line.setAttribute("stroke-linejoin", "round");
    svg.appendChild(line);
  }

  function statTile(value, label) {
    const el = document.createElement("div");
    el.className = "stat-tile";
    const v = document.createElement("div");
    v.className = "v";
    v.textContent = value;
    const l = document.createElement("div");
    l.className = "l";
    l.textContent = label;
    el.appendChild(v);
    el.appendChild(l);
    return el;
  }

  function applyEloToday(data) {
    const el = document.getElementById("elo-today");
    if (!data.has_elo_change_today) {
      el.classList.add("hidden");
      return;
    }
    const change = data.elo_change_today || 0;
    el.classList.remove("hidden", "up", "down", "flat");
    if (change > 0) {
      el.classList.add("up");
      el.textContent = "+" + change + " сегодня";
    } else if (change < 0) {
      el.classList.add("down");
      el.textContent = change + " сегодня";
    } else {
      el.classList.add("flat");
      el.textContent = "±0 сегодня";
    }
  }

  // FACEIT's history API returns oldest-of-the-batch first — reverse so the
  // most recent match reads leftmost, matching FACEIT's own site.
  function renderMatchHistory(matches) {
    const matchesEl = document.getElementById("matches");
    matchesEl.innerHTML = "";
    matches.slice().reverse().forEach((m) => {
      const d = document.createElement("div");
      d.className = "match " + (m.win ? "win" : "loss");

      const iconUrl = mapIconUrl(m.map);
      if (iconUrl) {
        const badge = document.createElement("img");
        badge.className = "map-badge";
        badge.src = iconUrl;
        badge.title = m.map || "";
        badge.onerror = () => badge.remove();
        d.appendChild(badge);
      }

      const text = document.createElement("div");
      text.className = "match-text";

      const wl = document.createElement("div");
      wl.className = "wl";
      wl.textContent = m.win ? "Победа" : "Поражение";
      text.appendChild(wl);

      if (m.kills >= 0 && m.deaths >= 0) {
        const kd = document.createElement("div");
        kd.className = "kd";
        kd.textContent = m.kills + "/" + m.deaths;
        text.appendChild(kd);
      }

      d.appendChild(text);
      matchesEl.appendChild(d);
    });
  }

  function applyLiveStrip() {
    document.getElementById("live-map").textContent = liveState.map || "—";
    document.getElementById("live-ct").textContent = liveState.ctScore || 0;
    document.getElementById("live-t").textContent = liveState.tScore || 0;
    document.getElementById("live-ct").classList.toggle("mine", liveState.playerTeam === "CT");
    document.getElementById("live-t").classList.toggle("mine", liveState.playerTeam === "T");
    document.getElementById("live-kda").textContent =
      `K/D/A ${liveState.kills || 0}/${liveState.deaths || 0}/${liveState.assists || 0}`;

    const banner = document.getElementById("bet-banner");
    if (liveState.bettingOpen) {
      banner.classList.add("visible");
      document.getElementById("bet-text").textContent =
        "Ставки открыты: !bet win / !bet lose — принимаются до раунда " + (liveState.lockRound || "?");
    } else {
      banner.classList.remove("visible");
    }
  }

  // The last-5-matches strip stays visible at all times — it just sits
  // above the live strip/bet banner instead of being replaced by them, so
  // viewers can still see match history while a game is in progress.
  function renderMatchesArea() {
    const live = document.getElementById("live");
    renderMatchHistory(lastMatches);
    if (liveState && liveState.active) {
      live.classList.add("visible");
      applyLiveStrip();
    } else {
      live.classList.remove("visible");
      document.getElementById("bet-banner").classList.remove("visible");
    }
  }

  function onWsMessage(data) {
    if (data.type === "cs2_live") {
      liveState = data;
      renderMatchesArea();
    }
  }

  async function refresh(onValidChange) {
    try {
      const resp = await fetch("/api/faceit/snapshot", { cache: "no-store" });
      const data = await resp.json();

      valid = !!data.valid;
      onValidChange();
      if (!data.valid) return;

      document.getElementById("nickname").textContent = data.nickname || "";
      const eloEl = document.getElementById("elo");
      eloEl.textContent = data.elo || "—";
      if (lastEloValue !== null && data.elo && data.elo !== lastEloValue) {
        eloEl.classList.remove("flash");
        void eloEl.offsetWidth; // restart the animation even if it's already mid-flash
        eloEl.classList.add("flash");
      }
      if (data.elo) lastEloValue = data.elo;
      document.getElementById("skill").textContent = "LVL " + (data.skill_level || "?");
      applyEloToday(data);

      const avatar = document.getElementById("avatar");
      if (data.avatar) {
        avatar.src = data.avatar;
        avatar.classList.remove("hidden");
      } else {
        avatar.classList.add("hidden");
      }

      drawSparkline(data.elo_history || []);

      const statsEl = document.getElementById("stats");
      statsEl.innerHTML = "";
      const lt = data.lifetime || {};
      if (lt.valid) {
        statsEl.appendChild(statTile(Math.round(lt.win_rate || 0) + "%", "Винрейт"));
        statsEl.appendChild(statTile((lt.avg_kd || 0).toFixed(2), "Средний K/D"));
        statsEl.appendChild(statTile(Math.round(lt.avg_headshots || 0) + "%", "HS%"));
        const streakLabel = lt.current_streak > 0 ? "Серия побед" : "Лучшая серия";
        const streakValue = lt.current_streak > 0 ? lt.current_streak : lt.longest_streak;
        statsEl.appendChild(statTile(streakValue || 0, streakLabel));
      }

      lastMatches = data.matches || [];
      renderMatchesArea();
    } catch (e) {
      // Overlay browser source with no core running yet — just keep the
      // widget hidden and retry on the next tick instead of throwing.
    }
  }

  return { isValid, onWsMessage, refresh };
})();
