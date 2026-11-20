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
  if (FILES[h]) location.replace(`plugins/${h}/`);
  else if (h === "donate") location.replace("donate/");
}
