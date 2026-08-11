#!/usr/bin/env python3
"""PPTrader mootdx K线桥接 — 获取日线/分钟线并以 JSON 行输出到 stdout。"""
import argparse
import json
import sys

from mootdx.quotes import Quotes
from mootdx.server import server as discover_servers

# TDX frequency codes
FREQ_MAP = {
    "1m": 0,
    "5m": 1,
    "15m": 2,
    "30m": 3,
    "60m": 4,
    "day": 9,
}

# Prefer endpoints that answer both quote and K-line requests. If they become
# unavailable, mootdx performs a concurrent discovery pass as a fallback.
PREFERRED_SERVERS = [
    ("202.108.253.139", 80),
    ("180.153.18.172", 80),
    ("123.125.108.14", 7709),
]


def emit(obj):
    print(json.dumps(obj, ensure_ascii=False), flush=True)


def fetch_bars(symbol, frequency, count):
    candidates = list(PREFERRED_SERVERS)
    attempted = set()

    while candidates:
        endpoint = candidates.pop(0)
        if endpoint in attempted:
            continue
        attempted.add(endpoint)
        try:
            client = Quotes.factory(market="std", server=endpoint, timeout=5)
            frame = client.bars(
                symbol=symbol, frequency=frequency, start=0, offset=count
            )
            if frame is not None and not frame.empty:
                return frame, endpoint
        except Exception:
            continue

        if not candidates and len(attempted) == len(PREFERRED_SERVERS):
            try:
                candidates.extend(discover_servers("HQ", sync=False)[:10])
            except Exception:
                pass

    return None, None


def main():
    parser = argparse.ArgumentParser(description="mootdx K线桥接")
    parser.add_argument("--symbol", required=True, help="股票代码")
    parser.add_argument("--freq", default="day", help="K线周期: 1m,5m,15m,30m,60m,day")
    parser.add_argument("--count", type=int, default=200, help="获取数量")
    parser.add_argument("--adjustment", default="none", choices=("none",),
                        help="mootdx 兼容源只支持不复权")
    args = parser.parse_args()

    freq_code = FREQ_MAP.get(args.freq)
    if freq_code is None:
        emit({"error": f"unsupported frequency: {args.freq}"})
        sys.exit(1)

    df, endpoint = fetch_bars(args.symbol, freq_code, args.count)

    if df is None or df.empty:
        emit({"error": f"no kline data for {args.symbol}"})
        sys.exit(1)

    emit({
        "status": f"fetched {len(df)} kline bars for {args.symbol} freq={args.freq}",
        "server": f"{endpoint[0]}:{endpoint[1]}",
    })

    for _, row in df.iterrows():
        bar = {
            "symbol": args.symbol,
            "open": float(row.get("open", 0) or 0),
            "high": float(row.get("high", 0) or 0),
            "low": float(row.get("low", 0) or 0),
            "close": float(row.get("close", 0) or 0),
            # 通达信证券 K 线成交量以手表示；项目 Candle 统一保存股数。
            "volume": float(row.get("volume", 0) or 0) * 100.0,
            "amount": float(row.get("amount", 0) or 0),
            "source": "mootdx",
            "adjustment": "none",
        }
        if "datetime" in df.columns:
            bar["datetime"] = str(row["datetime"])
        elif "date" in df.columns:
            bar["datetime"] = str(row["date"])
        emit(bar)

    emit({"status": "done"})


if __name__ == "__main__":
    main()
