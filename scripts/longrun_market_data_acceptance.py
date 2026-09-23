#!/usr/bin/env python3
"""PPTrader 真实行情长稳验收：持续采集 TDX 与腾讯全市场公开行情。"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import statistics
import sys
import time
from urllib.request import Request, urlopen
from zoneinfo import ZoneInfo

from probe_mootdx_snapshot import load_symbols as load_tdx_symbols
from probe_mootdx_snapshot import run_probe as run_tdx_probe
from probe_tencent_snapshot import QUOTE_LINE, QUOTE_URL
from probe_tencent_snapshot import load_symbols as load_tencent_symbols
from probe_tencent_snapshot import provider_code


SHANGHAI = ZoneInfo("Asia/Shanghai")


def percentile(values: list[float], ratio: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, math.ceil(len(ordered) * ratio) - 1))
    return round(ordered[index], 3)


def summary(values: list[float]) -> dict:
    if not values:
        return {"count": 0, "min": None, "mean": None, "p50": None,
                "p95": None, "max": None}
    return {
        "count": len(values),
        "min": round(min(values), 3),
        "mean": round(statistics.fmean(values), 3),
        "p50": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "max": round(max(values), 3),
    }


def local_datetime(value: str) -> datetime:
    parsed = datetime.fromisoformat(value)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=SHANGHAI)
    return parsed.astimezone(SHANGHAI)


def session_name(now: datetime) -> str:
    hhmm = now.hour * 100 + now.minute
    if 925 <= hhmm < 930:
        return "auction"
    if 930 <= hhmm <= 1130:
        return "morning"
    if 1130 < hhmm < 1300:
        return "lunch"
    if 1300 <= hhmm <= 1500:
        return "afternoon"
    if 1500 < hhmm <= 1530:
        return "after_hours"
    return "closed"


def fetch_tencent_round(symbols: list[str], batch_size: int,
                         request_timeout: float, round_timeout: float) -> dict:
    started = time.perf_counter()
    deadline = started + round_timeout
    parsed: set[str] = set()
    received: set[str] = set()
    latencies: list[float] = []
    quote_times: list[datetime] = []
    errors: list[dict] = []
    payload_bytes = 0

    for offset in range(0, len(symbols), batch_size):
        if time.perf_counter() >= deadline:
            errors.append({"batch": offset // batch_size,
                           "error": "round timeout"})
            break
        chunk = symbols[offset:offset + batch_size]
        url = QUOTE_URL + ",".join(provider_code(symbol) for symbol in chunk)
        request = Request(url, headers={
            "User-Agent": "PPTrader/0.1 longrun-acceptance",
            "Referer": "https://gu.qq.com/",
            "Accept": "*/*",
        })
        batch_started = time.perf_counter()
        try:
            with urlopen(request, timeout=request_timeout) as response:
                payload = response.read()
            latencies.append((time.perf_counter() - batch_started) * 1000.0)
            payload_bytes += len(payload)
            for match in QUOTE_LINE.finditer(payload):
                symbol = match.group(1).decode("ascii")
                fields = match.group(2).split(b"~")
                parsed.add(symbol)
                if len(fields) <= 35 or not fields[3] or fields[3] == b"0.00":
                    continue
                received.add(symbol)
                if len(fields) > 30:
                    try:
                        local = datetime.strptime(
                            fields[30].decode("ascii"), "%Y%m%d%H%M%S")
                        quote_times.append(local.replace(tzinfo=SHANGHAI))
                    except (UnicodeDecodeError, ValueError):
                        pass
        except Exception as error:  # noqa: BLE001 - 验收必须记录并继续
            errors.append({"batch": offset // batch_size,
                           "error": str(error)})

    completed = datetime.now(timezone.utc)
    latest_quote = max(quote_times) if quote_times else None
    lag_ms = ((completed - latest_quote.astimezone(timezone.utc)).total_seconds()
              * 1000.0) if latest_quote else None
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    return {
        "provider": "tencent_public_quote",
        "requested": len(symbols),
        "parsed": len(parsed),
        "received": len(received),
        "completeness": round(len(received) / len(symbols), 6) if symbols else 0.0,
        "elapsed_ms": round(elapsed_ms, 3),
        "batch_latency_ms": summary(latencies),
        "latest_quote_time": latest_quote.isoformat() if latest_quote else None,
        "latest_quote_lag_ms": round(lag_ms, 3) if lag_ms is not None else None,
        "payload_bytes": payload_bytes,
        "errors": errors,
    }


def aggregate(rounds: list[dict], provider: str) -> dict:
    results = [row[provider] for row in rounds if provider in row]
    return {
        "rounds": len(results),
        "successful_rounds": sum(1 for row in results if not row.get("errors")),
        "error_rounds": sum(1 for row in results if row.get("errors")),
        "completeness": summary([float(row["completeness"]) for row in results]),
        "elapsed_ms": summary([float(row.get("source_ms") or row.get("elapsed_ms"))
                               for row in results
                               if row.get("source_ms") is not None
                               or row.get("elapsed_ms") is not None]),
        "received": summary([float(row["received"]) for row in results]),
        "quote_lag_ms": summary([float(row["latest_quote_lag_ms"])
                                 for row in results
                                 if row.get("latest_quote_lag_ms") is not None]),
    }


def write_json_line(handle, value: dict) -> None:
    handle.write(json.dumps(value, ensure_ascii=False) + "\n")
    handle.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description="真实行情至少四小时长稳验收")
    parser.add_argument("--cache", type=Path, default=Path("data/astock_cache.json"))
    parser.add_argument("--start-at", required=True,
                        help="Asia/Shanghai ISO 时间，例如 2026-08-24T09:25:00+08:00")
    parser.add_argument("--end-at", required=True,
                        help="Asia/Shanghai ISO 时间，例如 2026-08-24T15:05:00+08:00")
    parser.add_argument("--interval", type=float, default=60.0)
    parser.add_argument("--tdx-count", type=int, default=5000)
    parser.add_argument("--tdx-batch-size", type=int, default=40)
    parser.add_argument("--tdx-timeout", type=int, default=120)
    parser.add_argument("--tencent-count", type=int, default=5207)
    parser.add_argument("--tencent-batch-size", type=int, default=80)
    parser.add_argument("--tencent-request-timeout", type=float, default=5.0)
    parser.add_argument("--tencent-round-timeout", type=float, default=50.0)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    start_at = local_datetime(args.start_at)
    end_at = local_datetime(args.end_at)
    if end_at <= start_at:
        parser.error("end-at must be later than start-at")
    if args.interval < 30.0:
        parser.error("interval must be at least 30 seconds")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    jsonl_path = args.output_dir / "rounds.jsonl"
    summary_path = args.output_dir / "summary.json"
    metadata_path = args.output_dir / "metadata.json"
    tdx_universe = load_tdx_symbols(args.cache)
    tencent_universe = load_tencent_symbols(args.cache)
    tdx_symbols = tdx_universe[:min(args.tdx_count, len(tdx_universe))]
    tencent_symbols = tencent_universe[:min(args.tencent_count, len(tencent_universe))]
    metadata = {
        "pid": os.getpid(),
        "started_process_at": datetime.now(timezone.utc).isoformat(),
        "start_at": start_at.isoformat(),
        "end_at": end_at.isoformat(),
        "interval_seconds": args.interval,
        "tdx_requested": len(tdx_symbols),
        "tencent_requested": len(tencent_symbols),
        "cache": str(args.cache),
    }
    metadata_path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2),
                             encoding="utf-8")

    while datetime.now(SHANGHAI) < start_at:
        time.sleep(min(30.0, max(1.0, (start_at - datetime.now(SHANGHAI)).total_seconds())))

    rounds: list[dict] = []
    sequence = 0
    next_round = time.monotonic()
    with jsonl_path.open("a", encoding="utf-8", buffering=1) as output:
        while datetime.now(SHANGHAI) <= end_at:
            sequence += 1
            round_started = datetime.now(SHANGHAI)
            tdx = run_tdx_probe(
                Path(__file__).with_name("mootdx_bridge.py"), tdx_symbols,
                args.tdx_batch_size, args.tdx_timeout, repeats=1, interval=0.0)
            tencent = fetch_tencent_round(
                tencent_symbols, args.tencent_batch_size,
                args.tencent_request_timeout, args.tencent_round_timeout)
            row = {
                "sequence": sequence,
                "started_at": round_started.isoformat(),
                "completed_at": datetime.now(SHANGHAI).isoformat(),
                "session": session_name(round_started),
                "tdx": tdx,
                "tencent": tencent,
            }
            rounds.append(row)
            write_json_line(output, row)
            next_round += args.interval
            while time.monotonic() < next_round and datetime.now(SHANGHAI) <= end_at:
                time.sleep(min(1.0, next_round - time.monotonic()))

    final = {
        "completed_at": datetime.now(SHANGHAI).isoformat(),
        "scheduled_start": start_at.isoformat(),
        "scheduled_end": end_at.isoformat(),
        "round_count": len(rounds),
        "session_rounds": {
            name: sum(1 for row in rounds if row["session"] == name)
            for name in ("auction", "morning", "lunch", "afternoon",
                         "after_hours", "closed")
        },
        "tdx": aggregate(rounds, "tdx"),
        "tencent": aggregate(rounds, "tencent"),
    }
    summary_path.write_text(json.dumps(final, ensure_ascii=False, indent=2),
                            encoding="utf-8")
    print(json.dumps(final, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
