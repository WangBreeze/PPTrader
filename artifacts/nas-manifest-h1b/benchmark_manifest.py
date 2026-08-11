#!/usr/bin/env python3
"""对显式 A 股代码清单执行只读 NAS manifest 分档基线。"""

from __future__ import annotations

import argparse
import json
import time
import urllib.request
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", default="http://127.0.0.1:28765")
    parser.add_argument("--date", required=True)
    parser.add_argument("--universe", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    universe = json.loads(Path(args.universe).read_text(encoding="utf-8"))
    shanghai_prefixes = {"600", "601", "603", "605", "688"}
    shenzhen_prefixes = {"000", "001", "002", "003", "300", "301"}
    symbols = [item["code"] for item in universe
               if ((item["code"][:3] in shanghai_prefixes
                    and "上海" in item.get("market", ""))
                   or (item["code"][:3] in shenzhen_prefixes
                       and "深圳" in item.get("market", "")))]
    symbols = list(dict.fromkeys(symbols))
    sizes = [1, 10, 100, 500, min(5207, len(symbols))]
    results: list[dict[str, object]] = []
    for size in sizes:
        body = json.dumps({
            "symbols": symbols[:size],
            "start": args.date,
            "end": args.date,
        }, separators=(",", ":")).encode("utf-8")
        request = urllib.request.Request(
            args.endpoint.rstrip("/") + "/v1/manifest",
            data=body,
            headers={"Content-Type": "application/json", "Accept": "application/json"},
            method="POST",
        )
        started = time.perf_counter()
        with urllib.request.urlopen(request, timeout=900) as response:
            payload = response.read()
            status = response.status
        elapsed_ms = round((time.perf_counter() - started) * 1000, 3)
        document = json.loads(payload)
        entries = document.get("entries", [])
        results.append({
            "requestedSymbols": size,
            "availableSymbols": document.get("availableSymbols", 0),
            "missingSymbols": document.get("missingSymbols", 0),
            "entries": len(entries),
            "sourceBytes": sum(int(entry.get("size", 0)) for entry in entries),
            "responseBytes": len(payload),
            "elapsedMilliseconds": elapsed_ms,
            "httpStatus": status,
        })
        print(json.dumps(results[-1], ensure_ascii=False), flush=True)

    report = {
        "date": args.date,
        "universeSymbols": len(symbols),
        "endpoint": "loopback-tunnel",
        "results": results,
    }
    Path(args.output).write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
