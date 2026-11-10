"""Build the combined documentation site and its cross-language navigation."""
from __future__ import annotations

import html
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from pdoc.render_helpers import to_html

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
    run(sys.executable, "-m", "pdoc", "--docformat", "markdown", "-t", str(ROOT / "docs" / "pdoc"),
        "-o", str(output / "python"), *(str(python_dir / f"{name}.py") for name in ("defer", "analysis", "manifest")))
    landing = output / "python" / "index.html"
    text = landing.read_text(encoding="utf-8")
    marker = '<main class="pdoc"></main>'
    if marker not in text:
        raise RuntimeError("pdoc's Python landing-page placeholder was not found")
    readme = to_html((python_dir / "README.md").read_text(encoding="utf-8"))
    readme = re.sub(
        r'href="(?!(?:https?:|mailto:|#))((?:\.\./)*[^"#]+)',
        lambda match: f'href="https://github.com/zfogg/defer/blob/main/{match[1].lstrip("./")}',
        readme,
    )
    landing.write_text(text.replace(marker, f'<main class="pdoc"><section class="module-info python-landing">{readme}</section></main>'), encoding="utf-8")
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
    api_page = (output / "cpp/api.html").read_text(encoding="utf-8")
    if 'class="sig sig-object cpp"' not in api_page or 'id="c.defer_transform_source"' not in api_page:
        raise RuntimeError("native API documentation did not include the transformer")
    if ".. cpp:function::" in api_page or ".. c:function::" in api_page:
        raise RuntimeError("native API directives were not rendered")
    if "Native C++ tool and SDK" not in (output / "cpp/index.html").read_text(encoding="utf-8"):
        raise RuntimeError("C++ landing page did not render its README")
    if "Python defer tool" not in landing.read_text(encoding="utf-8"):
        raise RuntimeError("Python landing page did not render its README")


if __name__ == "__main__":
    main()
