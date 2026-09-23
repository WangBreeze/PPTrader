#!/usr/bin/env python3
"""Build a frozen main-board limit-up candidate plan from read-only Tick CSVs."""

from __future__ import annotations

import argparse
import csv
from datetime import date, datetime, time
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
import hashlib
import json
from pathlib import Path
import sys
from zoneinfo import ZoneInfo


MAIN_BOARD_PREFIXES = ("000", "001", "002", "003", "600", "601", "603", "605")
EXCLUDED_NAME_MARKERS = ("ST", "退", "退市")
PRICE_QUANTUM = Decimal("0.01")
LIMIT_MULTIPLIER = Decimal("1.10")
CLOSE_CUTOFF = time(15, 0, 0)


def parse_day(value: str) -> date:
    return datetime.strptime(value, "%Y-%m-%d").date()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def load_names(path: Path) -> dict[str, dict[str, str]]:
    with path.open("r", encoding="utf-8-sig") as handle:
        rows = json.load(handle)
    result: dict[str, dict[str, str]] = {}
    for row in rows:
        code = str(row.get("code", "")).strip()
        if len(code) != 6 or not code.isdigit():
            continue
        result[code] = {
            "name": str(row.get("name", "")).replace("\x00", "").strip(),
            "market": str(row.get("market", "")).strip(),
        }
    return result


def open_csv(path: Path):
    try:
        return path.open("r", encoding="utf-8-sig", newline="")
    except UnicodeError:
        return path.open("r", encoding="gb18030", newline="")


def close_at_1500(path: Path) -> Decimal:
    # The collector may retain project-specific 15:00-15:30 observations.
    # Limit-up classification uses the latest valid price at or before 15:00.
    try:
        handle = open_csv(path)
        with handle:
            reader = csv.DictReader(handle)
            if not reader.fieldnames or not {"time", "price"}.issubset(reader.fieldnames):
                raise ValueError("missing time/price header")
            latest_time: time | None = None
            latest_price: Decimal | None = None
            for row in reader:
                raw_time = str(row.get("time", "")).strip()
                raw_price = str(row.get("price", "")).strip()
                try:
                    observed_time = datetime.strptime(raw_time, "%H:%M:%S").time()
                    observed_price = Decimal(raw_price)
                except (ValueError, InvalidOperation):
                    continue
                if observed_time > CLOSE_CUTOFF or observed_price <= 0:
                    continue
                if latest_time is None or observed_time >= latest_time:
                    latest_time = observed_time
                    latest_price = observed_price
            if latest_price is None:
                raise ValueError("no valid price at or before 15:00:00")
            return latest_price.quantize(PRICE_QUANTUM, rounding=ROUND_HALF_UP)
    except UnicodeDecodeError:
        with path.open("r", encoding="gb18030", newline="") as handle:
            reader = csv.DictReader(handle)
            latest_time = None
            latest_price = None
            for row in reader:
                try:
                    observed_time = datetime.strptime(
                        str(row.get("time", "")).strip(), "%H:%M:%S").time()
                    observed_price = Decimal(str(row.get("price", "")).strip())
                except (ValueError, InvalidOperation):
                    continue
                if observed_time <= CLOSE_CUTOFF and observed_price > 0:
                    if latest_time is None or observed_time >= latest_time:
                        latest_time = observed_time
                        latest_price = observed_price
            if latest_price is None:
                raise ValueError("no valid price at or before 15:00:00")
            return latest_price.quantize(PRICE_QUANTUM, rounding=ROUND_HALF_UP)


def limit_price(previous_close: Decimal) -> Decimal:
    return (previous_close * LIMIT_MULTIPLIER).quantize(
        PRICE_QUANTUM, rounding=ROUND_HALF_UP)


