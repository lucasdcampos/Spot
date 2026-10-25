#!/usr/bin/env python3
"""Builds one self-contained HTML file (and optionally a PDF) from the Markdown pages in docs/: the manual, then internals.

The pages are only read. Needs the `markdown` package (`pip install markdown`). The PDF is printed by Edge or Chrome,
when one is found.

    python scripts/build_docs.py [--out build/docs/spot_docs.html] [--pdf]
"""
import argparse
import html
import re
import shutil
import subprocess
import sys
from pathlib import Path

try:
    import markdown
except ImportError:
    sys.exit("Missing dependency: pip install markdown")

ROOT = Path(__file__).resolve().parent.parent
DOCS = (ROOT / "docs").resolve()

CSS = """
:root {
  --bg:#ffffff; --side:#f6f8fa; --fg:#1f2328; --muted:#59636e; --line:#d1d9e0; --code:#f3f5f7; --accent:#0969da;
  --accent-soft:#ddf4ff; --mark:#fff8c5;
}
@media (prefers-color-scheme: dark) and (not print) {
  :root:not([data-theme="light"]) {
    --bg:#0d1117; --side:#010409; --fg:#e6edf3; --muted:#9198a1; --line:#30363d; --code:#161b22; --accent:#4493f8;
    --accent-soft:#121d2f; --mark:#3b2e00;
  }
}
:root[data-theme="dark"] {
  --bg:#0d1117; --side:#010409; --fg:#e6edf3; --muted:#9198a1; --line:#30363d; --code:#161b22; --accent:#4493f8;
  --accent-soft:#121d2f; --mark:#3b2e00;
}
* { box-sizing:border-box; }
html { scroll-padding-top:16px; }
@media (max-width: 900px) { html { scroll-padding-top:68px; } }
body { margin:0; background:var(--bg); color:var(--fg); font:16px/1.65 system-ui, -apple-system, "Segoe UI", sans-serif; }
a { color:var(--accent); text-decoration:none; }
a:hover { text-decoration:underline; }

aside { position:fixed; inset:0 auto 0 0; width:300px; display:flex; flex-direction:column; background:var(--side);
  border-right:1px solid var(--line); font-size:14px; z-index:2; }
.brand { display:flex; align-items:center; justify-content:space-between; padding:18px 18px 10px; }
.brand a { color:var(--fg); font-weight:700; font-size:17px; }
.brand button, .menu { background:none; border:1px solid var(--line); color:var(--fg); border-radius:6px;
  padding:3px 8px; cursor:pointer; font:inherit; }
.search { margin:0 18px 10px; }
.search input { width:100%; padding:7px 10px; border:1px solid var(--line); border-radius:6px; background:var(--bg);
  color:var(--fg); font:inherit; }
nav { position:relative; overflow:auto; padding:0 10px 24px; flex:1; }
nav .part { margin:18px 8px 4px; font-size:12px; font-weight:700; letter-spacing:.06em; text-transform:uppercase;
  color:var(--muted); }
nav .group { margin:10px 8px 2px; font-size:12.5px; font-weight:600; color:var(--muted); }
nav a.entry { display:block; padding:4px 8px; border-radius:6px; color:var(--fg); }
nav a.entry:hover { background:var(--accent-soft); text-decoration:none; }
nav a.entry.active { background:var(--accent-soft); color:var(--accent); font-weight:600; }
nav .toc { display:none; margin:2px 0 6px 12px; border-left:1px solid var(--line); }
nav a.entry.active + .toc { display:block; }
nav .toc a { display:block; padding:2px 10px; color:var(--muted); font-size:13px; }
nav .toc a.current { color:var(--accent); }
nav .results a { display:block; padding:6px 8px; border-radius:6px; color:var(--fg); }
nav .results a:hover { background:var(--accent-soft); text-decoration:none; }
nav .results small { display:block; color:var(--muted); }
nav .results mark { background:var(--mark); color:inherit; }

main { margin-left:300px; padding:40px 56px 80px; min-width:0; overflow-wrap:break-word; }
.page { max-width:900px; margin:0 auto; }
body.routed .page { display:none; }
body.routed .page.shown { display:block; }
.crumb { color:var(--muted); font-size:13.5px; margin-bottom:6px; }
h1, h2, h3, h4 { line-height:1.25; position:relative; }
h1 { font-size:2.1em; margin:0 0 .6em; }
h2 { border-bottom:1px solid var(--line); padding-bottom:.3em; margin-top:2.2em; }
h3 { margin-top:1.8em; }
.anchor { position:absolute; left:-1.1em; padding-right:.3em; color:var(--muted); opacity:0; font-weight:400; }
h2:hover .anchor, h3:hover .anchor, h4:hover .anchor { opacity:1; text-decoration:none; }
code { overflow-wrap:anywhere; background:var(--code); padding:.15em .38em; border-radius:5px; font:13.5px/1.5 ui-monospace, "Cascadia Code", Consolas, monospace; }
pre { position:relative; background:var(--code); padding:14px 16px; border-radius:8px; overflow:auto; border:1px solid var(--line); }
pre code { overflow-wrap:normal; display:block; padding:0; background:none; line-height:1.55; }
pre .copy { position:absolute; top:8px; right:8px; opacity:0; background:var(--bg); color:var(--muted);
  border:1px solid var(--line); border-radius:6px; padding:2px 8px; font:12px system-ui, sans-serif; cursor:pointer; }
pre:hover .copy, pre .copy:focus { opacity:1; }
table { border-collapse:collapse; display:block; overflow:auto; margin:1em 0; font-size:15px; }
th, td { border:1px solid var(--line); padding:7px 12px; text-align:left; vertical-align:top; }
th { background:var(--side); }
blockquote { margin:1em 0; padding:.4em 1em; color:var(--muted); border-left:4px solid var(--accent); background:var(--side); border-radius:0 6px 6px 0; }
img { max-width:100%; }
.pager { display:flex; gap:16px; justify-content:space-between; margin-top:56px; padding-top:20px; border-top:1px solid var(--line); }
.pager a, .pager span { flex:1; }
.pager a { border:1px solid var(--line); border-radius:8px; padding:10px 14px; }
.pager a:hover { border-color:var(--accent); text-decoration:none; }
.pager .next { text-align:right; }
.pager small { display:block; color:var(--muted); }
.menu { display:none; }

@media (max-width: 900px) {
  aside { transform:translateX(-100%); transition:transform .2s; width:min(300px, 85vw); }
  body.open aside { transform:none; box-shadow:0 0 0 100vmax rgba(0,0,0,.35); }
  main { margin:0; padding:72px 16px 48px; }
  .menu { display:block; position:fixed; inset:0 0 auto 0; z-index:1; height:52px; width:100%; text-align:left;
    padding:0 16px; border:0; border-bottom:1px solid var(--line); border-radius:0; background:var(--side); font-weight:600; }
  .anchor { display:none; }
}
@media print {
  aside, .menu, .pager, .copy, .anchor { display:none !important; }
  main { margin:0; padding:0; }
  body.routed .page { display:block; }
  .page { max-width:none; break-before:page; }
  .page:first-of-type { break-before:auto; }
  pre { white-space:pre-wrap; }
  a { color:inherit; }
}
"""

