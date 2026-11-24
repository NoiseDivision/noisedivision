// ------------------------------------------------------------------
// Settings. GITHUB_REPO is your GitHub "name/repository". The download
// buttons point at the files the build attaches to the newest Release.
// Donation links: paste the full address; "" shows a "not set up yet" note.
// ------------------------------------------------------------------
const GITHUB_REPO = "NoiseDivision/noisedivision";
const DONATE = {
  bmc: "https://buymeacoffee.com/noisedivision",
  paypal: "https://www.paypal.com/ncp/payment/PBA2MTR9843BS",
};
const FILES = {
  peakslayer:  { win: "PEAKSLAYER-Windows.zip",  mac: "PEAKSLAYER-macOS.pkg" },
  krush:       { win: "KRUSH-Windows.zip",       mac: "KRUSH-macOS.pkg" },
  masterfuse:  { win: "MASTERFUSE-Windows.zip",  mac: "MASTERFUSE-macOS.pkg" },
  spectrakill: { win: "SPECTRAKILL-Windows.zip", mac: "SPECTRAKILL-macOS.pkg" },
  dark:        { win: "DARK-Windows.zip",        mac: "DARK-macOS.pkg" },
  shrapnel:    { win: "SHRAPNEL-Windows.zip",    mac: "SHRAPNEL-macOS.pkg" },
  recoil:      { win: "RECOIL-Windows.zip",      mac: "RECOIL-macOS.pkg" },
  pyre:        { win: "PYRE-Windows.zip",        mac: "PYRE-macOS.pkg" },
  flatline:    { win: "FLATLINE-Windows.zip",    mac: "FLATLINE-macOS.pkg" },
  // every plugin in one download
  suite:       { win: "NoiseDivision-Suite-Windows.zip", mac: "NoiseDivision-Suite-macOS.pkg" },
};

// accept "name/repo" or a pasted address like "https://github.com/name/repo/"
const REPO = GITHUB_REPO.trim().replace(/^https?:\/\/(www\.)?github\.com\//i, "").replace(/\.git$/i, "").replace(/\/+$/, "");

// ---- download buttons
for (const a of document.querySelectorAll("[data-download]")) {
  const key = a.dataset.download, file = FILES[key] && FILES[key][a.dataset.os];
  if (REPO && file) { a.href = `https://github.com/${REPO}/releases/latest/download/${file}`; continue; }
  a.addEventListener("click", (e) => {
    e.preventDefault();
    const note = document.querySelector(`[data-note="${key}"]`);
    if (note) { note.dataset.state = "missing"; note.textContent = "The downloads aren't up yet. Check back soon."; }
  });
}

// ---- donation buttons
const donateMsg = document.getElementById("donate-msg");
for (const a of document.querySelectorAll("[data-donate]")) {
  const url = DONATE[a.dataset.donate];
  if (url) { a.href = url; a.target = "_blank"; a.rel = "noopener"; continue; }
  a.addEventListener("click", (e) => {
    e.preventDefault();
    if (donateMsg) donateMsg.textContent = a.querySelector("strong").textContent + " isn't set up yet. Thanks for wanting to help!";
  });
}

// ---- Plugins dropdown: click or tap to open, hover on a mouse, Escape or click outside to close
for (const dd of document.querySelectorAll(".dd")) {
  const btn = dd.querySelector(".dd-btn"), panel = dd.querySelector(".dd-panel");
  let timer = 0, hoverAt = 0;
  const set = (open) => { clearTimeout(timer); dd.classList.toggle("open", open); btn.setAttribute("aria-expanded", String(open)); };
  // a mouse opens the menu on hover just before the click lands, so a click right after that keeps it open
  btn.addEventListener("click", () => set(!dd.classList.contains("open") || Date.now() - hoverAt < 600));
  if (matchMedia("(hover: hover) and (pointer: fine)").matches) {
    dd.addEventListener("mouseenter", () => { if (!dd.classList.contains("open")) hoverAt = Date.now(); set(true); });
    dd.addEventListener("mouseleave", () => { timer = setTimeout(() => set(false), 220); });
  }
  document.addEventListener("click", (e) => { if (!dd.contains(e.target)) set(false); });
  dd.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && dd.classList.contains("open")) { set(false); btn.focus(); }
    if (e.key === "ArrowDown" && dd.classList.contains("open")) {
      const links = [...panel.querySelectorAll("a")], i = links.indexOf(document.activeElement);
      links[Math.min(i + 1, links.length - 1)].focus(); e.preventDefault();
    }
    if (e.key === "ArrowUp" && dd.classList.contains("open")) {
      const links = [...panel.querySelectorAll("a")], i = links.indexOf(document.activeElement);
      if (i > 0) links[i - 1].focus(); else btn.focus();
      e.preventDefault();
    }
  });
  dd.addEventListener("focusout", (e) => { if (!dd.contains(e.relatedTarget)) set(false); });
}