def build_plan(
    root: Path,
    name_cache: Path,
    history_days: list[date],
    target_day: date,
) -> dict:
    if len(history_days) != 3 or history_days != sorted(set(history_days)):
        raise ValueError("history dates must contain exactly three increasing unique dates")
    if history_days[-1] >= target_day:
        raise ValueError("history must end before the target trading day")

    directories = [root / day.strftime("%Y%m%d") for day in history_days]
    for directory in directories:
        if not directory.is_dir():
            raise FileNotFoundError(f"missing Tick directory: {directory}")

    names = load_names(name_cache)
    day_symbols = [
        {path.stem for path in directory.glob("*.csv") if path.stem.isdigit()}
        for directory in directories
    ]
    common_symbols = set.intersection(*day_symbols)
    eligible_symbols = sorted(
        symbol for symbol in common_symbols
        if len(symbol) == 6 and symbol.startswith(MAIN_BOARD_PREFIXES)
    )

    second_board: list[str] = []
    third_board: list[str] = []
    candidate_details: list[dict] = []
    excluded: dict[str, int] = {
        "missingName": 0,
        "stOrDelistingName": 0,
        "invalidClose": 0,
    }

    for symbol in eligible_symbols:
        identity = names.get(symbol)
        if not identity or not identity["name"]:
            excluded["missingName"] += 1
            continue
        normalized_name = identity["name"].upper()
        if any(marker in normalized_name for marker in EXCLUDED_NAME_MARKERS):
            excluded["stOrDelistingName"] += 1
            continue
        try:
            closes = [
                close_at_1500(directory / f"{symbol}.csv")
                for directory in directories
            ]
        except (OSError, ValueError):
            excluded["invalidClose"] += 1
            continue

        first_limit = closes[1] == limit_price(closes[0])
        second_limit = closes[2] == limit_price(closes[1])
        group = ""
        if first_limit and second_limit:
            third_board.append(symbol)
            group = "thirdBoard"
        elif second_limit:
            second_board.append(symbol)
            group = "secondBoard"
        if group:
            candidate_details.append({
                "symbol": symbol,
                "name": identity["name"],
                "group": group,
                "closes": [format(value, ".2f") for value in closes],
                "limitPrices": [
                    format(limit_price(closes[0]), ".2f"),
                    format(limit_price(closes[1]), ".2f"),
                ],
            })

    frozen_at = datetime.now(ZoneInfo("Asia/Shanghai")).isoformat(timespec="seconds")
    day_text = [day.isoformat() for day in history_days]
    return {
        "planVersion": f"limit-up-main-board-v1-{target_day.isoformat()}",
        "frozenAt": frozen_at,
        "historyThrough": history_days[-1].isoformat(),
        "targetTradingDay": target_day.isoformat(),
        "market": "A股主板",
        "purpose": "前瞻观察昨日一板和二连板在下一交易日接近或触及涨停；只筛选与模拟观察，不下单。",
        "rules": {
            "dailyLimitPrice": "前一交易日15:00及之前最后有效价乘1.10，按0.01元ROUND_HALF_UP；当日15:00及之前最后有效价相等才记为收盘涨停",
            "secondBoard": f"{day_text[2]}收盘涨停、{day_text[1]}未收盘涨停，作为目标日二板候选",
            "thirdBoard": f"{day_text[1]}和{day_text[2]}连续收盘涨停，作为目标日三板候选",
            "liveApproach": "目标日涨幅达到7%",
            "liveLimitZone": "目标日涨幅达到9.5%；盘后仍以精确涨停价复核",
            "exclusions": "仅000/001/002/003/600/601/603/605主板代码；排除名称含ST或退市标识；不读取目标交易日数据",
        },
        "groups": {
            "thirdBoard": third_board,
            "secondBoard": second_board,
            "reversalBoard": [],
        },
        "candidateDetails": candidate_details,
        "source": {
            "kind": "read-only Tick CSV",
            "root": str(root),
            "historyDates": day_text,
            "directoryFileCounts": {
                day.isoformat(): len(symbols)
                for day, symbols in zip(history_days, day_symbols)
            },
            "commonSymbols": len(common_symbols),
            "eligibleMainBoardSymbols": len(eligible_symbols),
            "nameCachePath": str(name_cache),
            "nameCacheSha256": sha256_file(name_cache),
            "nameCacheEntries": len(names),
            "excluded": excluded,
        },
        "limitations": [
            "反转板定义仍未冻结，本版本不事后挑选反转样本。",
            "Tick CSV不含交易所官方前收盘字段；公司行动日可能需要授权日线再次复核。",
            "单日命中只证明前瞻筛选链路，不能证明可成交或盈利。",
            "触及涨停区不等于可成交，模拟观察不接真实账户。",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="冻结A股主板二板/三板候选计划")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--name-cache", type=Path, required=True)
    parser.add_argument("--history-dates", nargs=3, required=True)
    parser.add_argument("--target-date", required=True)
    parser.add_argument("--output", default="-")
    args = parser.parse_args()

    plan = build_plan(
        args.root.resolve(),
        args.name_cache.resolve(),
        [parse_day(value) for value in args.history_dates],
        parse_day(args.target_date),
    )
    payload = json.dumps(plan, ensure_ascii=False, indent=2)
    if args.output == "-":
        sys.stdout.write(payload + "\n")
    else:
        output = Path(args.output).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(payload + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
