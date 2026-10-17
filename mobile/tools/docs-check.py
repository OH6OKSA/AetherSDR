#!/usr/bin/env python3
"""Keeps OksaSDR's GitHub-facing docs in step with the app.

    mobile/tools/docs-check.py [--base REV] [--head REV]

Checks the commits in BASE..HEAD (default: what the upstream branch does not
have yet) and the docs as they are at HEAD:

  * an app change (the paths that start an iOS build, plus Android) needs
    new TestFlight notes (mobile/TESTFLIGHT_WHATS_NEW.txt) and a ROADMAP.md
    update;
  * a UI change (mobile/qml) needs the manual (docs/mobile/MANUAL.md) and
    refreshed screenshots (mobile/tools/update-docs.sh RADIO_IP);
  * the TestFlight notes fit Apple's 4000 characters and are in English;
  * every relative link, image and #anchor in the docs resolves;
  * the docs carry no old repository names, phone numbers or radio serials.

A rule that does not apply to a change is waived with a commit trailer
anywhere in the range, which also records why:

    Manual: unchanged (internal fix)
    Screenshots: unchanged (no visible change)
    Roadmap: unchanged (...)
    Notes: unchanged (...)

Run by the pre-push hook (mobile/tools/githooks/pre-push) and by CI
(.github/workflows/mobile-docs.yml). Exit status 1 when something is stale.
"""

import argparse
import os
import re
import subprocess
import sys
import unicodedata
from fnmatch import fnmatch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IOS_WORKFLOW = ROOT / ".github/workflows/mobile-ios.yml"
NOTES = "mobile/TESTFLIGHT_WHATS_NEW.txt"
ROADMAP = "mobile/ROADMAP.md"
MANUAL = "docs/mobile/MANUAL.md"
DOCS = ["README.md", "docs/mobile/*.md", "mobile/*.md"]
SCREENSHOTS = ["docs/mobile/*.jpg", "docs/mobile/manual/*.jpg"]
NOTES_LIMIT = 4000

# Never in the published docs: the repository's and branch's old names, and
# private details (a phone number, a FlexRadio serial number).
FORBIDDEN = [
    (re.compile(r"OH6OKSA/AetherSDR\b"), "old repository name (now OH6OKSA/OksaSDR)"),
    (re.compile(r"aether-mobile-core"), "old branch name (now mobile/oksasdr)"),
    (re.compile(r"\+358[\s\d]{6,}"), "a phone number"),
    (re.compile(r"\b\d{4}-\d{4}-\d{4}-\d{4}\b"), "what looks like a radio serial number"),
]


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout


def app_patterns():
    """The paths that start an iOS build (mobile-ios.yml), plus Android."""
    patterns, in_paths = [], False
    for line in IOS_WORKFLOW.read_text().splitlines():
        stripped = line.strip()
        if stripped == "paths:":
            in_paths = True
            continue
        if in_paths:
            m = re.match(r"-\s*['\"]?([^'\"]+)['\"]?$", stripped)
            if m:
                patterns.append(m.group(1))
            elif stripped and not stripped.startswith("#"):
                in_paths = False
    return sorted(p for p in set(patterns) if not p.startswith(".github/")) + ["mobile/android/**"]


def matches(path, pattern):
    if pattern.endswith("/**"):
        return path.startswith(pattern[:-3] + "/")
    return fnmatch(path, pattern)


def default_base():
    for ref in ("@{u}", "origin/mobile/oksasdr"):
        try:
            return git("merge-base", ref, "HEAD").strip()
        except subprocess.CalledProcessError:
            continue
    return None


def github_slug(heading):
    text = re.sub(r"<[^>]+>", "", heading).strip().lower()
    text = "".join(c for c in text if c in " -_" or unicodedata.category(c)[0] in "LN")
    return text.replace(" ", "-")


def anchors_of(path):
    slugs, seen = set(), {}
    in_code = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.lstrip().startswith("```"):
            in_code = not in_code
        if in_code:
            continue
        m = re.match(r"#{1,6}\s+(.*?)\s*#*\s*$", line)
        if not m:
            continue
        slug = github_slug(m.group(1))
        n = seen.get(slug, 0)
        seen[slug] = n + 1
        slugs.add(slug if n == 0 else f"{slug}-{n}")
    return slugs


