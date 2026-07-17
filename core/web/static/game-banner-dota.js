// Dota 2 half of the shared game banner widget (core/web/faceit.html — the
// same OBS source also shows CS2/Faceit, see game-banner-cs2.js). Two data
// sources feed this: the free OpenDota API (match history, needs an account
// id) and Dota's own Game State Integration (live match, needs no account
// id or profile-privacy setting at all — see dota_gsi_state.hpp). Either
// one alone is enough to make the card show something.
window.GameBannerDota = (function () {
  let valid = false;
  // Pushed by core over /ws as "dota_live" (dota_gsi_state.hpp) — independent
  // of `valid` above, which only reflects the OpenDota profile poll.
  let liveState = null;

  function isValid() {
    return valid;
  }

  function isReady() {
    return valid || (liveState && liveState.active);
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

  // OpenDota's recentMatches is already most-recent-first — no reordering
  // needed here, unlike FACEIT's history in game-banner-cs2.js.
  function renderMatches(matches) {
    const matchesEl = document.getElementById("dmatches");
    matchesEl.innerHTML = "";
    matches.forEach((m) => {
      const d = document.createElement("div");
      d.className = "match " + (m.win ? "win" : "loss");

      if (m.hero_icon) {
        const badge = document.createElement("img");
        badge.className = "map-badge";
        badge.src = m.hero_icon;
        badge.title = m.hero_name || "";
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
        kd.textContent = m.kills + "/" + m.deaths + "/" + (m.assists >= 0 ? m.assists : 0);
        text.appendChild(kd);
      }

      d.appendChild(text);
      matchesEl.appendChild(d);
    });
  }

  function applyLiveStrip() {
    const icon = document.getElementById("dlive-icon");
    if (liveState.heroIcon) {
      icon.src = liveState.heroIcon;
      icon.style.display = "";
    } else {
      icon.style.display = "none";
    }
    document.getElementById("dlive-hero").textContent = liveState.heroName || "?";
    document.getElementById("dlive-kda").textContent =
      `K/D/A ${liveState.kills || 0}/${liveState.deaths || 0}/${liveState.assists || 0}`;
    document.getElementById("dlive-score").textContent =
      (liveState.radiantScore || 0) + ":" + (liveState.direScore || 0);
  }

  function renderLiveArea() {
    const live = document.getElementById("dlive");
    if (liveState && liveState.active) {
      live.classList.add("visible");
      applyLiveStrip();
    } else {
      live.classList.remove("visible");
    }
  }

  function onWsMessage(data) {
    if (data.type === "dota_live") {
      liveState = data;
      renderLiveArea();
    }
  }

  async function refresh(onValidChange) {
    try {
      const resp = await fetch("/api/dota/snapshot", { cache: "no-store" });
      const data = await resp.json();

      valid = !!data.valid;
      onValidChange();
      if (!data.valid) return;

      document.getElementById("dnickname").textContent = data.personaname || "";
      document.getElementById("drank").textContent = data.rank_label || "—";

      const avatar = document.getElementById("davatar");
      if (data.avatar) {
        avatar.src = data.avatar;
        avatar.classList.remove("hidden");
      } else {
        avatar.classList.add("hidden");
      }

      const statsEl = document.getElementById("dstats");
      statsEl.innerHTML = "";
      if ((data.matches || []).length) {
        statsEl.appendChild(statTile(Math.round(data.win_rate || 0) + "%", "Винрейт"));
        statsEl.appendChild(statTile((data.avg_kda || 0).toFixed(2), "Средний KDA"));
      }

      renderMatches(data.matches || []);
    } catch (e) {
      // Same cold-start tolerance as game-banner-cs2.js's refresh().
    }
  }

  return { isValid, isReady, onWsMessage, refresh };
})();