# One page at a time, chosen by the address's anchor; printing shows them all.
SCRIPT = """
(() => {
  const root = document.documentElement;
  try { const saved = localStorage.getItem("spot-docs-theme"); if (saved) root.dataset.theme = saved; } catch (e) {}
  document.getElementById("theme").onclick = () => {
    const dark = root.dataset.theme ? root.dataset.theme === "dark" : matchMedia("(prefers-color-scheme: dark)").matches;
    root.dataset.theme = dark ? "light" : "dark";
    try { localStorage.setItem("spot-docs-theme", root.dataset.theme); } catch (e) {}
  };
  document.querySelector(".menu").onclick = () => document.body.classList.toggle("open");

  const pages = [...document.querySelectorAll("section.page")];
  const links = new Map([...document.querySelectorAll("nav a.entry")].map(a => [a.dataset.page, a]));
  document.body.classList.add("routed");

  for (const pre of document.querySelectorAll("pre")) {
    const code = pre.querySelector("code") || pre;
    const button = document.createElement("button");
    button.className = "copy"; button.textContent = "Copy";
    button.onclick = () => navigator.clipboard.writeText(code.innerText.trimEnd()).then(() => {
      button.textContent = "Copied"; setTimeout(() => button.textContent = "Copy", 1200);
    });
    pre.appendChild(button);
  }
  for (const heading of document.querySelectorAll(".page h2[id], .page h3[id], .page h4[id]")) {
    const a = document.createElement("a");
    a.className = "anchor"; a.href = "#" + heading.id; a.textContent = "#"; a.setAttribute("aria-hidden", "true");
    heading.prepend(a);
  }

  let current = null;
  function Show() {
    const id = decodeURIComponent(location.hash.slice(1));
    const target = id && document.getElementById(id);
    const page = (target && target.closest("section.page")) || pages[0];
    if (page !== current) {
      if (current) current.classList.remove("shown");
      page.classList.add("shown");
      current = page;
      for (const [name, a] of links) a.classList.toggle("active", name === page.dataset.page);
      const link = links.get(page.dataset.page);
      const nav = document.querySelector("nav");
      if (link && (link.offsetTop < nav.scrollTop || link.offsetTop > nav.scrollTop + nav.clientHeight - 40))
        nav.scrollTop = link.offsetTop - 80;
      document.title = page.dataset.title + " - Spot documentation";
    }
    // After the browser's own jump to the anchor, which would leave a page's title under the mobile header.
    requestAnimationFrame(() => {
      if (target && target.tagName !== "H1" && target.tagName !== "SPAN") target.scrollIntoView();
      else window.scrollTo(0, 0);
    });
    document.body.classList.remove("open");
    Spy();
  }
  function Spy() {
    const link = current && links.get(current.dataset.page);
    const toc = link && link.nextElementSibling;
    if (!toc) return;
    let active = null;
    for (const h of current.querySelectorAll("h2[id]")) if (h.getBoundingClientRect().top < 120) active = h.id;
    for (const a of toc.querySelectorAll("a")) a.classList.toggle("current", a.hash.slice(1) === active);
  }
  addEventListener("hashchange", Show);
  addEventListener("load", Show);
  addEventListener("scroll", Spy, { passive: true });
  Show();

  const input = document.querySelector(".search input");
  const tree = document.querySelector("nav .tree");
  const results = document.querySelector("nav .results");
  const texts = pages.map(p => ({ page: p, raw: p.textContent.replace(/\\s+/g, " ") }));
  const escape = s => s.replace(/[&<>]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[c]);
  input.oninput = () => {
    const query = input.value.trim().toLowerCase();
    tree.hidden = !!query; results.hidden = !query;
    if (!query) return;
    results.innerHTML = "";
    let count = 0;
    for (const { page, raw } of texts) {
      const at = raw.toLowerCase().indexOf(query);
      if (at < 0 || ++count > 30) continue;
      const snippet = escape(raw.slice(Math.max(0, at - 40), at)) + "<mark>" + escape(raw.slice(at, at + query.length)) +
                      "</mark>" + escape(raw.slice(at + query.length, at + query.length + 60));
      const a = document.createElement("a");
      a.href = "#" + page.dataset.page;
      a.innerHTML = escape(page.dataset.title) + "<small>..." + snippet + "...</small>";
      a.onclick = () => { input.value = ""; input.oninput(); };
      results.appendChild(a);
    }
    if (!count) results.innerHTML = '<div class="group">Nothing found</div>';
  };
  addEventListener("keydown", e => {
    if (e.key === "/" && document.activeElement !== input) { e.preventDefault(); input.focus(); }
    if (e.key === "Escape" && document.activeElement === input) { input.value = ""; input.oninput(); input.blur(); }
  });
})();
"""


