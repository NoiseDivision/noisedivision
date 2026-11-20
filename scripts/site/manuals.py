#!/usr/bin/env python3
"""Builds the PDF user manuals into docs/manuals/<PLUGIN>-User-Manual.pdf from the text in content.py.

    pip install playwright pypdf
    python -m playwright install chromium
    python scripts/site/manuals.py            (all plugins)
    python scripts/site/manuals.py dark       (just one)

Each manual is a dark cover page plus light, printable A4 pages with page numbers.
"""
import io
import pathlib
import sys

from playwright.sync_api import sync_playwright
from pypdf import PdfReader, PdfWriter

from content import (SITE, PLUGINS, INSTALL_WIN, INSTALL_MAC, REQUIREMENTS, DAW_RESCAN, TROUBLE_COMMON)

HERE = pathlib.Path(__file__).resolve().parent
DOCS = HERE.parents[1] / "docs"
FONTS = (DOCS / "assets" / "fonts").as_uri()
import tempfile
from PIL import Image

# JPEG copies of the screenshots keep the PDFs small (Chromium would embed the WebP as a lossless image)
_TMP = pathlib.Path(tempfile.mkdtemp(prefix="nd-manual-"))
for _f in (DOCS / "img").glob("*.webp"):
    _im = Image.open(_f).convert("RGB")
    _im.thumbnail((1700, 1700), Image.LANCZOS)
    _im.save(_TMP / (_f.stem + ".jpg"), quality=86, optimize=True)
IMG = _TMP.as_uri()

# light-page versions of the accents (the bone and pale colours vanish on white paper)
PRINT_ACCENT = {"spectrakill": "#9a1b22", "masterfuse": "#d9661c", "shrapnel": "#e2561e", "dark": "#a3121c"}

FONT_CSS = f"""
@font-face {{ font-family: "Chakra Petch"; src: url("{FONTS}/chakra-petch-semibold.woff2"); font-weight: 600; }}
@font-face {{ font-family: "Chakra Petch"; src: url("{FONTS}/chakra-petch-bold.woff2"); font-weight: 700; }}
@font-face {{ font-family: "Montserrat"; src: url("{FONTS}/montserrat-var.woff2"); font-weight: 100 900; }}
"""


def jpg(p):
    return p["img"].rsplit(".", 1)[0] + ".jpg"


def esc(s):
    import html
    return html.escape(s)


def cover_html(p):
    return f"""<!doctype html><html><head><meta charset="utf-8"><style>{FONT_CSS}
@page {{ size: A4; margin: 0; }}
html, body {{ margin: 0; }}
body {{ width: 210mm; height: 296mm; background: #0c0d0f; color: #e4e5e8; font-family: Montserrat, sans-serif; position: relative; overflow: hidden; }}
.glow {{ position: absolute; inset: 40% -30% -30% -30%; background: radial-gradient(ellipse at 50% 60%, {p['accent']}55, transparent 60%); }}
.top {{ position: absolute; left: 18mm; top: 18mm; right: 18mm; display: flex; justify-content: space-between; font-family: "Chakra Petch"; font-weight: 700; letter-spacing: .22em; font-size: 11pt; color: #c6c8cc; }}
.top b {{ color: #ff3038; }}
.top span {{ color: #8a8d95; font-weight: 600; letter-spacing: .18em; font-size: 9pt; }}
.title {{ position: absolute; left: 18mm; top: 42mm; right: 18mm; }}
.kind {{ font-family: "Chakra Petch"; font-weight: 600; font-size: 11pt; letter-spacing: .2em; text-transform: uppercase; color: {p['accent']}; }}
h1 {{ font-family: "Chakra Petch"; font-weight: 700; font-size: 56pt; line-height: 1; margin: 4mm 0 0; letter-spacing: .03em; color: #fff; }}
.sub {{ margin-top: 5mm; font-family: "Chakra Petch"; font-weight: 600; font-size: 16pt; letter-spacing: .14em; text-transform: uppercase; color: #c6c8cc; }}
.bar {{ width: 26mm; height: 1.2mm; background: linear-gradient(90deg, #ff3038, #ff7138); margin-top: 7mm; }}
.shot {{ position: absolute; left: 18mm; right: 18mm; top: 108mm; bottom: 34mm; display: flex; align-items: center; justify-content: center; }}
.shot img {{ max-width: 100%; max-height: 100%; border-radius: 2mm; border: .3mm solid #2c2e33; box-shadow: 0 8mm 20mm rgba(0,0,0,.7); }}
.foot {{ position: absolute; left: 18mm; right: 18mm; bottom: 16mm; display: flex; justify-content: space-between; font-size: 9pt; color: #8a8d95; }}
</style></head><body>
<div class="glow"></div>
<div class="top"><div>NOISE <b>DIVISION</b></div><span>Version {SITE['version']}</span></div>
<div class="title"><div class="kind">{esc(p['kind'])}</div><h1>{p['name']}</h1><div class="sub">User manual</div><div class="bar"></div></div>
<div class="shot"><img src="{IMG}/{jpg(p)}"></div>
<div class="foot"><span>VST3 · AU · Standalone · Windows &amp; macOS</span><span>noisedivision.net</span></div>
</body></html>"""


