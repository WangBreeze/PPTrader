#!/usr/bin/env python3
"""Tushare Pro A股日线桥：Token 仅从运行环境读取。"""

import argparse
import json
import os
import sys


def emit(value):
    print(json.dumps(value, ensure_ascii=False), flush=True)


def ts_code(symbol):
    if symbol.startswith(("4", "8")):
        return f"{symbol}.BJ"
    if symbol.startswith(("6", "9")):
        return f"{symbol}.SH"
    return f"{symbol}.SZ"


def main():
    parser = argparse.ArgumentParser(description="Tushare Pro A股日线桥")
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--freq", required=True, choices=("day",))
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--adjustment", default="none",
                        choices=("none", "qfq", "hfq"))
    args = parser.parse_args()
    token = os.environ.get("PPTRADER_TUSHARE_TOKEN", "").strip()
    if not token:
        emit({"error": "未配置 PPTRADER_TUSHARE_TOKEN"})
        return 2
    try:
        import tushare as ts
    except ImportError:
        emit({"error": "缺少 tushare；请安装 requirements.txt"})
        return 2

    try:
        if args.adjustment == "none":
            frame = ts.pro_api(token).daily(
                ts_code=ts_code(args.symbol), start_date="19900101")
        else:
            frame = ts.pro_bar(
                ts_code=ts_code(args.symbol), api=ts.pro_api(token),
                start_date="19900101", adj=args.adjustment)
    except Exception as error:
        emit({"error": f"Tushare 请求失败: {error}"})
        return 1
    if frame is None or frame.empty:
        emit({"error": f"Tushare 未返回 {args.symbol} 的有效日线"})
        return 1
    frame = frame.sort_values("trade_date").tail(max(1, min(args.count, 5000)))
    emit({"status": "ready", "provider": "tushare_pro", "count": len(frame)})
    for _, row in frame.iterrows():
        emit({
            "datetime": str(row["trade_date"]),
            "open": float(row["open"]),
            "high": float(row["high"]),
            "low": float(row["low"]),
            "close": float(row["close"]),
            "volume": float(row.get("vol", 0) or 0) * 100.0,
            "source": "tushare_pro",
            "adjustment": args.adjustment,
        })
    emit({"status": "done"})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

