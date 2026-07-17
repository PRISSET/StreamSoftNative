// CS2/Faceit half of the shared game banner widget (core/web/faceit.html —
// the same OBS source also shows Dota, see game-banner-dota.js). Split into
// its own file so each game's rendering logic lives and grows independently
// instead of one script handling both; the host page just wires the two
// modules to the WebSocket and its own poll timers.
window.GameBannerCS2 = (function () {
  const KNOWN_MAPS = ["mirage", "dust2", "inferno", "nuke", "ancient", "anubis", "vertigo", "train", "overpass", "cache"];

  // Official FACEIT CS2 skill-level ELO brackets — public, unchanged since
  // the CS2 migration. Used purely client-side to color the level-ring
  // badge and compute the progress-to-next-level bar; FACEIT's API doesn't
  // expose "points to next level" directly, but it's just arithmetic once
  // you know the brackets, so no extra API call is needed.
  const LEVEL_BANDS = [1, 501, 751, 901, 1051, 1201, 1351, 1531, 1751, 2001];
  // Colors read straight off FACEIT's own level-icon SVGs (esgame.net's
  // compact-widget-generator serves the same official assets at
  // /assets/levels/{1..10}.svg — see core/web/static/faceit-assets/levels/),
  // not eyeballed off a screenshot: grey, green x2, yellow x4, orange x2, red.
  const LEVEL_COLORS = ["#eeeeee", "#1ce400", "#1ce400", "#ffc800", "#ffc800", "#ffc800", "#ffc800", "#ff6309", "#ff6309", "#fe1f00"];

  function levelColor(level) {
    const idx = Math.min(Math.max((level || 1) - 1, 0), LEVEL_COLORS.length - 1);
    return LEVEL_COLORS[idx];
  }

  const RING_CIRCUMFERENCE = 2 * Math.PI * 17; // matches the r=17 circle in faceit.html's SVG

  function applyLevelProgress(elo, level) {
    const track = document.getElementById("level-progress-fill");
    const label = document.getElementById("level-progress-label");
    const ring = document.getElementById("skill-badge-ring");
    const lvl = Math.min(Math.max(level || 1, 1), 10);

    let pct = 100;
    if (lvl < 10) {
      const bandStart = LEVEL_BANDS[lvl - 1];
      const bandEnd = LEVEL_BANDS[lvl] - 1;
      const span = Math.max(1, bandEnd - bandStart);
      pct = Math.min(100, Math.max(0, ((elo - bandStart) / span) * 100));
    }

    track.style.width = pct.toFixed(0) + "%";
    ring.style.strokeDashoffset = (RING_CIRCUMFERENCE * (1 - pct / 100)).toFixed(1);
    label.textContent = lvl >= 10 ? "Максимальный уровень" : "+" + Math.max(0, LEVEL_BANDS[lvl] - elo) + " до " + (lvl + 1) + " уровня";
  }

  // Pushed by core over the same /ws every overlay page connects to (see
  // overlay_server.hpp's "cs2_live" broadcasts) — this is CS2's own Game
  // State Integration, not a Faceit API poll, so it updates the instant the
  // game reports a change instead of waiting for the next 20s /refresh.
  let liveState = null;
  let lastMatches = [];
  let lastEloValue = null;
  let lastSkillLevel = null;
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

    const accent = getComputedStyle(document.body).getPropertyValue("--accent").trim() || "#ff5500";

    const fillPoints = "0,30 " + coords.join(" ") + " 100,30";
    const fill = document.createElementNS("http://www.w3.org/2000/svg", "polygon");
    fill.setAttribute("points", fillPoints);
    fill.setAttribute("fill", accent);
    fill.setAttribute("opacity", "0.15");
    svg.appendChild(fill);

    const line = document.createElementNS("http://www.w3.org/2000/svg", "polyline");
    line.setAttribute("points", coords.join(" "));
    line.setAttribute("fill", "none");
    line.setAttribute("stroke", accent);
    line.setAttribute("stroke-width", "2.5");
    line.setAttribute("stroke-linecap", "round");
    line.setAttribute("stroke-linejoin", "round");
    svg.appendChild(line);
  }

  // esgame.net's own region icon set only covers eu/na/sa/oce/sea — FACEIT's
  // API returns the ladder region as "EU"/"US"/etc, and "US" (North America)
  // is the one name mismatch against what we actually downloaded.
  const REGION_ICON_ALIAS = { us: "na" };

  // The level badge (see #skill-badge-icon in faceit.html) uses the real
  // FACEIT level-icon SVGs (core/web/static/faceit-assets/levels/, pulled
  // from esgame.net's compact-widget-generator, which itself serves
  // FACEIT's own official assets), recolored via a CSS mask to match
  // whichever color palette is active — Milk's dark ink or Faceit Style's
  // white, both exact query-param values off the reference URLs.
  // Milk/White Cyber recolor the level icon flat to match their ink color
  // (a CSS mask over the SVG). Faceit Style shows the icon's real, original
  // per-level colors instead (gold/orange/red etc, straight off the SVG,
  // unmasked) — no entry here means "don't mask it".
  const ICON_MASK_COLOR = { milk: "#1a2a3e", whitecyber: "#0a0a0a" };

  function applySkillIcon(level) {
    const el = document.getElementById("skill-badge-icon");
    if (!el) return;
    const lvl = Math.min(Math.max(level || 1, 1), 10);
    const url = "/static/faceit-assets/levels/" + lvl + ".svg";
    const theme = document.body.dataset.theme || "milk";
    const maskColor = ICON_MASK_COLOR[theme];
    if (maskColor) {
      el.style.backgroundImage = "none";
      el.style.webkitMaskImage = "url(" + url + ")";
      el.style.maskImage = "url(" + url + ")";
      el.style.webkitMaskSize = "contain";
      el.style.maskSize = "contain";
      el.style.webkitMaskRepeat = "no-repeat";
      el.style.maskRepeat = "no-repeat";
      el.style.backgroundColor = maskColor;
    } else {
      el.style.webkitMaskImage = "none";
      el.style.maskImage = "none";
      el.style.backgroundColor = "transparent";
      el.style.backgroundImage = "url(" + url + ")";
      el.style.backgroundSize = "contain";
      el.style.backgroundRepeat = "no-repeat";
    }
  }

  // Region/country ladder position — see fetch_rankings() in
  // faceit_client.hpp. Both halves hide independently (rather than the
  // whole row) since a brand-new account can be ranked in one and not the
  // other yet.
  function applyRankRow(data) {
    const regionBox = document.getElementById("region-rank-box");
    const countryBox = document.getElementById("country-rank-box");
    if (!regionBox || !countryBox) return;

    if (data.region && data.region_rank) {
      const icon = document.getElementById("region-icon");
      const key = REGION_ICON_ALIAS[data.region] || data.region;
      icon.src = "/static/faceit-assets/regions/" + key + ".png";
      icon.onerror = () => { icon.style.visibility = "hidden"; };
      icon.style.visibility = "visible";
      document.getElementById("region-rank-num").textContent = data.region_rank;
      regionBox.classList.remove("hidden");
    } else {
      regionBox.classList.add("hidden");
    }

    if (data.country && data.country_rank) {
      const flag = document.getElementById("country-flag");
      flag.src = "/static/faceit-assets/flags/" + data.country + ".svg";
      flag.onerror = () => { flag.style.visibility = "hidden"; };
      flag.style.visibility = "visible";
      document.getElementById("country-rank-num").textContent = data.country_rank;
      countryBox.classList.remove("hidden");
    } else {
      countryBox.classList.add("hidden");
    }
  }

  function statTile(label) {
    const el = document.createElement("div");
    el.className = "stat-tile";
    const v = document.createElement("div");
    v.className = "v";
    const l = document.createElement("div");
    l.className = "l";
    l.textContent = label;
    el.appendChild(v);
    el.appendChild(l);
    el._valueEl = v;
    return el;
  }

  // Smoothly counts a displayed number from its current value to a new one
  // instead of snapping straight to it — covers the ELO number and every
  // stat tile, on every theme (it's plain JS, not CSS, so nothing about the
  // theme choice affects it). Keeps whatever non-numeric prefix/suffix the
  // new text has (e.g. "45%", "1.13") and matches its decimal precision;
  // falls back to a plain instant set for the very first render or for
  // text that isn't actually a number (e.g. "—").
  function animateNumberText(el, newText) {
    const newMatch = /^(-?[\d.]+)(.*)$/.exec(newText);
    const oldMatch = /^(-?[\d.]+)(.*)$/.exec(el.textContent || "");
    if (!newMatch || !oldMatch || el.textContent === "") {
      el.textContent = newText;
      return;
    }
    const from = parseFloat(oldMatch[1]);
    const to = parseFloat(newMatch[1]);
    const suffix = newMatch[2];
    const decimals = (newMatch[1].split(".")[1] || "").length;
    if (!isFinite(from) || !isFinite(to) || from === to) {
      el.textContent = newText;
      return;
    }
    if (el._numAnim) cancelAnimationFrame(el._numAnim);
    const duration = 650;
    const start = performance.now();
    const step = (now) => {
      const t = Math.min(1, (now - start) / duration);
      const eased = 1 - Math.pow(1 - t, 3); // ease-out cubic
      const value = from + (to - from) * eased;
      el.textContent = value.toFixed(decimals) + suffix;
      if (t < 1) {
        el._numAnim = requestAnimationFrame(step);
      } else {
        el._numAnim = null;
        el.textContent = newText;
      }
    };
    el._numAnim = requestAnimationFrame(step);
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
      wl.textContent = m.win ? "W" : "L";
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
      if (data.elo) {
        animateNumberText(eloEl, String(data.elo));
      } else {
        eloEl.textContent = "—";
      }
      if (lastEloValue !== null && data.elo && data.elo !== lastEloValue) {
        eloEl.classList.remove("flash");
        void eloEl.offsetWidth; // restart the animation even if it's already mid-flash
        eloEl.classList.add("flash");
      }
      if (data.elo) lastEloValue = data.elo;
      applyEloToday(data);

      const badge = document.getElementById("skill-badge");
      const badgeIcon = document.getElementById("skill-badge-icon");
      if (data.skill_level) {
        // Crossfades the icon/ring instead of snapping straight to the new
        // level's asset when it actually changes (a real level-up/down),
        // so it reads as a transition rather than a jump-cut — same idea
        // as the number count above, same "every theme" scope.
        const leveledUp = lastSkillLevel !== null && data.skill_level !== lastSkillLevel;
        const applyLevelVisuals = () => {
          document.getElementById("skill-badge-num").textContent = data.skill_level;
          badge.style.setProperty("--level-color", levelColor(data.skill_level));
          applySkillIcon(data.skill_level);
        };
        if (leveledUp) {
          badge.classList.add("level-fade");
          badgeIcon.classList.add("level-fade");
          setTimeout(() => {
            applyLevelVisuals();
            badge.classList.remove("level-fade");
            badgeIcon.classList.remove("level-fade");
          }, 220);
        } else {
          applyLevelVisuals();
        }
        lastSkillLevel = data.skill_level;
        badge.classList.remove("hidden");
      } else {
        badge.classList.add("hidden");
      }
      if (data.elo) applyLevelProgress(data.elo, data.skill_level);
      applyRankRow(data);

      document.getElementById("today-wins").textContent = data.wins_today || 0;
      document.getElementById("today-losses").textContent = data.losses_today || 0;

      const avatar = document.getElementById("avatar");
      const avatarUrl = window.StreamSoftCustomAvatarUrl || data.avatar;
      if (avatarUrl) {
        avatar.src = avatarUrl;
        avatar.classList.remove("hidden");
      } else {
        avatar.classList.add("hidden");
      }

      drawSparkline(data.elo_history || []);

      // ADR / AVG (kills) / K/D / K/R — esgame.net's Compact widget layout,
      // the only one this card has now. ADR and K/R aren't in FACEIT's
      // lifetime stats endpoint (only per-match), so those two are an
      // average over the 5 matches already fetched for the strip below —
      // "recent form", not a true lifetime number the way K/D still is.
      // The 4 tiles are created once and reused (not torn down and rebuilt
      // every refresh) specifically so animateNumberText has a stable
      // element to count from — a fresh node has no "previous value" to
      // animate away from.
      const lt = data.lifetime || {};
      const statsEl = document.getElementById("stats");
      if (!statsEl._tiles) {
        statsEl._tiles = ["ADR", "AVG", "K/D", "K/R"].map((label) => {
          const tile = statTile(label);
          statsEl.appendChild(tile);
          return tile;
        });
      }
      const statValues = [
        Math.round(data.avg_adr_recent || 0).toString(),
        Math.round(data.avg_kills_recent || 0).toString(),
        (lt.avg_kd || 0).toFixed(2),
        (data.avg_kr_recent || 0).toFixed(2),
      ];
      statsEl._tiles.forEach((tile, i) => animateNumberText(tile._valueEl, statValues[i]));

      lastMatches = data.matches || [];
      renderMatchesArea();
    } catch (e) {
      // Overlay browser source with no core running yet — just keep the
      // widget hidden and retry on the next tick instead of throwing.
    }
  }

  return { isValid, onWsMessage, refresh };
})();