// ---- copy the e-mail address (contact page)
for (const b of document.querySelectorAll("[data-copy]")) {
  b.addEventListener("click", async () => {
    const out = document.getElementById("copy-msg");
    try { await navigator.clipboard.writeText(b.dataset.copy); if (out) out.textContent = "Copied to the clipboard."; }
    catch { if (out) out.textContent = "Couldn't copy. Select the address and copy it by hand."; }
  });
}

// ---- plugin page: highlight the section you are reading in the sticky contents bar
const toc = document.querySelector(".toc");
if (toc && "IntersectionObserver" in window) {
  const links = new Map([...toc.querySelectorAll("a")].map((a) => [a.getAttribute("href").slice(1), a]));
  const seen = new Set();
  const io = new IntersectionObserver((entries) => {
    for (const en of entries) en.isIntersecting ? seen.add(en.target.id) : seen.delete(en.target.id);
    const first = [...links.keys()].find((id) => seen.has(id));
    for (const [id, a] of links) a.classList.toggle("on", id === first);
  }, { rootMargin: "-130px 0px -55% 0px" });
  for (const id of links.keys()) { const s = document.getElementById(id); if (s) io.observe(s); }
}

// ---- old one-page links (noisedivision.net/#krush) open the plugin's own page
if (document.body.dataset.page === "home") {
  const h = location.hash.slice(1).toLowerCase();
  if (FILES[h] && h !== "suite") location.replace(`plugins/${h}/`);
  else if (h === "donate") location.replace("donate/");
}