def body_html(p):
    acc = PRINT_ACCENT.get(p["slug"], p["accent"])
    secs = []   # (id, title, html)

    feats = "".join(f"<li>{f}</li>" for f in p["features"])
    intro = "".join(f"<p>{t}</p>" for t in p["intro"])
    uses = ", ".join(u[0].lower() + u[1:] if not u[:3].isupper() else u for u in p["uses"])
    secs.append(("Introduction", f"{intro}<h3>Highlights</h3><ul class='feat'>{feats}</ul><p class='muted'><b>Good for:</b> {esc(uses)}.</p>"))

    win = "".join(f"<li><b>{a}.</b> {b}</li>" for a, b in INSTALL_WIN)
    mac = "".join(f"<li><b>{a}.</b> {b}</li>" for a, b in INSTALL_MAC)
    req = "".join(f"<tr><th>{a}</th><td>{b}</td></tr>" for a, b in REQUIREMENTS)
    daw = "".join(f"<tr><th>{a}</th><td>{b}</td></tr>" for a, b in DAW_RESCAN)
    secs.append(("Installation", f"""
<p>Download the installer for your system from <b>noisedivision.net/plugins/{p['slug']}/</b>.</p>
<div class="cols"><div><h3>Windows</h3><ol>{win}</ol></div><div><h3><span style='text-transform:none'>macOS</span></h3><ol>{mac}</ol></div></div>
<h3>System requirements</h3><table class="kv">{req}</table>
<h3>Finding the plugin in your DAW</h3><table class="kv">{daw}</table>"""))

    quick = "".join(f"<li>{t}</li>" for t in p["quick"])
    secs.append(("Quick start", f"<ol class='steps'>{quick}</ol>"))

    help_line = " Turn on the <b>?</b> button to get a pop-up explanation when you hover any control." if p["slug"] in ("krush", "spectrakill", "shrapnel") else " Hover a control to see a short explanation."
    secs.append(("The interface", f"""<figure><img src="{IMG}/{jpg(p)}" class="ui {p['shape']}"></figure>
<ul><li><b>Resize:</b> drag the bottom-right corner. The window scales from 60% to 200% and remembers its size.</li>
<li><b>Reset:</b> double-click a knob to return it to its default value.</li>
<li><b>Help:</b>{help_line}</li>
<li><b>Automation:</b> every control can be automated from your DAW.</li></ul>"""))

    if p.get("chain"):
        ch = "".join(f"<tr><th>{esc(a)}</th><td>{esc(b)}</td></tr>" for a, b in p["chain"])
        secs.append(("How it works", f"<p>Everything below moves together when you turn the main knob, in this order.</p><table class='kv num'>{ch}</table>"))

    ctl = ""
    for g, rows in p["controls"]:
        r = "".join(f"<tr><th>{n}</th><td class='rng'>{esc(rg)}</td><td>{d}</td></tr>" for n, rg, d in rows)
        ctl += f"<h3>{esc(g)}</h3><table class='ctl'><thead><tr><th>Control</th><th>Range</th><th>What it does</th></tr></thead><tbody>{r}</tbody></table>"
    if p.get("quality"):
        r = "".join(f"<tr><th>{a}</th><td class='rng'>{b}</td><td>{esc(c)}</td></tr>" for a, b, c in p["quality"])
        ctl += f"<h3>Quality settings</h3><table class='ctl'><thead><tr><th>Setting</th><th>Latency</th><th>Best for</th></tr></thead><tbody>{r}</tbody></table>"
    secs.append(("Controls reference", ctl))

    if p.get("modes"):
        word = "SHAPE" if p["slug"] == "krush" else "CHARACTER"
        r = "".join(f"<tr><th>{esc(n)}</th><td class='rng'>{esc(c)}</td><td>{esc(d)}</td></tr>" for n, c, d in p["modes"])
        secs.append(("Modes", f"<p>{word} changes its job with the mode; its label in the plugin shows what it does.</p><table class='ctl'><thead><tr><th>Mode</th><th>{word.title()}</th><th>Sound</th></tr></thead><tbody>{r}</tbody></table>"))

    if p.get("sidechain"):
        sc = "".join(f"<li>{t}</li>" for t in p["sidechain"])
        secs.append(("Sidechain", f"<p>Classic sidechain compression turns the whole bass down on every kick. SPECTRAKILL only turns down the frequencies the kick is actually using, at the moment it uses them.</p><ol class='steps'>{sc}</ol>"))

    if p.get("presets"):
        r = "".join(f"<tr><th>{esc(n)}</th><td>{esc(d)}</td></tr>" for n, d in p["presets"])
        d = p["preset_dir"]
        secs.append(("Presets", f"""<p>Pick a preset from the list at the top of the window, or step through them with the arrows. Each preset has a short description in the list.</p>
<table class="kv">{r}</table>
<h3>Your own presets</h3><p>Click <b>SAVE</b> to store the current settings as a preset; <b>DELETE</b> removes one of your own. They are stored as files you can back up or share:</p>
<table class="kv"><tr><th>Windows</th><td><code>%APPDATA%\\{d}\\Presets</code></td></tr><tr><th>macOS</th><td><code>~/Library/Application Support/{d}/Presets</code></td></tr></table>"""))
    else:
        secs.append(("Saving your settings", "<p>All settings are saved with your project. To reuse a setting in other projects, use your DAW's own preset function for the plugin.</p>"))

    secs.append(("Latency", f"<p><b>{esc(p['latency'])}.</b> Your DAW reads the plugin's latency and delays the other tracks to match, so everything stays in time on playback. When recording through the plugin live, choose the lowest-latency setting.</p>"))

    tips = "".join(f"<li>{t}</li>" for t in p["tips"])
    secs.append(("Tips", f"<ul class='tips'>{tips}</ul>"))

    tr = "".join(f"<tr><th>{q}</th><td>{a}</td></tr>" for q, a in p["trouble"] + TROUBLE_COMMON)
    secs.append(("Troubleshooting", f"<table class='kv wide'>{tr}</table>"))

    secs.append(("Support and contact", f"""
<p>Questions, bug reports and ideas: <b>{SITE['email']}</b>. For a bug, include the plugin name and version, your DAW and its version, your operating system, and what you did just before it went wrong.</p>
<p>Updates and the other Noise Division plugins: <b>noisedivision.net</b></p>
<p>{p['name']} is free. If it made it into your tracks, you can support new plugins and updates at <b>noisedivision.net/donate</b>.</p>
<h3>Terms</h3>
<p class="muted">{p['name']} is free to use in commercial and personal music. It is provided as is, without warranty of any kind. VST is a trademark of Steinberg Media Technologies GmbH. Audio Units, macOS and Logic Pro are trademarks of Apple Inc. All other product names are trademarks of their respective owners.</p>"""))

    toc = "".join(f"<li><span>{i + 1:02d}</span>{esc(t)}</li>" for i, (t, _) in enumerate(secs))
    body = "".join(f"<section class='{"brk" if t in ("Controls reference", "Installation") else ""}{" keep" if t in ("Quick start", "Latency", "Tips", "Sidechain", "How it works") else ""}'><h2><span>{i + 1:02d}</span>{esc(t)}</h2>{h}</section>" for i, (t, h) in enumerate(secs))
    return f"""<!doctype html><html><head><meta charset="utf-8"><style>{FONT_CSS}
@page {{ size: A4; margin: 18mm 17mm 20mm; }}
body {{ margin: 0; font-family: Montserrat, sans-serif; font-size: 9.6pt; line-height: 1.55; color: #1b1c1f; }}
h2, h3, th, .toc li span, h2 span {{ font-family: "Chakra Petch", sans-serif; }}
h2 {{ font-size: 19pt; font-weight: 700; letter-spacing: .06em; text-transform: uppercase; margin: 0 0 5mm; padding-top: 3mm; border-top: .8mm solid #111214; display: flex; gap: 4mm; align-items: baseline; break-after: avoid; }}
h2 span {{ color: {acc}; font-size: 12pt; letter-spacing: .1em; }}
h3 {{ font-size: 11pt; font-weight: 700; letter-spacing: .12em; text-transform: uppercase; color: {acc}; margin: 6mm 0 2.5mm; break-after: avoid; }}
section {{ margin-bottom: 10mm; }}
section.brk {{ break-before: page; }}
section.keep {{ break-inside: avoid; }}
p {{ margin: 0 0 3mm; }}
ul, ol {{ margin: 0 0 3mm; padding-left: 5mm; }}
li {{ margin-bottom: 1.6mm; }}
b {{ font-weight: 650; }}
code {{ font-family: Consolas, monospace; font-size: 8.6pt; background: #f0f0f2; padding: .2mm 1.2mm; border-radius: .6mm; }}
.muted {{ color: #55585f; }}
ul.feat {{ list-style: none; padding: 0; }}
ul.feat li {{ padding-left: 5mm; position: relative; }}
ul.feat li::before {{ content: ""; position: absolute; left: 0; top: 1.8mm; width: 2mm; height: 2mm; background: {acc}; }}
.cols {{ display: grid; grid-template-columns: 1fr 1fr; gap: 8mm; }}
table {{ width: 100%; border-collapse: collapse; margin: 0 0 3mm; }}
th, td {{ text-align: left; vertical-align: top; padding: 2.2mm 3mm 2.2mm 0; border-bottom: .25mm solid #dcdde1; }}
tr {{ break-inside: avoid; }}
th {{ font-weight: 700; letter-spacing: .05em; white-space: nowrap; }}
table.kv th {{ width: 34mm; }}
table.kv.wide th {{ width: 62mm; white-space: normal; }}
table.ctl thead th {{ font-size: 7.6pt; letter-spacing: .16em; text-transform: uppercase; color: #6b6e75; font-weight: 600; border-bottom: .4mm solid #1b1c1f; }}
table.ctl tbody th {{ width: 34mm; white-space: normal; }}
table.ctl td.rng {{ width: 40mm; color: #55585f; font-size: 8.6pt; }}
ol.steps {{ counter-reset: s; list-style: none; padding: 0; }}
ol.steps li {{ counter-increment: s; position: relative; padding-left: 10mm; min-height: 7mm; }}
ol.steps li::before {{ content: counter(s); position: absolute; left: 0; top: -.4mm; width: 6.4mm; height: 6.4mm; border-radius: 50%; background: #111214; color: #fff; font-family: "Chakra Petch"; font-weight: 700; font-size: 9pt; display: flex; align-items: center; justify-content: center; }}
ul.tips {{ list-style: none; padding: 0; }}
ul.tips li {{ border-left: .8mm solid {acc}; padding: 1mm 0 1mm 4mm; margin-bottom: 3mm; }}
figure {{ margin: 0 0 5mm; text-align: center; }}
img.ui {{ max-width: 100%; border-radius: 1.5mm; }}
img.ui.tall {{ max-height: 128mm; }}
img.ui.square {{ max-height: 120mm; }}
.toc {{ margin-bottom: 10mm; }}
.toc ol {{ list-style: none; padding: 0; columns: 2; column-gap: 10mm; }}
.toc li {{ display: flex; gap: 4mm; padding: 1.6mm 0; border-bottom: .25mm solid #dcdde1; font-weight: 500; break-inside: avoid; }}
.toc li span {{ color: {acc}; font-weight: 700; }}
.toc .label {{ font-family: "Chakra Petch"; font-weight: 700; font-size: 19pt; letter-spacing: .06em; text-transform: uppercase; border-top: .8mm solid #111214; padding-top: 3mm; margin-bottom: 4mm; }}
</style></head><body>
<div class="toc"><div class="label">Contents</div><ol>{toc}</ol></div>
{body}
</body></html>"""


