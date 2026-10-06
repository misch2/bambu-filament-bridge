#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Opt-in real-printer check. --write-json or --clear changes the selected slot."""
import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path


def main():
    if os.environ.get("BFB_LIVE_TEST") != "1":
        raise RuntimeError("Set BFB_LIVE_TEST=1 explicitly; write mode changes printer state")
    parser = argparse.ArgumentParser(description=__doc__)
    operations = parser.add_mutually_exclusive_group()
    operations.add_argument("--write-json", type=Path)
    operations.add_argument("--clear", action="store_true", help="Clear selected slot metadata")
    parser.add_argument("--ams", type=int)
    parser.add_argument("--tray", type=int)
    args = parser.parse_args()
    base = os.environ["BAMBU_BRIDGE_URL"].rstrip("/")
    token = os.environ["BAMBU_BRIDGE_TOKEN"]

    def call(path, payload=None, authenticated=True, method=None):
        headers = {"Authorization": "Bearer " + token} if authenticated else {}
        data = None if payload is None else json.dumps(payload).encode()
        if data is not None:
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(base + path, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=12) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            # Never print the raw response or credentials.
            if path == "/health" and error.code == 503:
                return error.code, json.load(error)
            raise RuntimeError(f"Bridge request failed: HTTP {error.code}") from None

    status, health = call("/health", authenticated=False)
    if status not in (200, 503) or not isinstance(health.get("ready"), bool):
        raise RuntimeError("Invalid health response")
    _, capabilities = call("/api/v1/capabilities")
    if capabilities.get("apiVersion") != 1:
        raise RuntimeError("Unexpected API version")
    print(f"Health HTTP {status}; ready={health['ready']}; capabilities OK")
    if args.write_json or args.clear:
        if args.ams is None or args.tray is None or not (0 <= args.ams <= 255 and 0 <= args.tray <= 3):
            parser.error(
                "write mode requires --ams 0..255 and --tray 0..3 "
                "(external: --ams 254/255 --tray 0)"
            )
        if not health["ready"]:
            raise RuntimeError("Printer is not ready; no write attempted")
        print("Changing selected AMS tray or external holder metadata")
        payload = None if args.clear else json.loads(args.write_json.read_text(encoding="utf-8"))
        _, result = call(f"/api/v1/ams/{args.ams}/trays/{args.tray}/filament", payload, method="DELETE" if args.clear else "POST")
        if result.get("status") != ("cleared" if args.clear else "synced") or result.get("verified") is not True:
            raise RuntimeError("Write was not verified")
        print("Write verified")


if __name__ == "__main__":
    try:
        main()
    except (KeyError, ValueError, OSError, RuntimeError):
        print("Live smoke failed: check opt-in, options, environment and bridge state", file=sys.stderr)
        sys.exit(1)
