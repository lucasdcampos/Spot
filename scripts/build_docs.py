#!/usr/bin/env python3
"""Builds one self-contained HTML file (and optionally a PDF) from the Markdown pages in docs/.

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
DOCS = ROOT / "docs"

CSS = """
:root { --bg:#fff; --fg:#1f2328; --muted:#59636e; --line:#d1d9e0; --code:#f6f8fa; --accent:#0969da; }
@media (prefers-color-scheme: dark) and (not print) {
  :root { --bg:#0d1117; --fg:#e6edf3; --muted:#9198a1; --line:#3d444d; --code:#161b22; --accent:#4493f8; }
}
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--fg); font:16px/1.6 system-ui, sans-serif; }
nav { position:fixed; inset:0 auto 0 0; width:280px; overflow:auto; padding:20px; border-right:1px solid var(--line); font-size:14px; }
nav h2 { margin:0 0 12px; font-size:16px; }
nav a { display:block; padding:3px 0; color:var(--fg); text-decoration:none; }
nav a:hover { color:var(--accent); }
main { margin-left:280px; padding:32px 48px; max-width:1100px; }
section.page { padding-bottom:32px; margin-bottom:32px; border-bottom:1px solid var(--line); }
a { color:var(--accent); }
h1, h2, h3 { line-height:1.25; }
h2 { border-bottom:1px solid var(--line); padding-bottom:.3em; margin-top:2em; }
code { background:var(--code); padding:.15em .35em; border-radius:4px; font:13.5px ui-monospace, Consolas, monospace; }
pre { background:var(--code); padding:14px; border-radius:6px; overflow:auto; }
pre code { padding:0; background:none; }
table { border-collapse:collapse; display:block; overflow:auto; }
th, td { border:1px solid var(--line); padding:6px 12px; text-align:left; vertical-align:top; }
blockquote { margin:0; padding:0 1em; color:var(--muted); border-left:4px solid var(--line); }
@media print {
  nav { display:none; }
  main { margin:0; padding:0; max-width:none; }
  section.page { break-before:page; border:0; }
  section.page:first-of-type { break-before:auto; }
  pre { white-space:pre-wrap; }
  a { color:inherit; }
}
@media (max-width: 800px) { nav { position:static; width:auto; border:0; } main { margin:0; padding:16px; } }
"""


def ReadText(path):
    """Some pages are saved as Windows-1252 rather than UTF-8."""
    data = path.read_bytes()
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("cp1252", errors="replace")


def PageOrder():
    """Pages in the order of index.md's table, then any page it does not list."""
    index = ReadText(DOCS / "index.md")
    ordered = ["index"]
    for name in re.findall(r"\]\(([\w\-]+)\.md[^)]*\)", index):
        if name not in ordered and (DOCS / f"{name}.md").exists():
            ordered.append(name)
    for path in sorted(DOCS.glob("*.md")):
        if path.stem not in ordered:
            ordered.append(path.stem)
    return ordered


def Slug(prefix):
    def Make(value, separator):
        value = re.sub(r"[^\w\s-]", "", value.lower()).strip()
        return f"{prefix}-{re.sub(r'[-\s]+', separator, value)}"
    return Make


def RewriteLinks(body, page, pages):
    def Link(match):
        target, anchor = match.group(1), match.group(2)
        if target is None:  # "#anchor" inside the same page
            return f'href="#{page}-{anchor}"'
        name = Path(target).stem
        if target.endswith(".md") and "/" not in target and name in pages:
            return f'href="#{name}-{anchor}"' if anchor else f'href="#{name}"'
        return match.group(0)
    return re.sub(r'href="(?:([^"#:]+\.md))?(?:#([^"]*))?"', lambda m: Link(m) if (m.group(1) or m.group(2)) else m.group(0), body)


def Build(out):
    pages = PageOrder()
    sections, nav = [], []
    for page in pages:
        text = ReadText(DOCS / f"{page}.md")
        title = (re.search(r"^# (.+)$", text, re.M) or [None, page])[1]
        converter = markdown.Markdown(
            extensions=["fenced_code", "tables", "toc", "sane_lists"],
            extension_configs={"toc": {"slugify": Slug(page)}})
        body = RewriteLinks(converter.convert(text), page, pages)
        body = body.replace(f'id="{page}-{re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")}"', f'id="{page}"', 1)
        sections.append(f'<section class="page" id="{page}-top">{body}</section>')
        nav.append(f'<a href="#{page}">{html.escape(re.sub(r"[`*]", "", title))}</a>')
    document = (f'<!doctype html><html lang="en"><head><meta charset="utf-8">'
                f'<meta name="viewport" content="width=device-width,initial-scale=1"><title>Spot documentation</title>'
                f'<style>{CSS}</style></head><body><nav><h2>Spot documentation</h2>{"".join(nav)}</nav>'
                f'<main>{"".join(sections)}</main></body></html>')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(document, encoding="utf-8")
    print(f"Wrote {out} ({len(pages)} pages)")


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
