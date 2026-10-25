#!/usr/bin/env python3
"""Save public dashboard evidence without evaluating JavaScript or JSONP.

This collects names, bit addresses and server samples. It does not manufacture
numeric packet dispatch IDs, source formulas, signedness, units or a protocol.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import urllib.parse
import urllib.request


def jsonp(body):
    match = re.fullmatch(r"[A-Za-z_$][\w$]*\s*\((.*)\)\s*;?\s*", body, re.S)
    if not match:
        raise ValueError("Expected JSONP callback containing one JSON object")
    return json.loads(match[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--base", default="http://119.45.229.166")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"collected_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "satellite": "BY-04uv", "requests": [],
                "limitation": "Dashboard samples are not a complete wire dictionary"}

    def fetch(name, path, decode=False):
        url = args.base.rstrip("/") + path
        record = {"url": url, "file": name}
        manifest["requests"].append(record)
        try:
            with urllib.request.urlopen(url, timeout=20) as response:
                body = response.read()
                record.update(status=response.status, headers=dict(response.headers),
                              sha256=hashlib.sha256(body).hexdigest())
            (args.output / name).write_bytes(body)
            return jsonp(body.decode("utf-8")) if decode else None
        except Exception as error:
            record["error"] = str(error)
            raise

    def api(kind, code=None):
        params = {"type": kind, "sat": "BY-04uv", "callbackfunc": "data_callback"}
        if code:
            params["code"] = code
        return "/get_data?" + urllib.parse.urlencode(params)

    try:
        fetch("page.html", "/pages/telemetry-BY04uv.html")
        fetch("table.js", "/js/table.js?v=20260826-1415")
        kinds = fetch("all_sys.jsonp", api("all_sys"), True)
        fetch("table.jsonp", api("table"), True)
        for code in ("OBC-SF", "BUV-S0"):
            fetch(code + ".jsonp", api("system", code), True)
        print("Saved", len(kinds["data"]["BY-04uv"]), "dashboard packet names")
    finally:
        (args.output / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
