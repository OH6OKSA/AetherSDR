#!/usr/bin/env python3
"""Set TestFlight "What to Test" for one build via the App Store Connect API.

Env: API_KEY_ID, API_ISSUER_ID, API_KEY_P8 (key contents), BUNDLE_ID,
BUILD_NUMBER, NOTES_FILE. Waits for the uploaded build to appear, then
creates or updates its beta build localization in the app's primary locale.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

import jwt  # PyJWT

API = "https://api.appstoreconnect.apple.com/v1"
WAIT_SECONDS = 45 * 60
POLL_SECONDS = 30
MAX_CHARS = 4000


def token() -> str:
    now = int(time.time())
    return jwt.encode(
        {"iss": os.environ["API_ISSUER_ID"], "iat": now, "exp": now + 15 * 60,
         "aud": "appstoreconnect-v1"},
        os.environ["API_KEY_P8"],
        algorithm="ES256",
        headers={"kid": os.environ["API_KEY_ID"], "typ": "JWT"},
    )


def call(method: str, path: str, body=None, query=None):
    url = API + path
    if query:
        url += "?" + urllib.parse.urlencode(query)
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": "Bearer " + token(),
        "Content-Type": "application/json",
    })
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read()
            return json.loads(raw) if raw else {}
    except urllib.error.HTTPError as err:
        detail = err.read().decode(errors="replace")[:500]
        raise RuntimeError(f"{method} {path}: HTTP {err.code}: {detail}") from None


def main() -> int:
    notes = open(os.environ["NOTES_FILE"], encoding="utf-8").read().strip()
    if len(notes) > MAX_CHARS:
        print(f"notes are {len(notes)} characters; TestFlight allows {MAX_CHARS}", file=sys.stderr)
        return 1
    build_number = os.environ["BUILD_NUMBER"]

    apps = call("GET", "/apps", query={"filter[bundleId]": os.environ["BUNDLE_ID"]})["data"]
    if not apps:
        print("app not found for the bundle id", file=sys.stderr)
        return 1
    app = apps[0]
    locale = app["attributes"].get("primaryLocale") or "en-US"

    deadline = time.time() + WAIT_SECONDS
    build = None
    while time.time() < deadline:
        found = call("GET", "/builds", query={
            "filter[app]": app["id"], "filter[version]": build_number, "limit": "1",
        })["data"]
        if found:
            build = found[0]
            break
        print(f"build {build_number} not in App Store Connect yet; waiting")
        time.sleep(POLL_SECONDS)
    if build is None:
        print(f"build {build_number} did not appear within {WAIT_SECONDS // 60} min", file=sys.stderr)
        return 1

    existing = call("GET", f"/builds/{build['id']}/betaBuildLocalizations")["data"]
    match = next((l for l in existing if l["attributes"].get("locale") == locale), None)
    if match:
        call("PATCH", f"/betaBuildLocalizations/{match['id']}", {"data": {
            "type": "betaBuildLocalizations", "id": match["id"],
            "attributes": {"whatsNew": notes}}})
        print(f"updated What to Test for build {build_number} ({locale})")
    else:
        call("POST", "/betaBuildLocalizations", {"data": {
            "type": "betaBuildLocalizations",
            "attributes": {"locale": locale, "whatsNew": notes},
            "relationships": {"build": {"data": {"type": "builds", "id": build["id"]}}}}})
        print(f"set What to Test for build {build_number} ({locale})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
