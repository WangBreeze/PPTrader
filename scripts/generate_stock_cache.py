#!/usr/bin/env python3
"""下载 A 股股票列表并保存为 JSON 缓存文件。"""
import argparse
import json
import sys
import os

import pandas

from mootdx.quotes import Quotes
from mootdx.utils import to_data

CACHE_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "astock_cache.json")

# 可交易 A 股代码前缀
STOCK_PREFIXES = frozenset({
    "600", "601", "603", "605",  # 上海主板
    "688",                       # 科创板
    "000", "001", "002", "003",  # 深圳主板
    "300", "301",                # 创业板
    "800", "801", "802", "803", "804", "805", "806", "807", "808", "809",  # 北交所
})


def is_tradable(code: str) -> bool:
    if len(code) != 6 or not code.isdigit():
        return False
    return code[:3] in STOCK_PREFIXES or code.startswith(("43", "82", "83", "87", "88", "92"))


def expected_market(code: str):
    if code.startswith(("600", "601", "603", "605", "688")):
        return "上海"
    if code.startswith(("000", "001", "002", "003", "300", "301")):
        return "深圳"
    if code.startswith(("43", "82", "83", "87", "88", "92")):
        return "北京"
    return None


def fetch_market_stocks(client, market_code):
    if market_code in (0, 1):
        return client.stocks(market=market_code)
    count = client.stock_count(market=market_code)
    frames = []
    for start in range(0, count, 1000):
        frame = to_data(client.client.get_security_list(market=market_code, start=start))
        if frame is not None and not frame.empty:
            frames.append(frame)
    return pandas.concat(frames, ignore_index=True) if frames else pandas.DataFrame()


def main():
    parser = argparse.ArgumentParser(description="刷新 PPTrader 全 A 股股票池缓存")
    parser.add_argument("--output", default=CACHE_PATH, help="输出 JSON 文件")
    args = parser.parse_args()
    output_path = os.path.abspath(args.output)
    print("Downloading A-share stock list from TDX...")
    client = Quotes.factory(market="std", bestip=False, timeout=15)

    stocks = []
    seen = set()

    for market_code, market_label in [(0, "深圳"), (1, "上海"), (2, "北京")]:
        print(f"  Market {market_label}...")
        df = fetch_market_stocks(client, market_code)
        for _, row in df.iterrows():
            code = str(row["code"]).strip().zfill(6)
            name = str(row["name"]).replace("\x00", "").strip()

            # 跳过无效数据
            if not code.isdigit() or len(code) != 6:
                continue
            if not name:
                continue
            if code in seen:
                continue

            normalized_market = expected_market(code)
            if is_tradable(code) and normalized_market == market_label:
                seen.add(code)
                stocks.append({
                    "code": code,
                    "name": name,
                    "market": normalized_market,
                })

    stocks.sort(key=lambda x: x["code"])

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(stocks, f, ensure_ascii=False, indent=2)

    print(f"Saved {len(stocks)} stocks to {output_path}")


if __name__ == "__main__":
    main()
