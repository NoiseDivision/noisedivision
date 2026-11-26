#!/usr/bin/env python3
"""Builds the Noise Division website into docs/.

    python scripts/site/build.py

Writes docs/index.html, docs/plugins/<name>/index.html, docs/contact/, docs/donate/, docs/404.html
and docs/sitemap.xml from the text in content.py. The look lives in docs/assets/site.css and the
settings (GitHub repository, donation links, download file names) in docs/assets/site.js.
Run manuals.py afterwards to rebuild the PDF manuals, and og.py for the social preview images.
"""
import datetime
import hashlib
import html
import json
import pathlib

from content import (SITE, PLUGINS, INSTALL_WIN, INSTALL_MAC, REQUIREMENTS, DAW_RESCAN, TROUBLE_COMMON, for_plugin)

# before/after audio, written by tools/render/demos.py (pages build without it, just with no player)
_demos_file = pathlib.Path(__file__).resolve().parent / "demos.json"
DEMOS = json.loads(_demos_file.read_text()) if _demos_file.exists() else {"clips": [], "plugins": {}}

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
URL = SITE["url"].rstrip("/")
TODAY = datetime.date.today().isoformat()


def esc(s):
    return html.escape(s, quote=True)


def strip_tags(s):
    import re
    return html.unescape(re.sub(r"<[^>]+>", "", s))


def asset_version(name):
    return hashlib.sha1((DOCS / "assets" / name).read_bytes()).hexdigest()[:8]


CSS_V, JS_V = asset_version("site.css"), asset_version("site.js")


def manual_file(p):
    return f"{p['name']}-User-Manual.pdf"


# ------------------------------------------------------------------ icons
ICON_WIN = '<svg width="15" height="15" viewBox="0 0 16 16" aria-hidden="true"><path d="M1 2.6 6.6 1.8v5.5H1zM7.4 1.7 15 .6v6.7H7.4zM1 8.1h5.6v5.5L1 12.8zM7.4 8.1H15v6.8l-7.6-1.1z" fill="currentColor"/></svg>'
ICON_MAC = '<svg width="14" height="16" viewBox="0 0 14 16" aria-hidden="true"><path d="M11.6 8.5c0-2 1.6-2.9 1.7-3-1-1.4-2.4-1.6-2.9-1.6-1.2-.1-2.4.7-3 .7s-1.6-.7-2.6-.7C3.4 4 2.1 4.8 1.4 6c-1.5 2.6-.4 6.4 1.1 8.5.7 1 1.5 2.2 2.6 2.1 1-.1 1.4-.7 2.7-.7s1.6.7 2.7.6c1.1 0 1.8-1 2.5-2.1.8-1.2 1.1-2.3 1.1-2.4 0 0-2.5-.9-2.5-3.5zM9.6 2.5c.6-.7 1-1.6.9-2.5-.8 0-1.8.5-2.4 1.2-.5.6-1 1.6-.9 2.5.9.1 1.8-.5 2.4-1.2z" fill="currentColor"/></svg>'
ICON_PDF = '<svg width="16" height="18" viewBox="0 0 16 18" aria-hidden="true"><path d="M2 1h8l4 4v12H2z" fill="none" stroke="currentColor" stroke-width="1.5"/><path d="M10 1v4h4" fill="none" stroke="currentColor" stroke-width="1.5"/><path d="M8 7.5v6m-2.5-2.5L8 13.5l2.5-2.5" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>'
ICON_DOWN = '<svg width="10" height="7" viewBox="0 0 10 7" aria-hidden="true"><path d="M1 1.5 5 5.5l4-4" fill="none" stroke="currentColor" stroke-width="1.6"/></svg>'
LOGO = '<svg width="34" height="14" viewBox="0 0 34 14" aria-hidden="true"><line x1="0" y1="7" x2="9" y2="7" stroke="#777a82" stroke-width="1.6"/><rect x="9" y="1.5" width="16" height="11" fill="none" stroke="#c6c8cc" stroke-width="1.6"/><line x1="9" y1="7" x2="25" y2="7" stroke="#ff3038" stroke-width="1.8"/><line x1="25" y1="7" x2="34" y2="7" stroke="#777a82" stroke-width="1.6"/></svg>'
FAVICON = "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Crect width='32' height='32' rx='6' fill='%23111214'/%3E%3Crect x='8' y='11' width='16' height='10' fill='none' stroke='%23c6c8cc' stroke-width='2'/%3E%3Cline x1='2' y1='16' x2='30' y2='16' stroke='%23ff3038' stroke-width='2.4'/%3E%3C/svg%3E"