def ReadText(path):
    """Some pages are saved as Windows-1252 rather than UTF-8."""
    data = path.read_bytes()
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("cp1252", errors="replace")


def PageId(path):
    """A page's anchor: its path under docs/, as "manual-guides-scripting"."""
    return path.relative_to(DOCS).with_suffix("").as_posix().replace("/", "-")


def PageOrder():
    """Pages in the order each index links them, depth first from docs/index.md, then any page none links."""
    ordered = []

    def Visit(path):
        if path in ordered or not path.exists() or DOCS.resolve() not in path.parents:
            return
        ordered.append(path)
        if path.name == "index.md":
            for target in re.findall(r"\]\(([\w\-/.]+\.md)[^)]*\)", ReadText(path)):
                Visit((path.parent / target).resolve())

    Visit((DOCS / "index.md").resolve())
    for path in sorted(DOCS.rglob("*.md")):
        if path.resolve() not in ordered:
            ordered.append(path.resolve())
    return ordered


def Slug(prefix):
    def Make(value, separator):
        value = re.sub(r"[^\w\s-]", "", value.lower()).strip()
        return f"{prefix}-{re.sub(r'[-\s]+', separator, value)}"
    return Make


def RewriteLinks(body, page, pages):
    """Links to other pages become anchors in the one file; links out of docs/ stay relative to the repository."""
    ids = {path: PageId(path) for path in pages}
    current = ids[page]

    def Link(match):
        target, anchor = match.group(1), match.group(2)
        if target is None:  # "#anchor" inside the same page
            return f'href="#{current}-{anchor}"'
        resolved = (page.parent / target).resolve()
        if resolved in ids:
            return f'href="#{ids[resolved]}-{anchor}"' if anchor else f'href="#{ids[resolved]}"'
        return match.group(0)
    return re.sub(r'href="(?:([^"#:]+\.md))?(?:#([^"]*))?"', lambda m: Link(m) if (m.group(1) or m.group(2)) else m.group(0), body)