// ---- before / after player (plugin pages)
// Both versions play in sync; the switch crossfades between them in 15 ms, so you hear the
// difference at the same point in the music. Audio loads only when you press play.
for (const ab of document.querySelectorAll(".ab")) {
  const data = JSON.parse(ab.querySelector(".ab-data").textContent);
  const playBtn = ab.querySelector(".ab-play"), wave = ab.querySelector(".ab-wave"), canvas = wave.querySelector("canvas");
  const tabs = [...ab.querySelectorAll("[data-clip]")], sides = [...ab.querySelectorAll("[data-side]")].filter((b) => b.tagName === "BUTTON");
  const settingEl = ab.querySelector(".ab-setting"), levelEl = ab.querySelector(".ab-level"), matchBox = ab.querySelector(".ab-match input");
  const accent = getComputedStyle(document.documentElement).getPropertyValue("--accent").trim() || "#ff3038";
  let ctx = null, gB = null, gA = null, srcs = [], buffers = {}, clip = data.clips[0], side = "after";
  let playing = false, startAt = 0, offset = 0, raf = 0, token = 0;

  const url = (which, id) => `${data.base}${which}/${id}.mp3`;
  const duration = () => (buffers[url("before", clip.id)] || {}).duration || 0;
  const pos = () => { const d = duration(); if (!d) return 0; const t = playing ? offset + ctx.currentTime - startAt : offset; return ((t % d) + d) % d; };
  const afterGain = () => (data.keepLevel && matchBox && matchBox.checked ? Math.pow(10, -clip.louder / 20) : 1);

  function levelText() {
    if (!data.keepLevel) return "";
    if (matchBox && matchBox.checked) return `After turned down ${Math.abs(clip.louder).toFixed(1)} dB to match`;
    const v = clip.louder;
    return Math.abs(v) < 0.3 ? "Same loudness" : `After is ${Math.abs(v).toFixed(1)} dB ${v > 0 ? "louder" : "quieter"}`;
  }
  function showClip() {
    settingEl.textContent = clip.setting;
    levelEl.textContent = levelText();
    tabs.forEach((t) => t.setAttribute("aria-selected", String(t.dataset.clip === clip.id)));
    draw();
  }
  function applyGains(fade = 0.015) {
    if (!ctx) return;
    const t = ctx.currentTime;
    for (const [g, v] of [[gB, side === "before" ? 1 : 0], [gA, side === "after" ? afterGain() : 0]]) {
      g.gain.cancelScheduledValues(t); g.gain.setValueAtTime(g.gain.value, t); g.gain.linearRampToValueAtTime(v, t + fade);
    }
  }
  function setSide(s) {
    side = s; ab.dataset.side = s;
    sides.forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.side === s)));
    applyGains(); draw();
  }
  async function load(id) {
    const need = [url("before", id), url(data.plugin, id)].filter((u) => !buffers[u]);
    await Promise.all(need.map(async (u) => {
      const r = await fetch(u); if (!r.ok) throw new Error(u);
      buffers[u] = await ctx.decodeAudioData(await r.arrayBuffer());
    }));
  }
  function stopSources() { srcs.forEach((s) => { try { s.stop(); } catch {} s.disconnect(); }); srcs = []; }
  function startSources(at) {
    stopSources();
    const t = ctx.currentTime + 0.03;
    for (const [which, g] of [["before", gB], [data.plugin, gA]]) {
      const s = ctx.createBufferSource(); s.buffer = buffers[url(which, clip.id)]; s.loop = true;
      s.connect(g); s.start(t, at); srcs.push(s);
    }
    startAt = t; offset = at;
  }
  async function play() {
    if (!ctx) {
      try { if (navigator.audioSession) navigator.audioSession.type = "playback"; } catch {}   // iPhone: play even with the ring switch on silent
      ctx = new (window.AudioContext || window.webkitAudioContext)();
      gB = ctx.createGain(); gA = ctx.createGain(); gB.connect(ctx.destination); gA.connect(ctx.destination);
      gB.gain.value = 0; gA.gain.value = 0;
    }
    await ctx.resume();
    const my = ++token;
    ab.classList.add("loading");
    try { await load(clip.id); } catch { ab.classList.remove("loading"); levelEl.innerHTML = '<span class="ab-err">Couldn\'t load the audio.</span>'; return; }
    ab.classList.remove("loading");
    if (my !== token) return;
    // stop any other player on the page
    document.querySelectorAll(".ab.playing").forEach((o) => o !== ab && o.querySelector(".ab-play").click());
    startSources(pos());
    playing = true; ab.classList.add("playing"); playBtn.setAttribute("aria-label", "Pause");
    applyGains(0.01); tick();
  }
  function pause() {
    if (!playing) return;
    offset = pos(); playing = false; token++;
    applyGains(0.01); gB.gain.linearRampToValueAtTime(0, ctx.currentTime + 0.02); gA.gain.linearRampToValueAtTime(0, ctx.currentTime + 0.02);
    setTimeout(() => { if (!playing) stopSources(); }, 40);
    ab.classList.remove("playing"); playBtn.setAttribute("aria-label", "Play");
    cancelAnimationFrame(raf); draw();
  }
  function tick() { draw(); if (playing) raf = requestAnimationFrame(tick); }

  function draw() {
    const dpr = window.devicePixelRatio || 1, w = canvas.clientWidth, h = canvas.clientHeight;
    if (!w) return;
    if (canvas.width !== Math.round(w * dpr)) { canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr); }
    const g = canvas.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
    const d = duration(), p = d ? pos() / d : 0, mid = h / 2;
    const top = Math.max(1, ...clip.wb, ...clip.wa);      // fill the height with the louder of the two
    const bars = (src, color, alpha, played) => {
      const n = Math.min(src.length, Math.floor(w / 3)), bw = w / n, vals = [];
      for (let i = 0; i < n; i++) {                           // fewer, wider bars on a narrow screen
        let m = 0; for (let j = Math.floor(i * src.length / n); j < Math.floor((i + 1) * src.length / n); j++) m = Math.max(m, src[j]);
        vals.push(m);
      }
      for (let i = 0; i < n; i++) {
        const x = i * bw, a = (played && x / w < p) ? 1 : alpha, v = Math.max(1, (vals[i] / top) * (h / 2 - 6));
        g.globalAlpha = a; g.fillStyle = color; g.fillRect(x + 0.5, mid - v, Math.max(1, bw - 1.5), v * 2);
      }
      g.globalAlpha = 1;
    };
    const after = side === "after";
    bars(after ? clip.wb : clip.wa, "#5a5d65", 0.35, false);                 // the other version, faint, behind
    bars(after ? clip.wa : clip.wb, after ? accent : "#c6c8cc", 0.45, true);  // the one you hear
    if (d) { g.fillStyle = "#fff"; g.fillRect(Math.round(p * w), 0, 2, h); }
    wave.setAttribute("aria-valuenow", String(Math.round(p * 100)));
  }

  playBtn.addEventListener("click", () => (playing ? pause() : play()));
  sides.forEach((b) => b.addEventListener("click", () => setSide(b.dataset.side)));
  tabs.forEach((t) => t.addEventListener("click", async () => {
    if (t.dataset.clip === clip.id) return;
    const was = playing; if (was) pause();
    clip = data.clips.find((c) => c.id === t.dataset.clip); offset = 0; showClip();
    if (was) play();
  }));
  if (matchBox) matchBox.addEventListener("change", () => { levelEl.textContent = levelText(); applyGains(0.03); });
  const seek = (frac) => { const d = duration(); offset = Math.min(Math.max(frac, 0), 0.999) * (d || 0); if (playing) startSources(offset); draw(); };
  wave.addEventListener("click", (e) => { const r = wave.getBoundingClientRect(); if (duration()) seek((e.clientX - r.left) / r.width); else play(); });
  wave.addEventListener("keydown", (e) => {
    const d = duration(); if (!d) return;
    if (e.key === "ArrowRight" || e.key === "ArrowLeft") { seek(pos() / d + (e.key === "ArrowRight" ? 0.05 : -0.05)); e.preventDefault(); }
  });
  document.addEventListener("keydown", (e) => {
    if (e.target.closest("input, textarea, select") || e.ctrlKey || e.metaKey || e.altKey) return;
    if (e.key === "b" || e.key === "B") setSide(side === "after" ? "before" : "after");
  });
  window.addEventListener("resize", draw);
  showClip();
}
