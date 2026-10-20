from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
# Root and docs/ pages only: package READMEs and distribution/ link paths of the npm package
# and the mirror repos, which resolve there and not in this checkout.
PAGES = (":(glob)*.md", ":(glob)docs/**/*.md")
FENCE = re.compile(r"```.*?```", re.S)
LINK = re.compile(r"\]\(([^)\s]+)\)|^\[[^\]]+\]:\s*(\S+)", re.M)
EXTERNAL = re.compile(r"^[a-z][a-z0-9+.-]*:", re.I)


def tracked_pages():
    out = subprocess.run(["git", "ls-files", "--", *PAGES], cwd=ROOT, check=True,
                         capture_output=True, text=True).stdout
    return sorted(set(out.split()))


def broken_links(page):
    text = FENCE.sub("", (ROOT / page).read_text(encoding="utf-8"))
    broken = []
    for match in LINK.finditer(text):
        target = (match.group(1) or match.group(2)).split("#")[0]
        if not target or EXTERNAL.match(target):
            continue
        if not (ROOT / page).parent.joinpath(target).exists():
            broken.append(target)
    return broken


class DocLinksTest(unittest.TestCase):
    def test_relative_links_resolve(self):
        pages = tracked_pages()
        self.assertIn("docs/ARCHITECTURE.md", pages)
        for page in pages:
            with self.subTest(page=page):
                self.assertEqual(broken_links(page), [])


if __name__ == "__main__":
    unittest.main()