# ------------------------------------------------------------------ page frame
def head(R, *, title, desc, path, image, image_w=1200, image_h=630, ld=None, og_type="website", accent=None):
    url = f"{URL}/{path}"
    ld_html = ""
    if ld:
        ld_html = '<script type="application/ld+json">\n' + json.dumps(ld, indent=1, ensure_ascii=False) + "\n</script>\n"
    style = ""
    if accent:
        r, g, b = (int(accent[i:i + 2], 16) / 255 for i in (1, 3, 5))
        on = "#0c0d0f" if 0.2126 * r + 0.7152 * g + 0.0722 * b > 0.45 else "#ffffff"   # text on an accent background
        style = f'<style>:root {{ --accent: {accent}; --on-accent: {on}; }}</style>\n'
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>{esc(title)}</title>
<meta name="description" content="{esc(desc)}">
<link rel="canonical" href="{url}">
<meta name="theme-color" content="#0c0d0f">
<meta property="og:type" content="{og_type}">
<meta property="og:site_name" content="Noise Division">
<meta property="og:title" content="{esc(title)}">
<meta property="og:description" content="{esc(desc)}">
<meta property="og:url" content="{url}">
<meta property="og:image" content="{URL}/{image}">
<meta property="og:image:width" content="{image_w}">
<meta property="og:image:height" content="{image_h}">
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="{esc(title)}">
<meta name="twitter:description" content="{esc(desc)}">
<meta name="twitter:image" content="{URL}/{image}">
<link rel="icon" href="{FAVICON}">
<link rel="preload" href="{R}assets/fonts/chakra-petch-bold.woff2" as="font" type="font/woff2" crossorigin>
<link rel="preload" href="{R}assets/fonts/montserrat-var.woff2" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="{R}assets/site.css?v={CSS_V}">
{style}{ld_html}</head>
"""


def header(R, current=None):
    items = []
    for p in PLUGINS:
        cur = ' aria-current="page"' if current == p["slug"] else ""
        items.append(f'<a href="{R}plugins/{p["slug"]}/" style="--c:{p["accent"]}"{cur}><span class="n">{p["name"]}</span><span class="k">{esc(p["kind"])}</span></a>')
    items.append(f'<a class="all" href="{R}#plugins">All plugins <span aria-hidden="true">→</span></a>')
    is_plugin = current in [p["slug"] for p in PLUGINS]
    cur = lambda k: ' aria-current="page"' if current == k else ""
    return f"""<body data-page="{current or 'page'}">
<a class="skip" href="#main">Skip to content</a>
<header class="top">
  <div class="wrap">
    <a class="brand" href="{R or './'}" aria-label="Noise Division home">{LOGO} NOISE <b>DIVISION</b></a>
    <nav class="links" aria-label="Main">
      <div class="dd">
        <button class="dd-btn" type="button" aria-expanded="false" aria-controls="dd-plugins"{' data-current' if is_plugin else ''}>Plugins {ICON_DOWN}</button>
        <div class="dd-panel" id="dd-plugins">
          {chr(10).join('          ' + i for i in items).strip()}
        </div>
      </div>
      <a href="{R}contact/"{cur('contact')}>Contact</a>
      <a class="hot" href="{R}donate/"{cur('donate')}>Donate</a>
    </nav>
  </div>
</header>
"""


def footer(R):
    links = "\n".join(f'        <li><a href="{R}plugins/{p["slug"]}/">{p["name"]}</a></li>' for p in PLUGINS)
    return f"""<footer>
  <div class="wrap">
    <div class="about">
      <a class="brand" href="{R or './'}">NOISE <b>DIVISION</b></a>
      <p>Free VST3 and AU plugins for Windows and macOS. Free to use in commercial and personal music.</p>
      <p>© {SITE['year']} Noise Division</p>
    </div>
    <div>
      <h2>Plugins</h2>
      <ul>
{links}
      </ul>
    </div>
    <div>
      <h2>Noise Division</h2>
      <ul>
        <li><a href="{R}contact/">Contact</a></li>
        <li><a href="{R}donate/">Donate</a></li>
        <li><a href="mailto:{SITE['email']}">{SITE['email']}</a></li>
      </ul>
    </div>
  </div>
