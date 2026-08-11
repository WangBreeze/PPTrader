#!/usr/bin/env python3
"""PPTrader mootdx 行情桥接 — 定时轮询 mootdx 并以 JSON 行输出到 stdout。"""
import argparse
from datetime import datetime, timezone
import json
import signal
import sys
import time

from mootdx.quotes import Quotes
from mootdx.server import server as discover_servers


PREFERRED_SERVERS = [
    ("202.108.253.139", 80),
    ("180.153.18.172", 80),
    ("123.125.108.14", 7709),
]


def build_tick(row):
    """将 DataFrame 行转换为与 PPTrader Tick 结构一致的 JSON 字典。"""
    code = str(row.get("code", "") or "")
    if not code:
        return None

    price = float(row.get("price", 0) or 0)
    last_close = float(row.get("last_close", 0) or 0)
    change_pct = ((price - last_close) / last_close * 100.0) if last_close > 0 else 0.0

    return {
        "symbol": code,
        "price": price,
        "volume": float(row.get("volume", 0) or 0),
        "bid": float(row.get("bid1", 0) or 0),
        "ask": float(row.get("ask1", 0) or 0),
        "open": float(row.get("open", 0) or 0),
        "high": float(row.get("high", 0) or 0),
        "low": float(row.get("low", 0) or 0),
        "pre_close": last_close,
        "amount": float(row.get("amount", 0) or 0),
        "change_pct": round(change_pct, 2),
    }


def emit(obj):
    """输出 JSON 行并立即刷新。"""
    print(json.dumps(obj, ensure_ascii=False), flush=True)


def parse_server(value):
    if not value:
        return None
    host, separator, port = value.rpartition(":")
    if not separator or not host or not port.isdigit():
        raise ValueError("服务器格式必须为 host:port")
    return host, int(port)


def connect_client(symbols, configured_server=None):
    candidates = [configured_server] if configured_server else list(PREFERRED_SERVERS)
    attempted = set()
    discovery_attempted = False
    sample = symbols[:10]

    while candidates:
        endpoint = candidates.pop(0)
        if endpoint in attempted:
            continue
        attempted.add(endpoint)
        try:
            client = Quotes.factory(market="std", server=endpoint, timeout=10)
            frame = client.quotes(symbol=sample)
            if frame is not None and not frame.empty:
                return client, endpoint
        except Exception:
            pass

        if not candidates and not configured_server and not discovery_attempted:
            discovery_attempted = True
            try:
                candidates.extend(discover_servers("HQ", sync=False)[:10])
            except Exception:
                pass

    return None, None


def main():
    parser = argparse.ArgumentParser(description="mootdx 行情桥接")
    symbol_group = parser.add_mutually_exclusive_group(required=True)
    symbol_group.add_argument("--symbols", help="逗号分隔的股票代码")
    symbol_group.add_argument("--symbols-file", help="每行一个股票代码的 UTF-8 文件")
    parser.add_argument("--interval", type=float, default=3.0, help="轮询间隔（秒）")
    parser.add_argument("--batch-size", type=int, default=40, help="单次行情请求的股票数量")
    parser.add_argument("--server", help="固定 TDX 服务器，格式为 host:port")
    parser.add_argument("--max-batches", type=int, default=0,
                        help="完成指定批次数后退出，0 表示持续运行")
    args = parser.parse_args()

    if args.symbols_file:
        try:
            with open(args.symbols_file, "r", encoding="utf-8") as symbol_file:
                symbols = [line.strip() for line in symbol_file if line.strip()]
        except OSError as exc:
            emit({"error": f"failed to read symbols file: {exc}"})
            sys.exit(1)
    else:
        symbols = [s.strip() for s in args.symbols.split(",") if s.strip()]
    symbols = list(dict.fromkeys(symbols))
    if not symbols:
        emit({"error": "没有提供股票代码"})
        sys.exit(1)
    batch_size = max(1, args.batch_size)

    try:
        configured_server = parse_server(args.server)
    except ValueError as exc:
        emit({"error": str(exc)})
        sys.exit(1)

    emit({"status": f"connecting to TDX servers for {len(symbols)} symbols..."})
    client, endpoint = connect_client(symbols, configured_server)
    if client is None:
        emit({"error": "没有找到能返回实时行情的 TDX 服务器"})
        sys.exit(1)

    emit({"status": f"connected, polling {len(symbols)} symbols",
          "server": f"{endpoint[0]}:{endpoint[1]}"})

    # Ignore SIGTERM from QProcess::terminate()
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    completed_batches = 0
    while True:
        captured_at = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        batch_id = f"tdx-{time.time_ns()}"
        emit({"event": "snapshot_begin", "batch_id": batch_id,
              "timestamp": captured_at, "expected": len(symbols)})
        received = set()
        for offset in range(0, len(symbols), batch_size):
            chunk = symbols[offset:offset + batch_size]
            try:
                df = client.quotes(symbol=chunk)
            except Exception as exc:
                emit({"error": f"quotes() failed for chunk {offset // batch_size}: {exc}",
                      "batch_id": batch_id})
                continue
            if df is None or df.empty:
                continue
            for _, row in df.iterrows():
                tick = build_tick(row)
                if tick is None or tick["price"] <= 0:
                    continue
                tick["event"] = "quote"
                tick["batch_id"] = batch_id
                tick["timestamp"] = captured_at
                received.add(tick["symbol"])
                emit(tick)
        completed_at = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        emit({"event": "snapshot_end", "batch_id": batch_id,
              "timestamp": captured_at, "completed_at": completed_at,
              "expected": len(symbols),
              "received": len(received)})

        completed_batches += 1
        if args.max_batches > 0 and completed_batches >= args.max_batches:
            break

        time.sleep(args.interval)


if __name__ == "__main__":
    main()