def footer(p):
    return f"""<div style="width:100%;font-family:Montserrat,Arial,sans-serif;font-size:7.5pt;color:#8a8d95;padding:0 17mm;display:flex;justify-content:space-between;">
<span><b style="color:#1b1c1f;letter-spacing:.12em">{p['name']}</b> &nbsp;User manual &nbsp;·&nbsp; v{SITE['version']}</span><span>noisedivision.net &nbsp;·&nbsp; <span class="pageNumber"></span> / <span class="totalPages"></span></span></div>"""


def main(only):
    out_dir = DOCS / "manuals"
    out_dir.mkdir(exist_ok=True)
    with sync_playwright() as pw:
        browser = pw.chromium.launch()
        page = browser.new_page()
        tmp = DOCS / "manuals" / "_render.html"     # rendered from a file so the fonts and images load
        for p in PLUGINS:
            if only and p["slug"] not in only:
                continue
            tmp.write_text(cover_html(p), encoding="utf-8"); page.goto(tmp.as_uri(), wait_until="networkidle")
            cover = page.pdf(format="A4", print_background=True, margin={"top": "0", "bottom": "0", "left": "0", "right": "0"})
            tmp.write_text(body_html(p), encoding="utf-8"); page.goto(tmp.as_uri(), wait_until="networkidle")
            body = page.pdf(format="A4", print_background=True, prefer_css_page_size=True, display_header_footer=True,
                            header_template="<span></span>", footer_template=footer(p))
            w = PdfWriter()
            w.add_page(PdfReader(io.BytesIO(cover)).pages[0])
            for pg in PdfReader(io.BytesIO(body)).pages:
                w.add_page(pg)
            w.add_metadata({"/Title": f"{p['name']} User Manual", "/Author": "Noise Division",
                            "/Subject": f"{p['name']} {p['kind']} plugin, version {SITE['version']}"})
            dest = out_dir / f"{p['name']}-User-Manual.pdf"
            with open(dest, "wb") as f:
                w.write(f)
            print("wrote", dest.relative_to(HERE.parents[1]), len(w.pages), "pages")
        tmp.unlink(missing_ok=True)
        browser.close()


if __name__ == "__main__":
    main(sys.argv[1:])