</footer>
<script src="{R}assets/site.js?v={JS_V}" defer></script>
</body>
</html>
"""


def download_buttons(p):
    t = p["title"]
    return f"""<div class="ctas">
          <a class="btn primary" href="#" data-download="{p['slug']}" data-os="win">{ICON_WIN} {t} for Windows</a>
          <a class="btn mac" href="#" data-download="{p['slug']}" data-os="mac">{ICON_MAC} {t} for <span class="kc">macOS</span></a>
        </div>"""


DL_NOTE = "Free · Windows: .zip with the VST3 and the app · macOS: installer with AU, VST3 and the app, for Apple Silicon and Intel"


def suite_block(R, words):
    """The full suite: every plugin in one download (home page, #suite)."""
    names = "".join(f'<li><a href="{R}plugins/{p["slug"]}/">{p["name"]}</a></li>' for p in PLUGINS)
    return f"""<section class="suite" id="suite" aria-labelledby="su-title" style="scroll-margin-top:62px">
    <div class="wrap">
      <div class="txt">
        <p class="label" style="color:var(--orange)">The full suite</p>
        <h2 id="su-title">All {words.lower()} in one download</h2>
        <p>Every Noise Division plugin at once. On Windows, one .zip with a folder per plugin. On <span class="kc">macOS</span>, one installer; click Customize in it to leave any plugin out.</p>
        <ul class="suite-list" aria-label="Included">{names}</ul>
      </div>
      <div class="suite-dl">
        <div class="ctas">
          <a class="btn primary" href="#" data-download="suite" data-os="win">{ICON_WIN} Suite for Windows</a>
          <a class="btn mac" href="#" data-download="suite" data-os="mac">{ICON_MAC} Suite for <span class="kc">macOS</span></a>
        </div>
        <p class="dl-note" data-note="suite">Free · Windows: VST3 and the apps · macOS: AU, VST3 and the apps, Apple Silicon and Intel</p>
      </div>
    </div>
  </section>"""


def donate_block(R, slim=False, heading="h2"):
    return f"""<section class="donate{' slim' if slim else ''}" aria-labelledby="do-title">
    <div class="wrap">
      <div class="txt">
        <p class="label" style="color:var(--orange)">Support</p>
        <{heading} id="do-title">Keep the plugins free</{heading}>
        <p>Everything here is free and stays free. If a plugin made it into your tracks and you want to say thanks, a donation pays for the time that goes into new plugins and updates.</p>
      </div>
      <div>
        <div class="donate-options">
          <a class="donate-option beer" href="#" data-donate="bmc"><span><strong>Buy me a beer <span class="emoji" aria-hidden="true">🍺</span></strong><span>On Buy Me a Coffee: one beer, a round, or monthly</span></span><span class="go">Cheers →</span></a>
          <a class="donate-option paypal" href="#" data-donate="paypal"><span><strong>PayPal</strong><span>Send any amount directly</span></span><span class="go">Donate →</span></a>
        </div>
        <p class="donate-msg" id="donate-msg" role="status" aria-live="polite"></p>
      </div>
    </div>
  </section>"""


def card(R, p, small=False):
    tags = "" if small else '<div class="chips">' + "".join(f'<span class="chip">{esc(t)}</span>' for t in p["tags"]) + "</div>"
    desc = "" if small else f'<p class="desc">{esc(p["card"])}</p>'
    if not small and p.get("badge"):
        tags = f'<p class="badge-new">{esc(p["badge"])}</p>' + tags
    tags += "" if small else '<span class="more" aria-hidden="true">Details →</span>'
    h = "h3"
    return f"""<article class="card" style="--c:{p['accent']}">
          <div class="stage"><img src="{R}img/{p['img']}" alt="" width="{p['w']}" height="{p['h']}" loading="lazy" decoding="async"></div>
          <div class="body">
            <p class="label kind">{esc(p.get('tagline', p['kind']))}</p>
            <{h}><a href="{R}plugins/{p['slug']}/">{p['name']}</a></{h}>
            {desc}
            {tags}
          </div>
        </article>"""


def software_ld(p, standalone=True):
    d = {
        "@type": "SoftwareApplication",
        "name": p["name"],
        "url": f"{URL}/plugins/{p['slug']}/",
        "description": strip_tags(p["card"]),
        "applicationCategory": "MultimediaApplication",
        "applicationSubCategory": f"Audio plugin ({p['kind']})",
        "operatingSystem": "Windows, macOS",
        "softwareVersion": p.get("version", SITE["version"]),
        "image": f"{URL}/img/{p['img']}",
        "author": {"@id": f"{URL}/#org"},
        "offers": {"@type": "Offer", "price": "0", "priceCurrency": "EUR"},
    }
    if standalone:
        d["@context"] = "https://schema.org"
    return d


ORG = {
    "@type": "Organization",
    "@id": f"{URL}/#org",
    "name": "Noise Division",
    "url": f"{URL}/",
    "email": SITE["email"],
    "logo": f"{URL}/img/share.jpg",
}


def write(path, text):
    f = DOCS / path
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text(text, encoding="utf-8")
    print("wrote", f.relative_to(ROOT))


# ------------------------------------------------------------------ home
def trace(R):
    # the signal chain under the hero: IN -> the plugins in the order you'd chain them in a DAW -> OUT, each node links to its page
    # (plugin, x, width) along the chain; the path dips between the boxes
    W = {"pyre": 76, "krush": 88, "shrapnel": 118, "dark": 76, "recoil": 96, "spectrakill": 144, "peakslayer": 118, "masterfuse": 128, "flatline": 104, "transplant": 136}
    nodes, x = [], 50
    for p in PLUGINS:                                  # the site order is the chain order
        nodes.append((p["slug"], x, W.get(p["slug"], 100))); x += W.get(p["slug"], 100) + 28
    end = x - 28 + 58                                  # OUT, right after the last box
    path = f"M 0 46 H {nodes[0][1] - 22}"
    for i, (_, x, w) in enumerate(nodes):
        gap = (nodes[i + 1][1] - (x + w)) if i + 1 < len(nodes) else 60
        d = min(20, gap / 2 - 3)                      # slope width: down after this box, up before the next
        path += f" L {x - 2} 26 H {x + w + 2} L {x + w + 2 + d:g} 46"
        path += f" H {(nodes[i + 1][1] - 2 - min(20, gap / 2 - 3)) if i + 1 < len(nodes) else end:g}"
    names = {p["slug"]: p["name"] for p in PLUGINS}
    links = "\n".join(
        f'        <a href="{R}plugins/{s}/"><rect x="{x}" y="14" width="{w}" height="24" fill="#111214" stroke="#3a3c42"/><text class="node-name" x="{x + w / 2:g}" y="31" text-anchor="middle">{names[s]}</text></a>'
        for s, x, w in nodes)
    return f"""<div class="trace" aria-hidden="true">
    <div class="wrap">
      <svg viewBox="0 0 {end:g} 90" preserveAspectRatio="xMidYMid meet">
        <defs>
          <linearGradient id="heat" x1="0" x2="1"><stop offset="0" stop-color="#ff3038"/><stop offset=".7" stop-color="#ff7138"/><stop offset="1" stop-color="#ffa648"/></linearGradient>
          <filter id="glow" filterUnits="userSpaceOnUse" x="-20" y="-20" width="{end + 52:g}" height="130"><feGaussianBlur stdDeviation="3" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
        </defs>
        <path d="{path}" fill="none" stroke="url(#heat)" stroke-width="2" filter="url(#glow)"/>
        <path class="pulse" d="{path}" fill="none" stroke="#ffe2c8" stroke-width="3" stroke-linecap="round" filter="url(#glow)"/>
        <text class="node-label" x="0" y="74">IN</text>
{links}
        <text class="node-label" x="{end:g}" y="74" text-anchor="end">OUT</text>
      </svg>
    </div>
  </div>"""


def build_home():
    R = ""
    n = len(PLUGINS)
    words = {6: "Six", 7: "Seven", 8: "Eight", 9: "Nine", 10: "Ten"}.get(n, str(n))
    # how each plugin reads inside a sentence ("a ... , a ... and a ..."): some kind labels are tags, not nouns
    prose = {"one-knob mastering": "one-knob mastering chain", "harmonic loudness for leads": "harmonic exciter for leads",
             "sidechain ducking": "sidechain ducker", "match eq": "match EQ"}
    kinds = [prose.get(p["kind"].lower(), p["kind"].lower()) for p in PLUGINS]
    kinds_txt = ", ".join("a " + k for k in kinds[:-1]) + " and a " + kinds[-1]
    ld = {"@context": "https://schema.org", "@graph": [
        ORG,
        {"@type": "WebSite", "@id": f"{URL}/#site", "name": "Noise Division", "url": f"{URL}/", "publisher": {"@id": f"{URL}/#org"}},
        {"@type": "ItemList", "name": "Noise Division plugins", "itemListElement": [
            {"@type": "ListItem", "position": i + 1, "url": f"{URL}/plugins/{p['slug']}/", "name": p["name"]} for i, p in enumerate(PLUGINS)]},
    ]}
    by = {p["slug"]: p for p in PLUGINS}
    cards = "\n        ".join(card(R, p) for p in PLUGINS)
    page = head(R, title="Noise Division | Free VST3 & AU Plugins for Windows and macOS",
                desc=f"{words} free audio plugins for producers who push their mixes hard: {kinds_txt}. VST3, AU and standalone for Windows and macOS.",
                path="", image="img/share.jpg", ld=ld)
    page += header(R, "home")
    page += f"""
<main id="main">
  <section class="hero">
    <div class="wrap">
      <div>
        <p class="label">Audio plugins · Free to download</p>
        <h1>Loud tools.<br><span class="heat">Zero price.</span></h1>
        <p class="lede">{words} free VST3 and AU plugins for producers who push their mixes hard. For Windows and Mac, no account, no catch.</p>
        <div class="chips">
          <span class="chip">VST3 · AU · Standalone</span>
          <span class="chip">Windows &amp; <span class="kc">macOS</span></span>
          <span class="chip free">Free</span>
        </div>
        <div class="ctas">
          <a class="btn primary" href="#plugins">Browse the plugins</a>
          <a class="btn ghost" href="#suite">Download all {n}</a>
        </div>
      </div>
      <div class="stack" aria-hidden="true">
        <img class="s-1" src="img/{by['recoil']['img']}" alt="" width="{by['recoil']['w']}" height="{by['recoil']['h']}">
        <img class="s-2" src="img/{by['peakslayer']['img']}" alt="" width="{by['peakslayer']['w']}" height="{by['peakslayer']['h']}">
        <img class="s-3" src="img/{by['masterfuse']['img']}" alt="" width="{by['masterfuse']['w']}" height="{by['masterfuse']['h']}">
      </div>
    </div>
  </section>

  {trace(R)}

  <section class="grid-sec" id="plugins" aria-labelledby="pl-title" style="scroll-margin-top:62px">
    <div class="wrap">
      <div class="section-head">
        <div>
          <p class="label">The plugins</p>
          <h2 id="pl-title">Pick your weapon</h2>
        </div>
        <p>Each plugin has its own page with the full details, the downloads and a PDF user manual.</p>
      </div>
      <div class="cards">
        {cards}
      </div>
    </div>
  </section>

  {suite_block(R, words)}

  <section class="facts" aria-label="What you get">
    <div class="wrap">
      <div class="f"><h3>Free, for real</h3><p>No account, no licence key, no trial. Use them in commercial and personal music.</p></div>
      <div class="f"><h3>Windows &amp; <span class="kc">macOS</span></h3><p>VST3 on Windows; AU and VST3 on Mac, Apple Silicon and Intel. Most come as a standalone app too.</p></div>
      <div class="f"><h3>Full manuals</h3><p>Every plugin comes with a PDF manual covering every control, mode and preset.</p></div>
    </div>
  </section>

  {donate_block(R, slim=True)}
</main>

"""
    page += footer(R)
    write("index.html", page)


# ------------------------------------------------------------------ plugin pages
def ctl_rows(rows):
    out = []
    for name, rng, desc in rows:
        out.append(f'<div class="ctl"><span class="n">{name}</span><span class="r">{esc(rng) if rng else ""}</span><p class="d">{desc}</p></div>')
    return "\n            ".join(out)


ICON_PLAY = '<svg class="i-play" width="18" height="20" viewBox="0 0 18 20" aria-hidden="true"><path d="M2 1.5v17L17 10z" fill="currentColor"/></svg><svg class="i-pause" width="16" height="18" viewBox="0 0 16 18" aria-hidden="true"><path d="M2 1h4v16H2zM10 1h4v16h-4z" fill="currentColor"/></svg>'


def listen_section(R, p, demo):
    if not demo:
        return ""
    clips = [c for c in DEMOS["clips"] if c["id"] in demo["clips"]]
    tabs = "".join(
        f'<button type="button" role="tab" aria-selected="{"true" if i == 0 else "false"}" data-clip="{c["id"]}">{esc(c["label"])}</button>'
        for i, c in enumerate(clips))
    data = {"plugin": p["slug"], "base": f"{R}audio/", "keepLevel": demo["keepLevel"],
            "clips": [dict(id=c["id"], label=c["label"], **demo["clips"][c["id"]]) for c in clips]}
    first = demo["clips"][clips[0]["id"]]
    match = ""
    if demo["keepLevel"]:
        match = '<label class="ab-match"><input type="checkbox"> Match loudness</label>'
    lead = ("Bounced from a real session. 808: a kick and an 808, with Kick -> 808 and the Punch-Through Long shape, and with the 808 Tail preset (BAND on FULL). "
            "Mix: a whole song, with RECOIL on the bass bus. AUDIO is the Default preset with the kick on its sidechain; "
            "MAP is the Drum Bus Map preset with the drum bus and the bass bus bounced and loaded, so every kick and snare gets its own duck. "
            "Tick Match loudness to compare at equal volume."
            if p["slug"] == "recoil" else
            "Bounced from a real session. Lead: a synth lead with a little IGNITE and HEAT and BURN at +3 dB, for level. "
            "Mix: a whole track through PYRE, gently: a touch of HEAT with TILT toward bright, and no BURN. "
            "The level change is real here; tick Match loudness to compare at equal volume."
            if p["slug"] == "pyre" else
            "Same clips on every plugin page. The level change is real here, since loudness is what it's for; tick Match loudness to compare at equal volume."
            if demo["keepLevel"] else
            "Same clips on every plugin page. Before and after play at the same loudness, so you hear the character, not a volume jump.")
    return f"""
  <section class="sec listen" id="listen" aria-labelledby="ls-title">
    <div class="wrap">
      <p class="label">Hear it</p>
      <h2 id="ls-title">Before and after</h2>
      <p class="lead">{lead}</p>
      <div class="ab" data-side="after">
        <div class="ab-tabs" role="tablist" aria-label="Sample">{tabs}</div>
        <div class="ab-main">
          <button class="ab-play" type="button" aria-label="Play">{ICON_PLAY}</button>
          <div class="ab-wave" role="slider" tabindex="0" aria-label="Position" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0"><canvas></canvas></div>
          <div class="ab-switch" role="group" aria-label="Before or after">
            <button type="button" data-side="before" aria-pressed="false">Before</button>
            <button type="button" data-side="after" aria-pressed="true">After</button>
          </div>
        </div>
        <div class="ab-foot">
          <span class="ab-setting">{esc(first["setting"])}</span>
          <span class="ab-level"></span>
          {match}
          <span class="ab-hint">Press <kbd>B</kbd> to flip</span>
        </div>
        <script type="application/json" class="ab-data">{json.dumps(data, separators=(",", ":"))}</script>
      </div>
    </div>
  </section>
"""


def workflow_sec(p):
    # a worked method, step by step (FLATLINE: set the sound, calibrate, LEARN)
    if not p.get("workflow"):
        return ""
    steps = "\n          ".join(f"<li><span>{t}</span></li>" for t in p["workflow"])
    return f"""
  <section class="sec" id="workflow" aria-labelledby="wf-title">
    <div class="wrap">
      <p class="label">Workflow</p>
      <h2 id="wf-title">{esc(p.get("workflow_title", "Workflow"))}</h2>
      <p class="lead">{p.get("workflow_lead", "")}</p>
      <ol class="steps">
          {steps}
      </ol>
    </div>
  </section>
"""


def spotlight_sec(p):
    # a feature in depth (FLATLINE: MASK clipping): what it is, a figure, how to use it, the small print
    sp = p.get("spotlight")
    if not sp:
        return ""
    paras = "\n          ".join(f"<p>{t}</p>" for t in sp["paras"])
    fig = ""
    if sp.get("figure"):
        img, w, h, alt, cap = sp["figure"]
        fig = f'<figure class="spot-fig"><img src="../../img/{img}" alt="{esc(alt)}" width="{w}" height="{h}" loading="lazy" decoding="async"><figcaption>{cap}</figcaption></figure>'
    steps = "\n          ".join(f"<li><span>{t}</span></li>" for t in sp["steps"])
    facts = "\n          ".join(f"<div><dt>{esc(a)}</dt><dd>{b}</dd></div>" for a, b in sp.get("facts", []))
    return f"""
  <section class="sec spotlight" id="{sp['id']}" aria-labelledby="sp-title">
    <div class="wrap">
      <p class="label">{esc(sp['label'])}</p>
      <h2 id="sp-title">{esc(sp['title'])}</h2>
      <p class="lead">{sp['lead']}</p>
      <div class="prose spot-prose">
          {paras}
      </div>
      {fig}
      <h3 class="spot-h">{esc(sp.get('steps_title', 'How to use it'))}</h3>
      <ol class="steps">
          {steps}
      </ol>
      <dl class="spot-facts">
          {facts}
      </dl>
    </div>
  </section>
"""


def build_plugin(p):
    R = "../../"
    s = p["slug"]
    demo = DEMOS["plugins"].get(p["slug"])
    sections = ([("listen", "Listen")] if demo else []) + [("overview", "Overview")]
    if p.get("spotlight"): sections.append((p["spotlight"]["id"], p["spotlight"]["nav"]))
    if p.get("chain"): sections.append(("how", "How it works"))
    sections.append(("start", "Quick start"))
    if p.get("workflow"): sections.append(("workflow", "Workflow"))
    sections.append(("controls", "Controls"))
    if p.get("modes"): sections.append(("modes", p.get("modes_nav", "Styles" if p.get("modes_title") else "Modes")))
    if p.get("sidechain"): sections.append(("sidechain", "Sidechain"))
    if p.get("presets"): sections.append(("presets", "Presets"))
    sections += [("tips", "Tips"), ("install", "Install"), ("help", "Help")]
    toc = "\n      ".join(f'<a href="#{i}">{t}</a>' for i, t in sections)

    ld = {"@context": "https://schema.org", "@graph": [
        ORG,
        dict(software_ld(p, standalone=False), **{"@id": f"{URL}/plugins/{s}/#app", "description": strip_tags(p["meta"]),
                                                  "downloadUrl": f"{URL}/plugins/{s}/", "softwareHelp": {"@type": "CreativeWork", "url": f"{URL}/manuals/{manual_file(p)}"}}),
        {"@type": "BreadcrumbList", "itemListElement": [
            {"@type": "ListItem", "position": 1, "name": "Noise Division", "item": f"{URL}/"},
            {"@type": "ListItem", "position": 2, "name": "Plugins", "item": f"{URL}/#plugins"},
            {"@type": "ListItem", "position": 3, "name": p["name"], "item": f"{URL}/plugins/{s}/"}]},
    ]}
    page = head(R, title=f"{p['name']}: Free {p['kind']} VST3 & AU Plugin | Noise Division", desc=p["meta"],
                path=f"plugins/{s}/", image=f"img/og/{s}.jpg", ld=ld, accent=p["accent"])
    page += header(R, s)

    features = "\n            ".join(f"<li><span>{f}</span></li>" for f in p["features"])
    intro = "\n            ".join(f"<p>{t}</p>" for t in p["intro"])
    uses = "".join(f'<span class="chip">{esc(u)}</span>' for u in p["uses"])
    quick = "\n          ".join(f"<li><span>{t}</span></li>" for t in p["quick"])
    controls = "\n          ".join(
        f'<div class="ctl-group"><h3>{esc(g)}</h3>\n            {ctl_rows(rows)}\n          </div>' for g, rows in p["controls"])

    body = f"""
<main id="main">
  <section class="p-hero {p['shape']}" aria-labelledby="p-title">
    <div class="wrap">
      <nav class="crumbs" aria-label="Breadcrumb"><ol><li><a href="{R}">Home</a></li><li><a href="{R}#plugins">Plugins</a></li><li aria-current="page">{p['name']}</li></ol></nav>
      <div class="info">
        <p class="label kind">{esc(p.get('tagline', p['kind']))}</p>
        <h1 id="p-title">{p['name']}</h1>
        {f'<a class="new-link" href="#{p["spotlight"]["id"]}">{esc(p["spotlight"]["label"])}: {esc(p["spotlight"]["nav_long"])} <span aria-hidden="true">→</span></a>' if p.get("spotlight") else ""}
        <p class="pitch">{p['pitch']}</p>
        {download_buttons(p)}
        <p class="dl-note" data-note="{s}">{DL_NOTE if p.get("standalone", True) else DL_NOTE.replace(" and the app", "").replace("AU, VST3", "AU and VST3")}</p>
        <a class="manual-link" href="{R}manuals/{manual_file(p)}">{ICON_PDF} User manual (PDF)</a>
        <a class="suite-link" href="{R}#suite">Want them all? Get the full suite of {len(PLUGINS)} <span aria-hidden="true">→</span></a>
      </div>
      <figure class="shot"><img src="{R}img/{p['img']}" alt="{esc(p['alt'])}" width="{p['w']}" height="{p['h']}" fetchpriority="high"></figure>
    </div>
  </section>

  <nav class="toc" aria-label="On this page">
    <div class="wrap">
      {toc}
    </div>
  </nav>

{listen_section(R, p, demo)}
  <section class="sec" id="overview" aria-labelledby="ov-title">
    <div class="wrap">
      <p class="label">Overview</p>
      <h2 id="ov-title">What it does</h2>
      <div class="two">
        <div class="prose">
            {intro}
        </div>
        <div style="display:grid;gap:28px;align-content:start">
          <ul class="features">
            {features}
          </ul>
          <div class="uses"><p class="label">Good for</p><div class="chips">{uses}</div></div>
        </div>
      </div>
      <dl class="specs">
        <div><dt>Format</dt><dd>{"VST3, AU, Standalone" if p.get("standalone", True) else "VST3, AU"}</dd></div>
        <div><dt>System</dt><dd>Windows, <span class="kc">macOS</span></dd></div>
        <div><dt>Latency</dt><dd>{esc(p['latency'])}</dd></div>
        <div><dt>Version · Price</dt><dd>{p.get('version', SITE['version'])} · Free</dd></div>
      </dl>
    </div>
  </section>
"""
    body += spotlight_sec(p)
    if p.get("chain"):
        chain = "\n        ".join(f"<li><h3>{esc(a)}</h3><p>{esc(b)}</p></li>" for a, b in p["chain"])
        body += f"""
  <section class="sec" id="how" aria-labelledby="how-title">
    <div class="wrap">
      <p class="label">Under the hood</p>
      <h2 id="how-title">How it works</h2>
      <p class="lead">{p.get("chain_lead", "Everything below moves together when you turn the knob, in this order.")}</p>
      <ol class="chain">
        {chain}
      </ol>
    </div>
  </section>
"""
    body += f"""
  <section class="sec" id="start" aria-labelledby="qs-title">
    <div class="wrap">
      <p class="label">Quick start</p>
      <h2 id="qs-title">Up and running</h2>
      <ol class="steps">
          {quick}
      </ol>
    </div>
  </section>
{workflow_sec(p)}
  <section class="sec" id="controls" aria-labelledby="ct-title">
    <div class="wrap">
      <p class="label">Reference</p>
      <h2 id="ct-title">Every control</h2>
      <div class="ctls">
          {controls}
      </div>
"""
    if p.get("quality"):
        rows = "\n          ".join(f"<tr><td>{a}</td><td>{b}</td><td>{esc(c)}</td></tr>" for a, b, c in p["quality"])
        body += f"""      <div class="ctl-group"><h3>Quality settings</h3>
        <table class="qtable"><thead><tr><th>Setting</th><th>Latency</th><th>Best for</th></tr></thead><tbody>
          {rows}
        </tbody></table>
        <p class="note" style="margin-top:14px">The latency is reported to your DAW, which delays everything else to match, so tracks stay in time on playback.</p>
      </div>
"""
    body += """      <p class="note">Every plugin window scales from 60% to 200%: drag the bottom-right corner. Double-click a knob to return it to its default.</p>
    </div>
  </section>
"""
    if p.get("modes"):
        modes = "\n        ".join(
            f'<article class="mode"><h3>{esc(n)}</h3>{f"<p class=s>{esc(p.get(chr(109) + "odes_word", "Character"))}: {esc(c)}</p>" if c else ""}<p>{esc(d)}</p></article>' for n, c, d in p["modes"])
        label = "SHAPE" if s == "krush" else "CHARACTER"
        body += f"""
  <section class="sec" id="modes" aria-labelledby="md-title">
    <div class="wrap">
      <p class="label">{"Styles" if p.get("modes_title") else "Modes"}</p>
      <h2 id="md-title">{p.get("modes_title", f"{len(p['modes'])} ways to break it")}</h2>
      <p class="lead">{p.get("modes_lead") or ('Each band picks one mode. SHAPE changes its name and job with the mode.' if s == 'krush' else 'CHARACTER changes its job with the mode; the label above the knob shows what it does.')}</p>
      <div class="modes">
        {modes.replace('Character:', label.title() + ':') if s == 'krush' else modes}
      </div>
    </div>
  </section>
"""
    if p.get("sidechain"):
        sc = "\n          ".join(f"<li><span>{t}</span></li>" for t in p["sidechain"])
        body += f"""
  <section class="sec" id="sidechain" aria-labelledby="sc-title">
    <div class="wrap">
      <p class="label">Sidechain</p>
      <h2 id="sc-title">{"Route the trigger" if s == "recoil" else "Duck by frequency"}</h2>
      <p class="lead">{p.get("sidechain_lead", "Classic sidechain turns the whole bass down on every kick. SPECTRAKILL only turns down the frequencies the kick is actually using.")}</p>
      <ol class="steps">
          {sc}
      </ol>
    </div>
  </section>
"""
    if p.get("presets"):
        pre = "\n        ".join(f'<li><span class="n">{esc(n)}</span><span class="d">{esc(d)}</span></li>' for n, d in p["presets"])
        d = p.get("preset_dir")
        note = (f"""Click SAVE to store your own. They are kept in <code>%APPDATA%\\{d}\\Presets</code> on Windows and <code>~/Library/Application Support/{d}/Presets</code> on a Mac, so you can back them up or share them."""
                if d else esc(p.get("presets_note", "Your own settings are saved with the project.")))
        body += f"""
  <section class="sec" id="presets" aria-labelledby="pr-title">
    <div class="wrap">
      <p class="label">Presets</p>
      <h2 id="pr-title">{len(p['presets'])} factory presets</h2>
      <ul class="presets">
        {pre}
      </ul>
      <p class="note">{note}</p>
    </div>
  </section>
"""
    tips = "\n        ".join(f"<li>{t}</li>" for t in p["tips"])
    win = "\n            ".join(f"<li><h3>{a}</h3><p>{b}</p></li>" for a, b in for_plugin(p, INSTALL_WIN))
    mac = "\n            ".join(f"<li><h3>{a}</h3><p>{b}</p></li>" for a, b in for_plugin(p, INSTALL_MAC))
    faq = "\n      ".join(f"<details><summary>{q}</summary><p>{a}</p></details>" for q, a in p["trouble"] + for_plugin(p, TROUBLE_COMMON) + [(f"Something else is wrong", f'E-mail <a href="mailto:{SITE["email"]}">{SITE["email"]}</a> with the plugin name, version, your DAW and your operating system.')])
    others = "\n        ".join(card(R, o, small=True) for o in PLUGINS if o["slug"] != s)
    body += f"""
  <section class="sec" id="tips" aria-labelledby="tp-title">
    <div class="wrap">
      <p class="label">Tips</p>
      <h2 id="tp-title">Getting the most out of it</h2>
      <ul class="tips">
        {tips}
      </ul>
    </div>
  </section>

  <section class="sec" id="install" aria-labelledby="in-title">
    <div class="wrap">
      <p class="label">Install</p>
      <h2 id="in-title">Up and running in a minute</h2>
      <div class="os-cols">
        <div class="os-col">
          <h3 class="os-title">Windows</h3>
          <ol class="steps">
            {win}
          </ol>
          <p class="reqs">{for_plugin(p, REQUIREMENTS)[0][1]}</p>
        </div>
        <div class="os-col">
          <h3 class="os-title"><span class="kc">macOS</span></h3>
          <ol class="steps">
            {mac}
          </ol>
          <p class="reqs">{for_plugin(p, REQUIREMENTS)[1][1]}</p>
        </div>
      </div>
      <div class="manual-box">
        <div class="ico" aria-hidden="true">PDF</div>
        <div><h3>{p['name']} user manual</h3><p>Installation, every control, {'modes, ' if p.get('modes') else ''}{'presets, ' if p.get('presets') else ''}tips and troubleshooting in one PDF to keep next to your DAW.</p></div>
        <a class="btn ghost" href="{R}manuals/{manual_file(p)}">{ICON_PDF} Download PDF</a>
      </div>
    </div>
  </section>

  <section class="sec" id="help" aria-labelledby="hp-title">
    <div class="wrap">
      <p class="label">Help</p>
      <h2 id="hp-title">Troubleshooting</h2>
      <div class="faq">
      {faq}
      </div>
    </div>
  </section>

  <section class="sec others" aria-labelledby="ot-title">
    <div class="wrap">
      <p class="label">More from Noise Division</p>
      <h2 id="ot-title">The rest of the arsenal</h2>
      <div class="cards">
        {others}
      </div>
    </div>
  </section>

  {donate_block(R, slim=True)}
</main>

"""
    page += body + footer(R)
    write(f"plugins/{s}/index.html", page)


# ------------------------------------------------------------------ contact, donate, 404
def build_contact():
    R = "../"
    e = SITE["email"]
    ld = {"@context": "https://schema.org", "@type": "ContactPage", "url": f"{URL}/contact/", "name": "Contact Noise Division",
          "mainEntity": {"@id": f"{URL}/#org", "@type": "Organization", "name": "Noise Division", "email": e}}
    page = head(R, title="Contact | Noise Division", desc=f"Get in touch with Noise Division: bug reports, ideas for new plugins, feedback and everything else. E-mail {e}.",
                path="contact/", image="img/share.jpg", ld=ld)
    page += header(R, "contact")
    page += f"""
<main id="main">
  <section class="page-hero" aria-labelledby="c-title">
    <div class="wrap">
      <p class="label">Contact</p>
      <h1 id="c-title">Get in <span class="heat">touch</span></h1>
      <p class="lede">Found a bug, have an idea for a plugin, or just made something loud with one of them? Send an e-mail.</p>
      <div class="mail">
        <a class="addr" href="mailto:{e}">{e}</a>
        <div class="ctas">
          <a class="btn primary" href="mailto:{e}">Write an e-mail</a>
          <button class="btn ghost" type="button" data-copy="{e}">Copy address</button>
        </div>
      </div>
      <p class="copy-msg" id="copy-msg" role="status" aria-live="polite"></p>
    </div>
  </section>

  <section class="sec" aria-labelledby="t-title">
    <div class="wrap">
      <p class="label">What to write about</p>
      <h2 id="t-title">Help me help you</h2>
      <div class="topics">
        <div class="topic">
          <h3>Bug reports</h3>
          <p>Something crashes, sounds wrong or won't load? Include:</p>
          <ul><li>the plugin and its version</li><li>your DAW and its version</li><li>Windows or macOS, and which version</li><li>what you did just before it went wrong</li></ul>
        </div>
        <div class="topic">
          <h3>Ideas</h3>
          <p>A plugin you wish existed, a feature one of these is missing, or a preset idea. Every plugin here started as an idea.</p>
        </div>
        <div class="topic">
          <h3>Everything else</h3>
          <p>Feedback, tracks made with the plugins, collaborations, reviews and press. Before writing about a problem, the <a href="{R}plugins/peakslayer/#help">troubleshooting</a> section on each plugin page and its PDF manual may already have the answer.</p>
        </div>
      </div>
    </div>
  </section>
</main>

"""
    page += footer(R)
    write("contact/index.html", page)


def build_donate():
    R = "../"
    page = head(R, title="Donate | Noise Division", desc="Noise Division plugins are free and stay free. Support new plugins and updates with a beer on Buy Me a Coffee or a PayPal donation.",
                path="donate/", image="img/share.jpg", ld={"@context": "https://schema.org", "@type": "WebPage", "url": f"{URL}/donate/", "name": "Donate to Noise Division", "publisher": {"@id": f"{URL}/#org"}})
    page += header(R, "donate")
    page += f"""
<main id="main">
  <section class="page-hero" aria-labelledby="d-title">
    <div class="wrap">
      <p class="label">Donate</p>
      <h1 id="d-title">Fuel the <span class="heat">noise</span></h1>
      <p class="lede">No ads, no accounts, no paid versions. Donations are what keep the plugins free and new ones coming.</p>
    </div>
  </section>

  {donate_block(R, heading="h2")}

  <section class="sec" aria-labelledby="g-title">
    <div class="wrap">
      <p class="label">Where it goes</p>
      <h2 id="g-title">What a donation pays for</h2>
      <div class="goes">
        <div class="topic"><h3>New plugins</h3><p>Weeks of design, DSP and testing go into each one before it's released for free.</p></div>
        <div class="topic"><h3>Updates and fixes</h3><p>Keeping every plugin working with new DAWs, new macOS versions and new computers.</p></div>
        <div class="topic"><h3>Signed Mac installers</h3><p>An Apple developer account, so macOS stops asking you to allow the installer.</p></div>
      </div>
      <p class="note">Can't donate? Sharing the plugins, or a track made with them, helps just as much.</p>
    </div>
  </section>
</main>

"""
    page += footer(R)
    write("donate/index.html", page)


def build_404():
    R = "/"
    page = head(R, title="Page not found | Noise Division", desc="This page doesn't exist.", path="404.html", image="img/share.jpg")
    page = page.replace("<head>", "<head>\n<meta name=\"robots\" content=\"noindex\">", 1)
    page += header(R, "404")
    cards = "\n        ".join(card(R, p, small=True) for p in PLUGINS)
    page += f"""
<main id="main">
  <section class="page-hero" aria-labelledby="nf-title">
    <div class="wrap">
      <p class="label">Error 404</p>
      <h1 id="nf-title">Signal <span class="heat">lost</span></h1>
      <p class="lede">That page doesn't exist, or it moved when the site got a page for each plugin.</p>
      <div class="ctas"><a class="btn primary" href="/">Go to the home page</a></div>
    </div>
  </section>
  <section class="sec others" aria-label="Plugins">
    <div class="wrap">
      <div class="cards" style="grid-template-columns:repeat(auto-fill,minmax(160px,1fr))">
        {cards}
      </div>
    </div>
  </section>
</main>

"""
    page += footer(R)
    write("404.html", page)


def build_sitemap():
    entries = [("", "1.0", [("img/share.jpg", "Noise Division free audio plugins")])]
    for p in PLUGINS:
        entries.append((f"plugins/{p['slug']}/", "0.9", [(f"img/{p['img']}", strip_tags(p["alt"]))]))
    entries += [("contact/", "0.4", []), ("donate/", "0.5", [])]
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:image="http://www.google.com/schemas/sitemap-image/1.1">']
    for path, prio, imgs in entries:
        out.append("  <url>")
        out.append(f"    <loc>{URL}/{path}</loc>")
        out.append(f"    <lastmod>{TODAY}</lastmod>")
        out.append(f"    <priority>{prio}</priority>")
        for src, cap in imgs:
            out.append(f"    <image:image><image:loc>{URL}/{src}</image:loc><image:title>{esc(cap.split(':')[0])}</image:title></image:image>")
        out.append("  </url>")
    out.append("</urlset>")
    write("sitemap.xml", "\n".join(out) + "\n")


if __name__ == "__main__":
    build_home()
    for p in PLUGINS:
        build_plugin(p)
    build_contact()
    build_donate()
    build_404()
    build_sitemap()
