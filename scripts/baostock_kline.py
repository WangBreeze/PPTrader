#!/usr/bin/env python3
"""BaoStock A股日线桥：显式请求、JSON Lines 输出，不记录本地凭据。"""

import argparse
from datetime import date, timedelta
import json
import sys


def emit(value):
    print(json.dumps(value, ensure_ascii=False), flush=True)


def provider_code(symbol):
    if symbol.startswith(("6", "9", "68")):
        return f"sh.{symbol}"
    if symbol.startswith(("4", "8")):
        return f"bj.{symbol}"
    return f"sz.{symbol}"


def main():
    parser = argparse.ArgumentParser(description="BaoStock A股日线桥")
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--freq", required=True, choices=("day",))
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--adjustment", default="none",
                        choices=("none", "qfq", "hfq"))
    args = parser.parse_args()
    if not 1 <= args.count <= 5000:
        emit({"error": "count must be between 1 and 5000"})
        return 2
    try:
        import baostock as bs
    except ImportError:
        emit({"error": "缺少 baostock；请安装 requirements.txt"})
        return 2

    login = bs.login()
    if login.error_code != "0":
        emit({"error": f"BaoStock 登录失败: {login.error_msg}"})
        return 1
    adjustment_flag = {"none": "3", "qfq": "2", "hfq": "1"}[args.adjustment]
    # 交易日约占自然日的 5/7；额外留出节假日和停牌余量，避免每次扫描全历史。
    lookback_days = max(45, int(args.count * 1.75) + 30)
    start_date = (date.today() - timedelta(days=lookback_days)).isoformat()
    try:
        result = bs.query_history_k_data_plus(
            provider_code(args.symbol),
            "date,open,high,low,close,volume,tradestatus",
            start_date=start_date, frequency="d",
            adjustflag=adjustment_flag)
        if result.error_code != "0":
            emit({"error": f"BaoStock 查询失败: {result.error_msg}"})
            return 1
        rows = []
        while result.next():
            row = dict(zip(result.fields, result.get_row_data()))
            if row.get("tradestatus") not in ("", "1"):
                continue
            try:
                rows.append({
                    "datetime": row["date"],
                    "open": float(row["open"]),
                    "high": float(row["high"]),
                    "low": float(row["low"]),
                    "close": float(row["close"]),
                    "volume": float(row["volume"] or 0),
                    "source": "baostock",
                    "adjustment": args.adjustment,
                })
            except (KeyError, TypeError, ValueError):
                continue
        rows = rows[-args.count:]
        if not rows:
            emit({"error": f"BaoStock 未返回 {args.symbol} 的有效日线"})
            return 1
        emit({"status": "ready", "provider": "baostock", "count": len(rows)})
        for row in rows:
            emit(row)
        emit({"status": "done"})
        return 0
    finally:
        bs.logout()


if __name__ == "__main__":
    raise SystemExit(main())
