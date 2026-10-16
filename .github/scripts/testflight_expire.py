#!/usr/bin/env python3
"""Expire old TestFlight builds via the App Store Connect API.

Env: API_KEY_ID, API_ISSUER_ID, API_KEY_P8 (key contents), BUNDLE_ID,
KEEP_FROM (lowest build number to keep), DRY_RUN ("true" lists only).
Every unexpired build numbered below KEEP_FROM is expired: testers can no
longer install it. Apple keeps the build record; this cannot be undone.
"""
import os
import sys

from testflight_whats_new import call


def main() -> int:
    keep_from = int(os.environ["KEEP_FROM"])
    dry_run = os.environ.get("DRY_RUN", "true") == "true"

    apps = call("GET", "/apps", query={"filter[bundleId]": os.environ["BUNDLE_ID"]})["data"]
    if not apps:
        print("app not found for the bundle id", file=sys.stderr)
        return 1

    builds = []
    query = {"filter[app]": apps[0]["id"], "filter[expired]": "false", "limit": "200"}
    page = call("GET", "/builds", query=query)
    while True:
        builds += page["data"]
        nxt = page.get("links", {}).get("next")
        if not nxt:
            break
        page = call("GET", nxt.split("/v1", 1)[1])

    old, kept = [], []
    for build in builds:
        version = build["attributes"]["version"]
        if not version.isdigit():
            print(f"skipping build with non-numeric version {version!r}")
        elif int(version) < keep_from:
            old.append((int(version), build["id"]))
        else:
            kept.append(int(version))
    old.sort()
    kept.sort()
    print(f"unexpired builds kept: {kept}")
    print(f"to expire (below {keep_from}): {[v for v, _ in old]}")
    if dry_run:
        print("dry run: nothing expired")
        return 0
    for version, build_id in old:
        call("PATCH", f"/builds/{build_id}", {"data": {
            "type": "builds", "id": build_id, "attributes": {"expired": True}}})
        print(f"expired build {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