def doc_files():
    files = []
    for pattern in DOCS:
        files.extend(sorted(ROOT.glob(pattern)))
    return files


def check_links(errors):
    link = re.compile(r"!?\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)|(?:src|href)=\"([^\"]+)\"")
    for doc in doc_files():
        rel_doc = doc.relative_to(ROOT)
        in_code = False
        for number, line in enumerate(doc.read_text(encoding="utf-8").splitlines(), 1):
            if line.lstrip().startswith("```"):
                in_code = not in_code
            if in_code:
                continue
            for m in link.finditer(line):
                target = m.group(1) or m.group(2)
                if re.match(r"[a-z][a-z0-9+.-]*:", target, re.I):
                    continue  # http(s), mailto, ...
                path_part, _, anchor = target.partition("#")
                dest = (doc.parent / path_part).resolve() if path_part else doc
                if path_part and not dest.exists():
                    errors.append(f"{rel_doc}:{number}: broken link {target}")
                    continue
                if anchor and dest.suffix == ".md" and anchor not in anchors_of(dest):
                    errors.append(f"{rel_doc}:{number}: no heading for #{anchor} in {dest.relative_to(ROOT)}")


def check_forbidden(errors):
    for doc in doc_files() + [ROOT / NOTES]:
        for number, line in enumerate(doc.read_text(encoding="utf-8").splitlines(), 1):
            for pattern, what in FORBIDDEN:
                if pattern.search(line):
                    errors.append(f"{doc.relative_to(ROOT)}:{number}: {what}")


def check_notes(errors):
    text = (ROOT / NOTES).read_text(encoding="utf-8")
    if len(text) > NOTES_LIMIT:
        errors.append(f"{NOTES}: {len(text)} characters, TestFlight takes {NOTES_LIMIT}")
    if re.search(r"[äöåÄÖÅ]", text):
        errors.append(f"{NOTES}: Finnish letters; the notes for testers are in English")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--base", help="first commit not to check (default: the upstream branch)")
    parser.add_argument("--head", default="HEAD")
    args = parser.parse_args()

    errors, notes = [], []
    base = args.base or default_base()
    if base and set(base) == {"0"}:
        base = default_base()

    if base:
        changed = [p for p in git("diff", "--name-only", f"{base}..{args.head}").splitlines() if p]
        messages = git("log", "--format=%B", f"{base}..{args.head}")
        waived = {m.group(1).lower() for m in re.finditer(r"^(Manual|Screenshots|Roadmap|Notes):\s*unchanged",
                                                           messages, re.M | re.I)}
        app = [p for p in changed if any(matches(p, pat) for pat in app_patterns())]
        ui = [p for p in changed if p.startswith("mobile/qml/")]

        def need(rule, ok, message):
            if ok:
                return
            if rule in waived:
                notes.append(f"{message} (waived: \"{rule.capitalize()}: unchanged\")")
            else:
                errors.append(message)

        if app:
            need("notes", NOTES in changed, f"the app changed but {NOTES} did not (testers' What to Test)")
            need("roadmap", ROADMAP in changed, f"the app changed but {ROADMAP} did not")
        if ui:
            need("manual", MANUAL in changed, f"the UI changed (mobile/qml) but {MANUAL} did not")
            shots = any(any(fnmatch(p, s) for s in SCREENSHOTS) for p in changed)
            need("screenshots", shots,
                 "the UI changed but no screenshot did: mobile/tools/update-docs.sh RADIO_IP")
        span = f"{base[:8]}..{args.head}"
        print(f"docs-check: {span}: {len(changed)} files, {len(app)} app, {len(ui)} UI")
    else:
        print("docs-check: no base to compare with; checking the docs only")

    check_notes(errors)
    check_links(errors)
    check_forbidden(errors)

    for note in notes:
        print(f"  note: {note}")
    for error in errors:
        print(f"  STALE: {error}")
    if errors:
        print("docs-check: fix the above, or waive a rule with a commit trailer (see --help).")
        return 1
    print("docs-check: docs are up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
