#!/usr/bin/env python3
"""受控探测腾讯公开报价端点的全市场批量能力。"""

import argparse
import json
import re
import time
from pathlib import Path
from urllib.request import Request, urlopen


QUOTE_URL = "https://qt.gtimg.cn/q="
QUOTE_LINE = re.compile(rb'v_(?:sh|sz)(\d{6})="([^"]*)";')


def load_symbols(cache_path):
    rows = json.loads(Path(cache_path).read_text(encoding="utf-8"))
    symbols = []
    for row in rows:
        code = str(row.get("code", "")).strip()
        market = str(row.get("market", "")).strip()
        if len(code) != 6 or not code.isdigit():
            continue
        if market and not any(label in market for label in ("上海", "深圳", "上交", "深交", "SSE", "SZSE")):
            continue
        if code.startswith(("600", "601", "603", "605", "688",
                            "000", "001", "002", "003", "300", "301")):
            symbols.append(code)
    return list(dict.fromkeys(symbols))


def provider_code(symbol):
    return ("sh" if symbol.startswith(("600", "601", "603", "605", "688")) else "sz") + symbol


def parse_received(payload):
    received = set()
    parsed = set()
    for match in QUOTE_LINE.finditer(payload):
        parsed.add(match.group(1).decode("ascii"))
        fields = match.group(2).split(b"~")
        if len(fields) > 35 and fields[3] and fields[3] != b"0.00":
            received.add(match.group(1).decode("ascii"))
    return parsed, received


def fetch_chunk(symbols, timeout):
    url = QUOTE_URL + ",".join(provider_code(symbol) for symbol in symbols)
    request = Request(url, headers={
        "User-Agent": "PPTrader/0.1 realtime-capability-probe",
        "Referer": "https://gu.qq.com/",
        "Accept": "*/*",
    })
    started = time.perf_counter()
    with urlopen(request, timeout=timeout) as response:
        payload = response.read()
    parsed, received = parse_received(payload)
    return parsed, received, (time.perf_counter() - started) * 1000.0, len(payload)


def main():
    parser = argparse.ArgumentParser(description="腾讯公开报价批量能力探测")
    parser.add_argument("--cache", default="data/astock_cache.json")
    parser.add_argument("--count", type=int, default=200)
    parser.add_argument("--batch-size", type=int, default=80)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--pause", type=float, default=0.05)
    args = parser.parse_args()

    universe = load_symbols(args.cache)
    symbols = universe[:max(0, min(args.count, len(universe)))]
    received = set()
    parsed = set()
    errors = []
    latencies = []
    total_bytes = 0
    started = time.perf_counter()
    batch_size = max(1, args.batch_size)
    for offset in range(0, len(symbols), batch_size):
        chunk = symbols[offset:offset + batch_size]
        try:
            chunk_parsed, chunk_received, latency, payload_size = fetch_chunk(chunk, args.timeout)
            parsed.update(chunk_parsed)
            received.update(chunk_received)
            latencies.append(latency)
            total_bytes += payload_size
        except Exception as exc:  # 探测工具必须报告失败批次而不是中断统计
            errors.append({"batch": offset // batch_size, "error": str(exc)})
        if args.pause > 0 and offset + batch_size < len(symbols):
            time.sleep(args.pause)

    elapsed = (time.perf_counter() - started) * 1000.0
    ordered = sorted(latencies)
    p95_index = max(0, min(len(ordered) - 1, int(len(ordered) * 0.95))) if ordered else 0
    result = {
        "provider": "tencent_public_quote",
        "available": len(universe),
        "requested": len(symbols),
        "parsed": len(parsed),
        "received": len(received),
        "completeness": round(len(received) / len(symbols), 4) if symbols else 0.0,
        "batch_size": batch_size,
        "batches": (len(symbols) + batch_size - 1) // batch_size,
        "elapsed_ms": round(elapsed, 1),
        "mean_batch_ms": round(sum(latencies) / len(latencies), 1) if latencies else None,
        "p95_batch_ms": round(ordered[p95_index], 1) if ordered else None,
        "payload_bytes": total_bytes,
        "missing_sample": [symbol for symbol in symbols if symbol not in received][:10],
        "errors": errors,
    }
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
