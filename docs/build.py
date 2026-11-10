"""Build the combined documentation site and its cross-language navigation."""
from __future__ import annotations

import html
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LANGUAGES = {"cpp": "C++", "python": "Python"}


def run(*args: str) -> None:
    subprocess.run(args, cwd=ROOT, check=True)


def main() -> None:
    output = Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    python_dir = ROOT / "python"
    os.environ["PYTHONPATH"] = os.pathsep.join([str(python_dir), str(python_dir / "vendor"), os.environ.get("PYTHONPATH", "")])
    run(sys.executable, "-m", "sphinx", "-b", "html", "-W", "--keep-going", str(ROOT / "docs" / "cpp"), str(output / "cpp"))
    run(sys.executable, "-m", "pdoc", "-o", str(output / "python"), *(str(python_dir / f"{name}.py") for name in ("defer", "analysis", "manifest")))
    shutil.copyfile(ROOT / "docs" / "site.css", output / "site.css")
    description = "Block-scoped cleanup for C and Objective-C, with C++ and Python tooling."
    cards = "".join(f'<a class="language" href="{key}/"><strong>{label}</strong><span>API reference →</span></a>' for key, label in LANGUAGES.items())
    (output / "index.html").write_text(f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="description" content="{html.escape(description)}"><title>defer documentation</title>
<link rel="stylesheet" href="site.css"></head><body class="docs-home"><main>
<p class="eyebrow">Developer documentation</p><h1>defer</h1><p class="lead">{html.escape(description)}</p>
<div class="languages">{cards}</div>
<p>Choose a language for its public types, functions and usage guidance. These references are generated from the source and published together on every main-branch update.</p>
<p><a href="https://github.com/zfogg/defer#readme">Getting started &amp; command-line guide</a> · <a href="https://github.com/zfogg/defer">Source on GitHub</a></p>
<footer>MIT licensed.</footer></main></body></html>\n''', encoding="utf-8")
    (output / ".nojekyll").touch()
    for language in LANGUAGES:
        for page in (output / language).rglob("*.html"):
            text = page.read_text(encoding="utf-8")
            # Sphinx can leave unchanged HTML in place between builds.
            text = re.sub(r'<nav id="language-nav".*?</nav>', "", text, flags=re.DOTALL)
            text = re.sub(r'<link rel="stylesheet" href="[^"]*site\.css">', "", text)
            prefix = "../" * len(page.relative_to(output).parent.parts)
            links = f'<a href="{prefix}index.html">defer docs</a>' + "".join(
                f'<a href="{prefix}{key}/"' + (' aria-current="page"' if key == language else "") + f'>{label}</a>'
                for key, label in LANGUAGES.items()
            )
            nav = f'<nav id="language-nav" aria-label="Documentation languages">{links}</nav>'
            text, head_count = re.subn(r"</head>", f'<link rel="stylesheet" href="{prefix}site.css"></head>', text, count=1, flags=re.IGNORECASE)
            if not head_count:
                raise RuntimeError(f"missing HTML head in {page}")
            text, body_count = re.subn(r"</body>", nav + "</body>", text, count=1, flags=re.IGNORECASE)
            if not body_count:
                raise RuntimeError(f"missing HTML body in {page}")
            page.write_text(text, encoding="utf-8")
    for required in ("index.html", "cpp/index.html", "cpp/api.html", "python/index.html", "python/defer.html"):
        if not (output / required).is_file():
            raise RuntimeError(f"missing generated documentation page: {required}")


if __name__ == "__main__":
    main()
