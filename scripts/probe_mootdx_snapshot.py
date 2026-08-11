#!/usr/bin/env python3
"""对 mootdx 全市场快照协议执行受控的一次性容量探测。"""

import argparse
from datetime import datetime
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


EXCLUDED_NAME_MARKERS = ("B股", "指数", "基金", "ETF", "债", "退")


def expected_market(code):
    if code.startswith(("600", "601", "603", "605", "688")):
        return "上海"
    if code.startswith(("000", "001", "002", "003", "300", "301")):
        return "深圳"
    if code.startswith(("43", "82", "83", "87", "88", "92")):
        return "北京"
    return None


def load_symbols(cache_path):
    records = json.loads(cache_path.read_text(encoding="utf-8"))
    symbols = []
    seen = set()
    for record in records:
        code = str(record.get("code", "")).strip()
        name = str(record.get("name", "")).replace("\x00", "").strip()
        market = str(record.get("market", "")).strip()
        if len(code) != 6 or not code.isdigit() or expected_market(code) is None:
            continue
        if expected_market(code) != market:
            continue
        if any(marker.lower() in name.lower() for marker in EXCLUDED_NAME_MARKERS):
            continue
        if code not in seen:
            seen.add(code)
            symbols.append(code)
    return symbols


def parse_iso(value):
    return datetime.fromisoformat(value) if value else None


def run_probe(bridge_path, symbols, batch_size, timeout_seconds, server=None,
              repeats=1, interval=0.0):
    symbols_file = tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", suffix=".txt", delete=False)
    try:
        symbols_file.write("\n".join(symbols) + "\n")
        symbols_file.close()
        command = [
            sys.executable,
            str(bridge_path),
            "--symbols-file",
            symbols_file.name,
            "--batch-size",
            str(batch_size),
            "--interval",
            str(interval),
            "--max-batches",
            str(repeats),
        ]
        if server:
            command.extend(["--server", server])
        started = time.perf_counter()
        try:
            process = subprocess.run(
                command,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout_seconds,
                check=False,
            )
        except subprocess.TimeoutExpired:
            return {
                "requested": len(symbols),
                "received": 0,
                "completeness": 0.0,
                "wall_ms": round((time.perf_counter() - started) * 1000),
                "timeout": True,
                "return_code": None,
                "repeats": repeats,
                "errors": [f"超过 {timeout_seconds} 秒"],
            }

        events = []
        parse_errors = 0
        for line in process.stdout.splitlines():
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                parse_errors += 1
        end_events = [event for event in events if event.get("event") == "snapshot_end"]
        begin_events = {
            event.get("batch_id"): event for event in events
            if event.get("event") == "snapshot_begin"
        }
        batch_results = []
        for end_event in end_events:
            begin_event = begin_events.get(end_event.get("batch_id"), {})
            started_at = parse_iso(begin_event.get("timestamp"))
            completed_at = parse_iso(end_event.get("completed_at"))
            batch_received = int(end_event.get("received", 0))
            batch_results.append({
                "received": batch_received,
                "completeness": round(batch_received / len(symbols), 4) if symbols else 0.0,
                "source_ms": round((completed_at - started_at).total_seconds() * 1000)
                if started_at and completed_at else None,
            })
        errors = [event.get("error") for event in events if event.get("error")]
        connected_event = next(
            (event for event in events if event.get("server")), {})
        if process.stderr.strip():
            errors.append(process.stderr.strip())
        last_result = batch_results[-1] if batch_results else {}
        source_times = [result["source_ms"] for result in batch_results
                        if result["source_ms"] is not None]
        received_values = [result["received"] for result in batch_results]
        return {
            "requested": len(symbols),
            "received": last_result.get("received", 0),
            "completeness": last_result.get("completeness", 0.0),
            "source_ms": last_result.get("source_ms"),
            "received_min": min(received_values) if received_values else 0,
            "source_ms_average": round(sum(source_times) / len(source_times))
            if source_times else None,
            "source_ms_max": max(source_times) if source_times else None,
            "wall_ms": round((time.perf_counter() - started) * 1000),
            "chunks": (len(symbols) + batch_size - 1) // batch_size,
            "batch_size": batch_size,
            "completed_batches": len(batch_results),
            "batch_results": batch_results,
            "server": connected_event.get("server"),
            "return_code": process.returncode,
            "parse_errors": parse_errors,
            "timeout": False,
            "errors": errors,
        }
    finally:
        Path(symbols_file.name).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description="PPTrader mootdx 批次容量探测")
    parser.add_argument("--cache", type=Path, default=Path("data/astock_cache.json"))
    parser.add_argument("--counts", default="10,100,500,1000,5000")
    parser.add_argument("--batch-size", type=int, default=40)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--server", help="固定 TDX 服务器，格式为 host:port")
    parser.add_argument("--repeats", type=int, default=1, help="同一连接连续采集批次数")
    parser.add_argument("--interval", type=float, default=0.0, help="批次间隔秒数")
    args = parser.parse_args()

    bridge_path = Path(__file__).with_name("mootdx_bridge.py")
    universe = load_symbols(args.cache)
    requested_counts = [int(value) for value in args.counts.split(",") if int(value) > 0]
    print(json.dumps({"event": "probe_start", "available": len(universe)}, ensure_ascii=False))
    for count in requested_counts:
        actual_count = min(count, len(universe))
        result = run_probe(
            bridge_path, universe[:actual_count], max(1, args.batch_size), args.timeout,
            args.server, max(1, args.repeats), max(0.0, args.interval))
        print(json.dumps(result, ensure_ascii=False), flush=True)
        if result["timeout"] or result.get("return_code") != 0:
            break


if __name__ == "__main__":
    main()