PARTS = {"manual": "Manual", "internals": "Internals"}


def PlainTitle(title):
    return re.sub(r"[`*]", "", title)


def GroupName(path):
    """The folder a page is in under its part, as people read it: "getting_started" is "Getting started"."""
    parts = path.relative_to(DOCS).parts
    return parts[1].replace("_", " ").capitalize() if len(parts) > 2 else None


def Render(path, pages):
    page = PageId(path)
    text = ReadText(path)
    title = (re.search(r"^# (.+)$", text, re.M) or [None, page])[1].strip()
    converter = markdown.Markdown(
        extensions=["fenced_code", "tables", "toc", "sane_lists"],
        extension_configs={"toc": {"slugify": Slug(page)}})
    body = RewriteLinks(converter.convert(text), path, pages)
    body = body.replace(f'id="{page}-{re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")}"', f'id="{page}"', 1)
    if f'id="{page}"' not in body:
        body = f'<span id="{page}"></span>' + body
    headings = [(child["id"], PlainTitle(html.unescape(child["name"])))
                for top in converter.toc_tokens for child in top["children"]]
    return page, PlainTitle(title), body, headings


def Build(out):
    paths = PageOrder()
    rendered = [(path, *Render(path, paths)) for path in paths]
    nav, sections = [], []
    part, group = None, None
    for index, (path, page, title, body, headings) in enumerate(rendered):
        relative = path.relative_to(DOCS).parts
        page_part = PARTS.get(relative[0]) if len(relative) > 1 else None
        if page_part != part:
            part, group = page_part, None
            if part:
                nav.append(f'<div class="part">{part}</div>')
        if GroupName(path) != group:
            group = GroupName(path)
            if group:
                nav.append(f'<div class="group">{html.escape(group)}</div>')
        label = "Overview" if path.name == "index.md" and part else "Home" if path.name == "index.md" else title
        toc = "".join(f'<a href="#{id}">{html.escape(name)}</a>' for id, name in headings)
        nav.append(f'<a class="entry" data-page="{page}" href="#{page}">{html.escape(label)}</a><div class="toc">{toc}</div>')

        crumb = " / ".join(filter(None, [part, GroupName(path)]))
        pager = ['<span></span>']
        if index > 0:
            previous = rendered[index - 1]
            pager[0] = f'<a class="previous" href="#{previous[1]}"><small>Previous</small>{html.escape(previous[2])}</a>'
        if index + 1 < len(rendered):
            following = rendered[index + 1]
            pager.append(f'<a class="next" href="#{following[1]}"><small>Next</small>{html.escape(following[2])}</a>')
        sections.append(f'<section class="page" data-page="{page}" data-title="{html.escape(title)}">'
                        + (f'<div class="crumb">{crumb}</div>' if crumb else "")
                        + f'{body}<div class="pager">{"".join(pager)}</div></section>')

    document = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
                '<meta name="viewport" content="width=device-width,initial-scale=1"><title>Spot documentation</title>'
                f'<style>{CSS}</style></head><body>'
                '<button class="menu" aria-label="Menu">&#9776;&nbsp; Spot documentation</button>'
                '<aside><div class="brand"><a href="#index">Spot documentation</a>'
                '<button id="theme" title="Light or dark" aria-label="Light or dark">&#9680;</button></div>'
                '<div class="search"><input type="search" placeholder="Search (/)" aria-label="Search"></div>'
                f'<nav><div class="tree">{"".join(nav)}</div><div class="results" hidden></div></nav></aside>'
                f'<main>{"".join(sections)}</main><script>{SCRIPT}</script></body></html>')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(document, encoding="utf-8")
    print(f"Wrote {out} ({len(paths)} pages)")


def FindBrowser():
    for name in ("msedge", "chrome", "google-chrome", "chromium"):
        found = shutil.which(name)
        if found:
            return found
    for path in (r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
                 r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
                 r"C:\Program Files\Google\Chrome\Application\chrome.exe"):
        if Path(path).exists():
            return path
    return None


def Pdf(source):
    browser = FindBrowser()
    if not browser:
        sys.exit("No Edge or Chrome found to print the PDF; open the HTML and print it instead.")
    target = source.with_suffix(".pdf")
    subprocess.run([browser, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                    f"--print-to-pdf={target}", source.as_uri()], check=True, capture_output=True)
    print(f"Wrote {target}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "docs" / "spot_docs.html")
    parser.add_argument("--pdf", action="store_true", help="also print a PDF beside the HTML")
    args = parser.parse_args()
    Build(args.out.resolve())
    if args.pdf:
        Pdf(args.out.resolve())
