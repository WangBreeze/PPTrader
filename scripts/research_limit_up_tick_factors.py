#!/usr/bin/env python3
"""Read-only second/third-board Tick factor research and walk-forward backtest.

The scanner never modifies the TickData root.  It first reads only the tail of
each CSV to build a close cache, then parses full files only for candidate and
context days.  Strategy selection is expanding walk-forward by calendar month;
the evaluated month is never used to choose its own rule.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor, as_completed
import csv
from dataclasses import dataclass
from datetime import date, datetime, timedelta
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
import gzip
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys
import time
from typing import Iterable, Iterator


MAIN_BOARD_PREFIXES = ("000", "001", "002", "003", "600", "601", "603", "605")
EXCLUDED_NAME_MARKERS = ("ST", "退", "退市")
PRICE_QUANTUM = Decimal("0.01")
CLOSE_CUTOFF_SECONDS = 15 * 3600
SIGNAL_CUTOFFS = (9 * 3600 + 35 * 60, 9 * 3600 + 45 * 60, 10 * 3600)
EXIT_SECONDS = 9 * 3600 + 35 * 60
COMMISSION_RATE = 0.0003
STAMP_DUTY_RATE = 0.0005
SLIPPAGE_RATE = 0.0005
MINIMUM_COMMISSION = 5.0
PARTICIPATION_RATE = 0.01
STARTING_CAPITAL = 1_000_000.0
FEATURE_NAMES = (
    "streak",
    "prior_first_touch_minute",
    "prior_final_seal_minute",
    "prior_break_count",
    "prior_touch_segments",
    "prior_range_pct",
    "prior_close_location",
    "prior_buy_imbalance",
    "prior_opening_notional_share",
    "prior_closing_notional_share",
    "prior_log_notional",
    "prior_notional_ratio_5d",
    "prior_tick_ratio_5d",
    "early_return",
    "early_high_return",
    "early_drawdown",
    "early_buy_imbalance",
    "early_notional_ratio_prior",
    "early_tick_ratio_prior",
)


def parse_seconds(value: str) -> int | None:
    parts = value.strip().split(":")
    if len(parts) not in (2, 3):
        return None
    try:
        hour, minute = int(parts[0]), int(parts[1])
        second = int(parts[2]) if len(parts) == 3 else 0
    except ValueError:
        return None
    if not (0 <= hour <= 23 and 0 <= minute <= 59 and 0 <= second <= 59):
        return None
    return hour * 3600 + minute * 60 + second


def format_seconds(value: int | None) -> str | None:
    if value is None:
        return None
    return f"{value // 3600:02d}:{value // 60 % 60:02d}:{value % 60:02d}"


def round_price(value: float) -> float:
    return float(Decimal(str(value)).quantize(PRICE_QUANTUM, rounding=ROUND_HALF_UP))


def limit_price(previous_close: float) -> float:
    return float((Decimal(str(previous_close)) * Decimal("1.10")).quantize(
        PRICE_QUANTUM, rounding=ROUND_HALF_UP))


def lower_limit_price(previous_close: float) -> float:
    return float((Decimal(str(previous_close)) * Decimal("0.90")).quantize(
        PRICE_QUANTUM, rounding=ROUND_HALF_UP))


def price_equal(left: float, right: float) -> bool:
    return abs(left - right) < 0.005


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def load_names(path: Path) -> dict[str, str]:
    with path.open("r", encoding="utf-8-sig") as handle:
        rows = json.load(handle)
    names: dict[str, str] = {}
    for row in rows:
        symbol = str(row.get("code", "")).strip()
        name = str(row.get("name", "")).replace("\x00", "").strip()
        if len(symbol) == 6 and symbol.isdigit() and name:
            names[symbol] = name
    return names


def is_eligible_symbol(symbol: str, names: dict[str, str]) -> bool:
    if len(symbol) != 6 or not symbol.isdigit() or not symbol.startswith(MAIN_BOARD_PREFIXES):
        return False
    name = names.get(symbol, "").upper()
    return bool(name) and not any(marker in name for marker in EXCLUDED_NAME_MARKERS)


def discover_dates(root: Path, start: str, end: str) -> list[str]:
    dates = sorted(
        child.name for child in root.iterdir()
        if child.is_dir() and len(child.name) == 8 and child.name.isdigit()
        and start <= child.name <= end
    )
    if len(dates) < 6:
        raise ValueError(f"not enough exact Tick trading-day directories: {len(dates)}")
    return dates


def month_coverage(dates: list[str], start: str, end: str) -> dict[str, dict]:
    observed = {datetime.strptime(value, "%Y%m%d").date() for value in dates}
    start_day = datetime.strptime(start, "%Y%m%d").date()
    end_day = datetime.strptime(end, "%Y%m%d").date()
    months = sorted({value.strftime("%Y%m") for value in observed})
    result: dict[str, dict] = {}
    for month in months:
        year, number = int(month[:4]), int(month[4:])
        month_start = date(year, number, 1)
        next_month = date(year + (number == 12), 1 if number == 12 else number + 1, 1)
        month_end = next_month - timedelta(days=1)
        left, right = max(month_start, start_day), min(month_end, end_day)
        expected_weekdays: list[date] = []
        cursor = left
        while cursor <= right:
            if cursor.weekday() < 5:
                expected_weekdays.append(cursor)
            cursor += timedelta(days=1)
        missing = [value.isoformat() for value in expected_weekdays if value not in observed]
        month_observed = sorted(value for value in observed if value.strftime("%Y%m") == month)
        requested_range_covers_whole_month = (
            start_day <= month_start and end_day >= month_end)
        result[month] = {
            "observedDirectories": len(month_observed),
            "weekdaySlotsInRequestedRange": len(expected_weekdays),
            "missingWeekdaysWithoutCalendarAdjustment": missing,
            "requestedRangeCoversWholeMonth": requested_range_covers_whole_month,
            "completeByDirectoryHeuristic": (
                requested_range_covers_whole_month and not missing),
        }
    return result


def has_large_calendar_gap(values: list[str], maximum_days: int = 4) -> bool:
    parsed = [datetime.strptime(value, "%Y%m%d").date() for value in values]
    return any((right - left).days > maximum_days
               for left, right in zip(parsed, parsed[1:]))


def read_close_tail(path: Path) -> tuple[float, int] | None:
    """Read the last valid price no later than 15:00 without loading the file."""
    try:
        with path.open("rb") as handle:
            # Avoid a separate path.stat(): on the NAS-backed directory that
            # extra metadata round-trip costs more than the small tail read.
            handle.seek(0, 2)
            size = handle.tell()
            if size <= 0:
                return None
            # Most files have a usable close in their last few records.  Start
            # small to avoid turning a metadata scan on a spinning disk into a
            # full-data read; expand only for files with a long 15:00–15:30 tail.
            window = min(size, 16 * 1024)
            while window <= size:
                offset = size - window
                handle.seek(offset)
                payload = handle.read(window)
                lines = payload.splitlines()
                if offset and lines:
                    lines = lines[1:]
                for raw in reversed(lines):
                    fields = raw.decode("utf-8", errors="replace").strip().split(",")
                    if len(fields) < 2:
                        continue
                    observed = parse_seconds(fields[0])
                    if observed is None or observed > CLOSE_CUTOFF_SECONDS:
                        continue
                    try:
                        price = float(Decimal(fields[1].strip()))
                    except (InvalidOperation, ValueError):
                        continue
                    if math.isfinite(price) and price > 0:
                        return round_price(price), observed
                if window == size:
                    break
                window = min(size, window * 4)
    except OSError:
        return None
    return None


def _close_task(item: tuple[str, Path]) -> tuple[str, tuple[float, int] | None]:
    symbol, path = item
    return symbol, read_close_tail(path)


def build_close_cache(
    root: Path,
    dates: list[str],
    names: dict[str, str],
    workers: int,
    progress: callable | None = None,
) -> tuple[dict[str, dict[str, tuple[float, int]]], dict[str, int]]:
    result: dict[str, dict[str, tuple[float, int]]] = {}
    file_counts: dict[str, int] = {}
    eligible = {symbol for symbol in names if is_eligible_symbol(symbol, names)}
    with ThreadPoolExecutor(max_workers=max(1, workers)) as executor:
        for index, day in enumerate(dates, 1):
            directory = root / day
            items = [
                (path.stem, path) for path in directory.glob("*.csv")
                if path.stem in eligible
            ]
            file_counts[day] = len(items)
            day_result: dict[str, tuple[float, int]] = {}
            for symbol, close in executor.map(_close_task, items, chunksize=64):
                if close is not None:
                    day_result[symbol] = close
            result[day] = day_result
            if progress:
                progress({
                    "stage": "close_scan",
                    "date": day,
                    "dateIndex": index,
                    "dateCount": len(dates),
                    "files": len(items),
                    "validCloses": len(day_result),
                })
    return result, file_counts


def save_close_cache(
    path: Path,
    dates: list[str],
    closes: dict[str, dict[str, tuple[float, int]]],
    file_counts: dict[str, int],
    name_sha: str,
) -> None:
    payload = {
        "schemaVersion": 1,
        "dates": dates,
        "nameCacheSha256": name_sha,
        "fileCounts": file_counts,
        "closes": {
            day: {symbol: [value[0], value[1]] for symbol, value in rows.items()}
            for day, rows in closes.items()
        },
    }
    temp = path.with_suffix(path.suffix + ".tmp")
    with gzip.open(temp, "wt", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=True, separators=(",", ":"))
    temp.replace(path)


def load_close_cache(
    path: Path, dates: list[str], name_sha: str,
) -> tuple[dict[str, dict[str, tuple[float, int]]], dict[str, int]] | None:
    if not path.is_file():
        return None
    try:
        with gzip.open(path, "rt", encoding="utf-8") as handle:
            payload = json.load(handle)
        if payload.get("schemaVersion") != 1 or payload.get("dates") != dates:
            return None
        if payload.get("nameCacheSha256") != name_sha:
            return None
        closes = {
            day: {symbol: (float(value[0]), int(value[1])) for symbol, value in rows.items()}
            for day, rows in payload["closes"].items()
        }
        return closes, {key: int(value) for key, value in payload["fileCounts"].items()}
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
        return None


@dataclass(frozen=True)
class Tick:
    seconds: int
    price: float
    volume_shares: float
    side: str

    @property
    def notional(self) -> float:
        return self.price * self.volume_shares


def iter_ticks(path: Path, digest: hashlib._Hash | None = None) -> Iterator[Tick]:
    try:
        handle = path.open("rb")
    except OSError:
        return
    with handle:
        chinese = False
        layout_known = False
        for raw_line in handle:
            if digest is not None:
                digest.update(raw_line)
            text = raw_line.decode("utf-8-sig", errors="replace").strip()
            fields = text.split(",") if text else []
            if not fields:
                continue
            if not layout_known:
                layout_known = True
                first = fields[0].strip().lower()
                if first == "time":
                    continue
                if fields[0].strip() == "成交时间" and len(fields) >= 6:
                    chinese = True
                    continue
            if len(fields) < (6 if chinese else 5):
                continue
            volume_index = 3 if chinese else 2
            side_index = 5 if chinese else 4
            seconds = parse_seconds(fields[0])
            if seconds is None or seconds > 15 * 3600 + 30 * 60:
                continue
            try:
                price = float(fields[1].strip())
                volume_lots = float(fields[volume_index].strip())
            except ValueError:
                continue
            if not (math.isfinite(price) and price > 0 and math.isfinite(volume_lots)
                    and volume_lots >= 0):
                continue
            raw_side = fields[side_index].strip().upper()
            if raw_side in ("B", "买", "买盘"):
                side = "B"
            elif raw_side in ("S", "卖", "卖盘"):
                side = "S"
            elif raw_side in ("M", "中", "中性", "中性盘"):
                side = "M"
            else:
                continue
            yield Tick(seconds, price, volume_lots * 100.0, side)


def summarize_tick_day(path: Path, previous_close: float) -> dict | None:
    target_limit = limit_price(previous_close)
    target_lower_limit = lower_limit_price(previous_close)
    snapshots = {
        cutoff: {
            "lastPrice": None, "high": None, "low": None, "tickCount": 0,
            "notional": 0.0, "buyNotional": 0.0, "sellNotional": 0.0,
            "firstAtOrAfter": None, "entry": None, "entryNotional60s": 0.0,
            "exit": None, "exitNotional60s": 0.0, "hitAfterEntry": False,
        }
        for cutoff in SIGNAL_CUTOFFS + (EXIT_SECONDS,)
    }
    first_price = last_price = high = low = None
    first_seconds = last_seconds = None
    tick_count = 0
    total_notional = buy_notional = sell_notional = 0.0
    opening_notional = closing_notional = 0.0
    at_limit = False
    first_touch = final_seal = None
    touch_segments = break_count = 0
    last_state_seconds = None
    limit_state_seconds = 0
    digest = hashlib.sha256()
    for tick in iter_ticks(path, digest):
        if tick.seconds > CLOSE_CUTOFF_SECONDS:
            continue
        tick_count += 1
        if first_price is None:
            first_price, first_seconds = tick.price, tick.seconds
        last_price, last_seconds = tick.price, tick.seconds
        high = tick.price if high is None else max(high, tick.price)
        low = tick.price if low is None else min(low, tick.price)
        total_notional += tick.notional
        if tick.side == "B":
            buy_notional += tick.notional
        elif tick.side == "S":
            sell_notional += tick.notional
        if tick.seconds <= 10 * 3600:
            opening_notional += tick.notional
        if tick.seconds >= 14 * 3600 + 30 * 60:
            closing_notional += tick.notional

        now_at_limit = price_equal(tick.price, target_limit)
        if last_state_seconds is not None and at_limit:
            limit_state_seconds += max(0, tick.seconds - last_state_seconds)
        if now_at_limit and not at_limit:
            touch_segments += 1
            first_touch = tick.seconds if first_touch is None else first_touch
            final_seal = tick.seconds
        elif at_limit and not now_at_limit:
            break_count += 1
            final_seal = None
        at_limit = now_at_limit
        last_state_seconds = tick.seconds

        for cutoff, snap in snapshots.items():
            if tick.seconds <= cutoff:
                snap["lastPrice"] = tick.price
                snap["high"] = tick.price if snap["high"] is None else max(snap["high"], tick.price)
                snap["low"] = tick.price if snap["low"] is None else min(snap["low"], tick.price)
                snap["tickCount"] += 1
                snap["notional"] += tick.notional
                if tick.side == "B":
                    snap["buyNotional"] += tick.notional
                elif tick.side == "S":
                    snap["sellNotional"] += tick.notional
            if tick.seconds >= cutoff and snap["firstAtOrAfter"] is None:
                snap["firstAtOrAfter"] = [tick.seconds, tick.price, tick.volume_shares]
            # The signal snapshot includes ticks through the cutoff second, so
            # execution must use a strictly later trade to avoid same-tick fill.
            if tick.seconds > cutoff and snap["entry"] is None and tick.price < target_limit - 0.004:
                snap["entry"] = [tick.seconds, tick.price, tick.volume_shares]
            if (tick.seconds >= cutoff and snap["exit"] is None
                    and tick.price > target_lower_limit + 0.004):
                snap["exit"] = [tick.seconds, tick.price, tick.volume_shares]
            entry = snap["entry"]
            if entry is not None:
                if entry[0] <= tick.seconds <= entry[0] + 60:
                    snap["entryNotional60s"] += tick.notional
                if tick.seconds >= entry[0] and now_at_limit:
                    snap["hitAfterEntry"] = True
            exit_tick = snap["exit"]
            if exit_tick is not None and exit_tick[0] <= tick.seconds <= exit_tick[0] + 60:
                snap["exitNotional60s"] += tick.notional

    if first_price is None or last_price is None or high is None or low is None:
        return None
    close_at_limit = price_equal(last_price, target_limit)
    if not close_at_limit:
        final_seal = None
    return {
        "path": str(path),
        "sha256": digest.hexdigest().upper(),
        "previousClose": previous_close,
        "limitPrice": target_limit,
        "lowerLimitPrice": target_lower_limit,
        "firstPrice": first_price,
        "lastPrice": last_price,
        "high": high,
        "low": low,
        "firstSeconds": first_seconds,
        "lastSeconds": last_seconds,
        "tickCount": tick_count,
        "notional": total_notional,
        "buyNotional": buy_notional,
        "sellNotional": sell_notional,
        "openingNotional": opening_notional,
        "closingNotional": closing_notional,
        "firstTouchSeconds": first_touch,
        "finalSealSeconds": final_seal,
        "touchSegments": touch_segments,
        "breakCount": break_count,
        "limitStateSeconds": limit_state_seconds,
        "touchedLimit": first_touch is not None,
        "closedLimit": close_at_limit,
        "snapshots": snapshots,
        "quality": {
            "hasOpen": first_seconds is not None and first_seconds <= 10 * 3600,
            "hasClose": last_seconds is not None and last_seconds >= 14 * 3600 + 55 * 60,
            "tickCountAtLeast20": tick_count >= 20,
        },
    }


def summarize_tick_day_task(
    task: tuple[str, str, str, float],
) -> tuple[tuple[str, str], dict | None]:
    """Process-pool entry point for one immutable Tick file."""
    day, symbol, path, previous_close = task
    return (day, symbol), summarize_tick_day(Path(path), previous_close)


def safe_ratio(numerator: float, denominator: float, default: float = 0.0) -> float:
    return numerator / denominator if denominator > 0 else default


def median(values: Iterable[float]) -> float:
    rows = [value for value in values if math.isfinite(value)]
    return statistics.median(rows) if rows else 0.0


def build_events(
    root: Path,
    dates: list[str],
    closes: dict[str, dict[str, tuple[float, int]]],
    names: dict[str, str],
    workers: int,
    progress: callable | None = None,
) -> tuple[list[dict], dict]:
    full_cache: dict[tuple[str, str], dict | None] = {}
    date_index = {day: index for index, day in enumerate(dates)}

    def get_close(day: str, symbol: str) -> float | None:
        row = closes.get(day, {}).get(symbol)
        return row[0] if row else None

    def closed_limit(day_index_value: int, symbol: str) -> bool:
        if day_index_value <= 0:
            return False
        current = get_close(dates[day_index_value], symbol)
        previous = get_close(dates[day_index_value - 1], symbol)
        return current is not None and previous is not None and price_equal(
            current, limit_price(previous))

    def get_full(day: str, symbol: str) -> dict | None:
        key = (day, symbol)
        if key in full_cache:
            return full_cache[key]
        index = date_index[day]
        if index <= 0:
            full_cache[key] = None
            return None
        previous = get_close(dates[index - 1], symbol)
        if previous is None:
            full_cache[key] = None
            return None
        value = summarize_tick_day(root / day / f"{symbol}.csv", previous)
        full_cache[key] = value
        return value

    candidate_rows: list[tuple[int, str, int]] = []
    group_counts = defaultdict(int)
    exclusions = defaultdict(int)
    for target_index in range(3, len(dates) - 1):
        target_day = dates[target_index]
        feature_window = dates[max(0, target_index - 5):target_index + 2]
        crosses_large_gap = has_large_calendar_gap(feature_window)
        previous_symbols = closes[dates[target_index - 1]].keys()
        for symbol in previous_symbols:
            if symbol not in names or not is_eligible_symbol(symbol, names):
                continue
            if not closed_limit(target_index - 1, symbol):
                continue
            if crosses_large_gap:
                exclusions["calendarGapWindow"] += 1
                continue
            streak = 1
            probe = target_index - 2
            while probe > 0 and closed_limit(probe, symbol):
                streak += 1
                probe -= 1
            group = "thirdBoard" if streak >= 2 else "secondBoard"
            group_counts[group] += 1
            candidate_rows.append((target_index, symbol, streak))

    required_keys: set[tuple[str, str]] = set()
    for target_index, symbol, _streak in candidate_rows:
        for required_index in range(max(1, target_index - 5), target_index + 2):
            required_keys.add((dates[required_index], symbol))
    tasks: list[tuple[str, str, str, float]] = []
    for day, symbol in sorted(required_keys):
        index = date_index[day]
        previous = get_close(dates[index - 1], symbol) if index > 0 else None
        if previous is None:
            full_cache[(day, symbol)] = None
            continue
        tasks.append((day, symbol, str(root / day / f"{symbol}.csv"), previous))
    if progress:
        progress({
            "stage": "full_file_plan",
            "candidates": len(candidate_rows),
            "uniqueFiles": len(tasks),
            "workers": max(1, workers),
        })
    with ProcessPoolExecutor(max_workers=max(1, workers)) as executor:
        futures = [executor.submit(summarize_tick_day_task, task) for task in tasks]
        for completed, future in enumerate(as_completed(futures), 1):
            key, value = future.result()
            full_cache[key] = value
            if progress and (completed % 250 == 0 or completed == len(futures)):
                progress({
                    "stage": "full_file_parse",
                    "processed": completed,
                    "files": len(futures),
                })

    events: list[dict] = []
    for position, (target_index, symbol, streak) in enumerate(candidate_rows, 1):
        target_day = dates[target_index]
        prior_day = dates[target_index - 1]
        next_day = dates[target_index + 1]
        prior = get_full(prior_day, symbol)
        target = get_full(target_day, symbol)
        following = get_full(next_day, symbol)
        if not prior or not target or not following:
            exclusions["missingFullDay"] += 1
            if progress and (position % 25 == 0 or position == len(candidate_rows)):
                progress({"stage": "event_extract", "processed": position,
                          "candidates": len(candidate_rows), "events": len(events)})
            continue
        if not prior["quality"]["hasClose"] or not target["quality"]["hasClose"]:
            exclusions["missingCloseCoverage"] += 1
            if progress and (position % 25 == 0 or position == len(candidate_rows)):
                progress({"stage": "event_extract", "processed": position,
                          "candidates": len(candidate_rows), "events": len(events)})
            continue

        context: list[dict] = []
        for context_index in range(max(1, target_index - 5), target_index):
            item = get_full(dates[context_index], symbol)
            if item:
                context.append(item)
        baseline_notional = median(item["notional"] for item in context[:-1])
        baseline_ticks = median(float(item["tickCount"]) for item in context[:-1])
        prior_direction_denominator = prior["buyNotional"] + prior["sellNotional"]
        prior_range = safe_ratio(prior["high"] - prior["low"], prior["previousClose"])
        prior_close_location = safe_ratio(
            prior["lastPrice"] - prior["low"], prior["high"] - prior["low"], 1.0)
        prior_features = {
            "streak": float(streak),
            "prior_first_touch_minute": safe_ratio(
                float(prior["firstTouchSeconds"] or CLOSE_CUTOFF_SECONDS) - 9.5 * 3600, 60.0),
            "prior_final_seal_minute": safe_ratio(
                float(prior["finalSealSeconds"] or CLOSE_CUTOFF_SECONDS) - 9.5 * 3600, 60.0),
            "prior_break_count": float(prior["breakCount"]),
            "prior_touch_segments": float(prior["touchSegments"]),
            "prior_range_pct": prior_range,
            "prior_close_location": prior_close_location,
            "prior_buy_imbalance": safe_ratio(
                prior["buyNotional"] - prior["sellNotional"], prior_direction_denominator),
            "prior_opening_notional_share": safe_ratio(
                prior["openingNotional"], prior["notional"]),
            "prior_closing_notional_share": safe_ratio(
                prior["closingNotional"], prior["notional"]),
            "prior_log_notional": math.log1p(prior["notional"]),
            "prior_notional_ratio_5d": safe_ratio(prior["notional"], baseline_notional, 1.0),
            "prior_tick_ratio_5d": safe_ratio(float(prior["tickCount"]), baseline_ticks, 1.0),
        }
        cutoff_rows: dict[str, dict] = {}
        for cutoff in SIGNAL_CUTOFFS:
            snap = target["snapshots"][cutoff]
            label = format_seconds(cutoff)
            entry = snap["entry"]
            last_price = snap["lastPrice"]
            early_direction_denominator = snap["buyNotional"] + snap["sellNotional"]
            features = dict(prior_features)
            features.update({
                "early_return": safe_ratio((last_price or target["firstPrice"])
                                             - target["previousClose"], target["previousClose"]),
                "early_high_return": safe_ratio((snap["high"] or target["firstPrice"])
                                                  - target["previousClose"], target["previousClose"]),
                "early_drawdown": safe_ratio((snap["high"] or target["firstPrice"])
                                               - (last_price or target["firstPrice"]),
                                               snap["high"] or target["firstPrice"]),
                "early_buy_imbalance": safe_ratio(
                    snap["buyNotional"] - snap["sellNotional"], early_direction_denominator),
                "early_notional_ratio_prior": safe_ratio(snap["notional"], prior["notional"]),
                "early_tick_ratio_prior": safe_ratio(float(snap["tickCount"]),
                                                       float(prior["tickCount"])),
            })
            exit_snapshot = following["snapshots"][EXIT_SECONDS]
            exit_tick = exit_snapshot["exit"]
            net_return = None
            if entry is not None and exit_tick is not None:
                gross_buy = entry[1] * (1.0 + SLIPPAGE_RATE)
                gross_sell = exit_tick[1] * (1.0 - SLIPPAGE_RATE)
                net_return = (gross_sell * (1.0 - COMMISSION_RATE - STAMP_DUTY_RATE)
                              / (gross_buy * (1.0 + COMMISSION_RATE)) - 1.0)
            cutoff_rows[label] = {
                "features": features,
                "lastPrice": last_price,
                "entrySeconds": entry[0] if entry else None,
                "entryPrice": entry[1] if entry else None,
                "entryVolumeShares": entry[2] if entry else None,
                "entryNotional60s": snap["entryNotional60s"],
                "entryReturn": (safe_ratio(entry[1] - target["previousClose"],
                                             target["previousClose"])
                                if entry else None),
                "touchedBeforeSignal": (target["firstTouchSeconds"] is not None
                                        and target["firstTouchSeconds"] <= cutoff),
                "hitAfterEntry": bool(snap["hitAfterEntry"]),
                "exitSeconds": exit_tick[0] if exit_tick else None,
                "exitPrice": exit_tick[1] if exit_tick else None,
                "exitNotional60s": exit_snapshot["exitNotional60s"],
                "netReturn": net_return,
            }
        events.append({
            "targetDate": target_day,
            "month": target_day[:6],
            "symbol": symbol,
            "name": names[symbol],
            "group": "thirdBoard" if streak >= 2 else "secondBoard",
            "streak": streak,
            "previousClose": target["previousClose"],
            "limitPrice": target["limitPrice"],
            "touchedLimit": target["touchedLimit"],
            "closedLimit": target["closedLimit"],
            "firstTouchSeconds": target["firstTouchSeconds"],
            "targetTickCount": target["tickCount"],
            "priorTickCount": prior["tickCount"],
            "priorSha256": prior["sha256"],
            "targetSha256": target["sha256"],
            "exitSha256": following["sha256"],
            "cutoffs": cutoff_rows,
        })
        if progress and (position % 25 == 0 or position == len(candidate_rows)):
            progress({
                "stage": "event_extract",
                "processed": position,
                "candidates": len(candidate_rows),
                "events": len(events),
            })
    return events, {
        "candidateRows": len(candidate_rows),
        "groupCountsBeforeQuality": dict(group_counts),
        "exclusions": dict(exclusions),
        "fullFileTasks": len(tasks),
        "fullFileSummaries": sum(value is not None for value in full_cache.values()),
        "fullFileKeysRequested": len(full_cache),
    }


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    rows = sorted(values)
    position = fraction * (len(rows) - 1)
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return rows[lower]
    weight = position - lower
    return rows[lower] * (1.0 - weight) + rows[upper] * weight


def auc(values: list[tuple[float, bool]]) -> float | None:
    positive = [value for value, label in values if label]
    negative = [value for value, label in values if not label]
    if not positive or not negative:
        return None
    wins = 0.0
    for left in positive:
        for right in negative:
            wins += 1.0 if left > right else 0.5 if left == right else 0.0
    return wins / (len(positive) * len(negative))


def sample_for(event: dict, cutoff: str) -> dict | None:
    row = event["cutoffs"].get(cutoff)
    if not row or row["lastPrice"] is None:
        return None
    return {
        "event": event,
        "row": row,
        "features": row["features"],
        "label": bool(row["hitAfterEntry"]),
        "return": (float(row["netReturn"])
                   if row["netReturn"] is not None else None),
    }


def fit_score_model(samples: list[dict]) -> dict:
    model = {"features": {}}
    for name in FEATURE_NAMES:
        rows = [(float(sample["features"].get(name, 0.0)), sample["label"])
                for sample in samples]
        values = [value for value, _ in rows]
        positives = [value for value, label in rows if label]
        negatives = [value for value, label in rows if not label]
        mean = statistics.fmean(values) if values else 0.0
        stddev = statistics.pstdev(values) if len(values) >= 2 else 0.0
        difference = ((statistics.fmean(positives) if positives else mean)
                      - (statistics.fmean(negatives) if negatives else mean))
        weight = max(-2.0, min(2.0, difference / stddev)) if stddev > 1e-12 else 0.0
        model["features"][name] = {
            "mean": mean,
            "stddev": stddev,
            "weight": weight,
            "auc": auc(rows),
        }
    model["weightScale"] = sum(abs(row["weight"])
                               for row in model["features"].values()) or 1.0
    return model


def score_sample(sample: dict, model: dict) -> float:
    total = 0.0
    for name, row in model["features"].items():
        stddev = row["stddev"]
        if stddev <= 1e-12:
            continue
        value = float(sample["features"].get(name, row["mean"]))
        total += row["weight"] * (value - row["mean"]) / stddev
    return total / model["weightScale"]


def execute_trade(capital: float, budget: float, sample: dict, capacity_aware: bool) -> dict | None:
    row = sample["row"]
    if row["entryPrice"] is None or row["exitPrice"] is None:
        return None
    entry = float(row["entryPrice"])
    exit_price = float(row["exitPrice"])
    capacity = min(float(row["entryNotional60s"]),
                   float(row["exitNotional60s"])) * PARTICIPATION_RATE
    deploy = min(budget, capacity) if capacity_aware else budget
    buy_price = entry * (1.0 + SLIPPAGE_RATE)
    shares = math.floor(deploy / buy_price / 100.0) * 100
    if shares < 100:
        return None
    buy_value = shares * buy_price
    buy_fee = max(MINIMUM_COMMISSION, buy_value * COMMISSION_RATE)
    while shares >= 100 and buy_value + buy_fee > min(capital, budget):
        shares -= 100
        buy_value = shares * buy_price
        buy_fee = max(MINIMUM_COMMISSION, buy_value * COMMISSION_RATE) if shares else 0.0
    if shares < 100:
        return None
    sell_value = shares * exit_price * (1.0 - SLIPPAGE_RATE)
    sell_fee = max(MINIMUM_COMMISSION, sell_value * COMMISSION_RATE)
    stamp = sell_value * STAMP_DUTY_RATE
    profit = sell_value - sell_fee - stamp - buy_value - buy_fee
    return {
        "shares": shares,
        "deployed": buy_value + buy_fee,
        "profit": profit,
        "returnOnTrade": profit / (buy_value + buy_fee),
        "capacity": capacity,
    }


def simulate_month(samples: list[dict], parameters: dict, capacity_aware: bool) -> dict:
    by_day: dict[str, list[dict]] = defaultdict(list)
    for sample in samples:
        event = sample["event"]
        row = sample["row"]
        if parameters["group"] != "both" and event["group"] != parameters["group"]:
            continue
        if sample["score"] < parameters["minimumScore"]:
            continue
        signal_return = float(row["features"].get("early_return", 0.0))
        if signal_return > parameters["maximumEntryReturn"]:
            continue
        if parameters["excludeTouchedBeforeSignal"] and row["touchedBeforeSignal"]:
            continue
        by_day[event["targetDate"]].append(sample)
    capital = STARTING_CAPITAL
    peak_capital = capital
    maximum_drawdown = 0.0
    trades: list[dict] = []
    for day in sorted(by_day):
        selected = sorted(by_day[day], key=lambda item: (-item["score"], item["event"]["symbol"]))[
            :parameters["topN"]]
        if not selected:
            continue
        budget = capital / len(selected)
        day_profit = 0.0
        for sample in selected:
            fill = execute_trade(capital, budget, sample, capacity_aware)
            if not fill:
                continue
            day_profit += fill["profit"]
            event = sample["event"]
            row = sample["row"]
            trades.append({
                "targetDate": event["targetDate"],
                "symbol": event["symbol"],
                "group": event["group"],
                "score": sample["score"],
                "entryTime": format_seconds(row["entrySeconds"]),
                "entryPrice": row["entryPrice"],
                "exitTime": format_seconds(row["exitSeconds"]),
                "exitPrice": row["exitPrice"],
                "capturedBoard": bool(row["hitAfterEntry"]),
                **fill,
            })
        capital += day_profit
        peak_capital = max(peak_capital, capital)
        maximum_drawdown = max(maximum_drawdown,
                               safe_ratio(peak_capital - capital, peak_capital))
    captured = sum(1 for trade in trades if trade["capturedBoard"])
    winners = [trade["profit"] for trade in trades if trade["profit"] > 0]
    losers = [-trade["profit"] for trade in trades if trade["profit"] < 0]
    return {
        "startingCapital": STARTING_CAPITAL,
        "endingCapital": capital,
        "monthlyReturn": capital / STARTING_CAPITAL - 1.0,
        "maximumDrawdown": maximum_drawdown,
        "tradeCount": len(trades),
        "winningTrades": len(winners),
        "losingTrades": len(losers),
        "averageWin": statistics.fmean(winners) if winners else None,
        "averageLossAbs": statistics.fmean(losers) if losers else None,
        "payoffRatio": (statistics.fmean(winners) / statistics.fmean(losers)
                        if winners and losers else None),
        "capturedBoards": captured,
        "qualifiedReturn50": capital >= STARTING_CAPITAL * 1.5,
        "qualifiedFiveBoards": captured >= 5,
        "qualifiedBoth": capital >= STARTING_CAPITAL * 1.5 and captured >= 5,
        "trades": trades,
    }


def parameter_grid(training_samples: list[dict]) -> Iterator[dict]:
    scores_by_group = defaultdict(list)
    for sample in training_samples:
        scores_by_group[sample["event"]["group"]].append(sample["score"])
        scores_by_group["both"].append(sample["score"])
    for group in ("secondBoard", "thirdBoard", "both"):
        scores = scores_by_group[group]
        if not scores:
            continue
        for quantile in (0.0, 0.25, 0.5, 0.75):
            threshold = percentile(scores, quantile)
            for top_n in (1, 2, 3, 5):
                for maximum_entry_return in (0.05, 0.07, 0.085, 0.095):
                    for exclude_touched in (True, False):
                        yield {
                            "group": group,
                            "minimumScoreQuantile": quantile,
                            "minimumScore": threshold,
                            "topN": top_n,
                            "maximumEntryReturn": maximum_entry_return,
                            "excludeTouchedBeforeSignal": exclude_touched,
                        }


def choose_parameters(training_samples: list[dict]) -> tuple[dict, list[dict], int]:
    months = sorted({sample["event"]["month"] for sample in training_samples})
    best_parameters = None
    best_metrics = None
    best_key = None
    candidate_count = 0
    for parameters in parameter_grid(training_samples):
        candidate_count += 1
        metrics = []
        for month in months:
            month_samples = [sample for sample in training_samples if sample["event"]["month"] == month]
            result = simulate_month(month_samples, parameters, True)
            metrics.append({"month": month, **{key: value for key, value in result.items()
                                               if key != "trades"}})
        if not metrics:
            continue
        qualified = sum(1 for row in metrics if row["qualifiedBoth"])
        min_return = min(row["monthlyReturn"] for row in metrics)
        min_boards = min(row["capturedBoards"] for row in metrics)
        avg_return = statistics.fmean(row["monthlyReturn"] for row in metrics)
        total_boards = sum(row["capturedBoards"] for row in metrics)
        key = (qualified, min_return, min_boards, avg_return, total_boards,
               -parameters["topN"], parameters["excludeTouchedBeforeSignal"])
        if best_key is None or key > best_key:
            best_key = key
            best_parameters = dict(parameters)
            best_metrics = metrics
    if best_parameters is None or best_metrics is None:
        raise ValueError("no executable training parameter candidate")
    return best_parameters, best_metrics, candidate_count


def group_statistics(events: list[dict]) -> dict:
    result = {}
    for group in ("secondBoard", "thirdBoard"):
        rows = [event for event in events if event["group"] == group]
        result[group] = {
            "events": len(rows),
            "touched": sum(1 for row in rows if row["touchedLimit"]),
            "closed": sum(1 for row in rows if row["closedLimit"]),
            "touchRate": safe_ratio(sum(1 for row in rows if row["touchedLimit"]), len(rows)),
            "closeRate": safe_ratio(sum(1 for row in rows if row["closedLimit"]), len(rows)),
        }
    return result


def factor_statistics(events: list[dict], cutoff: str) -> list[dict]:
    samples = [sample for event in events if (sample := sample_for(event, cutoff)) is not None]
    rows = []
    for name in FEATURE_NAMES:
        labelled = [(float(sample["features"].get(name, 0.0)), sample["label"])
                    for sample in samples]
        positive = [value for value, label in labelled if label]
        negative = [value for value, label in labelled if not label]
        second = [float(sample["features"].get(name, 0.0)) for sample in samples
                  if sample["event"]["group"] == "secondBoard"]
        third = [float(sample["features"].get(name, 0.0)) for sample in samples
                 if sample["event"]["group"] == "thirdBoard"]
        rows.append({
            "factor": name,
            "aucForCapturedBoard": auc(labelled),
            "capturedMedian": median(positive),
            "notCapturedMedian": median(negative),
            "secondBoardMedian": median(second),
            "thirdBoardMedian": median(third),
        })
    rows.sort(key=lambda row: abs((row["aucForCapturedBoard"] or 0.5) - 0.5), reverse=True)
    return rows


def run_walk_forward(events: list[dict], qualification_through: str,
                     coverage: dict[str, dict]) -> dict:
    months = sorted({event["month"] for event in events})
    evaluations = []
    for month_index in range(1, len(months)):
        evaluation_month = months[month_index]
        training_months = months[:month_index]
        best = None
        search_candidates = 0
        for cutoff_seconds in SIGNAL_CUTOFFS:
            cutoff = format_seconds(cutoff_seconds)
            training = [sample for event in events if event["month"] in training_months
                        and (sample := sample_for(event, cutoff)) is not None]
            evaluation = [sample for event in events if event["month"] == evaluation_month
                          and (sample := sample_for(event, cutoff)) is not None]
            if len(training) < 20 or not evaluation:
                continue
            model = fit_score_model(training)
            for sample in training + evaluation:
                sample["score"] = score_sample(sample, model)
            parameters, training_metrics, rule_candidates = choose_parameters(training)
            search_candidates += rule_candidates
            capacity = simulate_month(evaluation, parameters, True)
            optimistic = simulate_month(evaluation, parameters, False)
            candidate = {
                "signalCutoff": cutoff,
                "trainingMonths": training_months,
                "evaluationMonth": evaluation_month,
                "qualificationEligible": (
                    evaluation_month <= qualification_through
                    and coverage.get(evaluation_month, {}).get(
                        "completeByDirectoryHeuristic", False)),
                "trainingSampleCount": len(training),
                "evaluationSampleCount": len(evaluation),
                "parameters": parameters,
                "trainingMetrics": training_metrics,
                "capacityAware": capacity,
                "optimisticUpperBound": optimistic,
                "model": model,
            }
            # Cutoff selection is also training-only.  Never use the evaluation
            # month's return or captures to choose among 09:35/09:45/10:00.
            training_key = (
                sum(1 for row in training_metrics if row["qualifiedBoth"]),
                min(row["monthlyReturn"] for row in training_metrics),
                min(row["capturedBoards"] for row in training_metrics),
                statistics.fmean(row["monthlyReturn"] for row in training_metrics),
                sum(row["capturedBoards"] for row in training_metrics),
            )
            if best is None or training_key > best[0]:
                best = (training_key, candidate)
        if best:
            best[1]["trainingSearchCandidatesAcrossCutoffs"] = search_candidates
            evaluations.append(best[1])
    qualification_rows = [row for row in evaluations if row["qualificationEligible"]]
    return {
        "months": months,
        "qualificationThroughMonth": qualification_through,
        "evaluations": evaluations,
        "trainingSearchCandidates": sum(
            row["trainingSearchCandidatesAcrossCutoffs"] for row in evaluations),
        "allOosMonthsMeetBoth": bool(qualification_rows) and all(
            row["capacityAware"]["qualifiedBoth"] for row in qualification_rows),
        "allOosMonthsMeetFiveBoards": bool(qualification_rows) and all(
            row["capacityAware"]["qualifiedFiveBoards"] for row in qualification_rows),
        "allOosMonthsMeetReturn50": bool(qualification_rows) and all(
            row["capacityAware"]["qualifiedReturn50"] for row in qualification_rows),
    }


def verify_report(report: dict, events: list[dict]) -> dict:
    identities = [(event["targetDate"], event["symbol"]) for event in events]
    if len(identities) != len(set(identities)):
        raise ValueError("duplicate targetDate/symbol event identity")
    source_hashes = 0
    for event in events:
        for key in ("priorSha256", "targetSha256", "exitSha256"):
            value = event.get(key, "")
            if len(value) != 64 or any(character not in "0123456789ABCDEF" for character in value):
                raise ValueError(f"invalid {key} for {event['targetDate']}/{event['symbol']}")
            source_hashes += 1
    verified_trades = 0
    for evaluation in report["walkForward"]["evaluations"]:
        month = evaluation["evaluationMonth"]
        if any(training_month >= month for training_month in evaluation["trainingMonths"]):
            raise ValueError(f"non-causal training month in {month}")
        result = evaluation["capacityAware"]
        recomputed_profit = 0.0
        for trade in result["trades"]:
            if not trade["targetDate"].startswith(month):
                raise ValueError(f"trade outside evaluation month {month}")
            shares = int(trade["shares"])
            if shares < 100 or shares % 100:
                raise ValueError(f"non-board-lot trade in {month}")
            buy_notional = shares * float(trade["entryPrice"]) * (1.0 + SLIPPAGE_RATE)
            if buy_notional > float(trade["capacity"]) + 0.01:
                raise ValueError(f"entry exceeds two-sided capacity in {month}")
            recomputed_profit += float(trade["profit"])
            verified_trades += 1
        recomputed_return = recomputed_profit / float(result["startingCapital"])
        if abs(recomputed_return - float(result["monthlyReturn"])) > 1e-9:
            raise ValueError(f"monthly return does not reconcile in {month}")
    return {
        "eventIdentityUnique": True,
        "verifiedEvents": len(events),
        "verifiedSourceHashes": source_hashes,
        "strictlyEarlierTrainingMonths": True,
        "verifiedCapacityAwareTrades": verified_trades,
        "boardLotsAndTwoSidedCapacityValid": True,
        "monthlyReturnsReconciled": True,
    }


def write_markdown(report: dict, path: Path) -> None:
    lines = [
        "# 二板/三板历史 Tick 因子与逐月样本外回测",
        "",
        "## 结论",
        "",
        ("样本外所有月份同时满足“可成交封板捕获不少于 5 次”和“扣费月收益不低于 50%”。"
         if report["walkForward"]["allOosMonthsMeetBoth"] else
         "没有证据证明样本外每个月都能同时达到 5 次可成交封板和 50% 扣费收益；不发布可用实盘策略。"),
        "",
        "触板次数和账户收益分别计算；一字板或信号后没有涨停价以下成交的样本不计可成交买入，次日封死跌停时不虚构卖出。容量口径最多使用买卖两侧成交后 60 秒实际成交额较小值的 1%，并计入双边滑点、佣金、卖出印花税、100 股整手和 T+1 次日退出。",
        "",
        "## 数据与样本",
        "",
        f"- 日期：{report['source']['dates'][0]}–{report['source']['dates'][-1]}，{len(report['source']['dates'])} 个交易日目录。",
        f"- 候选事件：{report['eventExtraction']['events']}；去重 Tick 文件读取任务：{report['eventExtraction']['fullFileTasks']}。",
        f"- 扩展式样本外各折仅用训练月选择规则，累计评估 {report['walkForward']['trainingSearchCandidates']} 个训练期规则组合。",
        f"- 输入收盘身份 SHA-256：`{report['source']['closeIdentitySha256']}`。",
        "",
        "## 二板与三板",
        "",
        "| 组别 | 事件 | 触板 | 触板率 | 收盘封板 | 封板率 |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for key, label in (("secondBoard", "二板"), ("thirdBoard", "三板及以上")):
        row = report["groupStatistics"][key]
        lines.append(f"| {label} | {row['events']} | {row['touched']} | {row['touchRate']:.2%} | {row['closed']} | {row['closeRate']:.2%} |")
    lines.extend(["", "## 逐月样本外结果", "",
                  "| 评价月 | 完整月门禁 | 信号 | 规则组 | 容量收益 | 最大回撤 | 可成交封板 | 交易 | 乐观上界收益 | 同时达标 |",
                  "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |"])
    for evaluation in report["walkForward"]["evaluations"]:
        capacity = evaluation["capacityAware"]
        optimistic = evaluation["optimisticUpperBound"]
        lines.append(
            f"| {evaluation['evaluationMonth']} | {'是' if evaluation['qualificationEligible'] else '否（未完月）'} | "
            f"{evaluation['signalCutoff']} | {evaluation['parameters']['group']} | "
            f"{capacity['monthlyReturn']:.2%} | {capacity['maximumDrawdown']:.2%} | "
            f"{capacity['capturedBoards']} | {capacity['tradeCount']} | "
            f"{optimistic['monthlyReturn']:.2%} | {'是' if capacity['qualifiedBoth'] else '否'} |")
    lines.extend(["", "## 日期目录覆盖", "",
                  "| 月份 | 目录数 | 覆盖整月 | 请求范围工作日 | 缺失工作日（未扣除官方休市） | 完整月门禁 |",
                  "| --- | ---: | --- | ---: | --- | --- |"])
    for month, coverage in report["source"]["monthCoverage"].items():
        missing = "、".join(coverage["missingWeekdaysWithoutCalendarAdjustment"]) or "无"
        lines.append(
            f"| {month} | {coverage['observedDirectories']} | "
            f"{'是' if coverage['requestedRangeCoversWholeMonth'] else '否'} | "
            f"{coverage['weekdaySlotsInRequestedRange']} | {missing} | "
            f"{'是' if coverage['completeByDirectoryHeuristic'] else '否'} |")
    lines.extend(["", "## 描述性强因子（不作为样本外选参证据）", "",
                  "| 因子 | 捕获 AUC | 捕获中位数 | 未捕获中位数 | 二板中位数 | 三板中位数 |",
                  "| --- | ---: | ---: | ---: | ---: | ---: |"])
    for row in report["factorStatistics"][:10]:
        auc_value = row["aucForCapturedBoard"]
        lines.append(
            f"| `{row['factor']}` | {auc_value:.3f} | {row['capturedMedian']:.4g} | "
            f"{row['notCapturedMedian']:.4g} | {row['secondBoardMedian']:.4g} | "
            f"{row['thirdBoardMedian']:.4g} |" if auc_value is not None else
            f"| `{row['factor']}` | N/A | {row['capturedMedian']:.4g} | "
            f"{row['notCapturedMedian']:.4g} | {row['secondBoardMedian']:.4g} | {row['thirdBoardMedian']:.4g} |")
    lines.extend([
        "", "## 研究边界", "",
        "- 这是历史研究，不是收益承诺；50% 月收益是验收门槛，不是可保证结果。",
        "- Tick 成交不能证明涨停排队中的真实委托可成交，因此只把涨停价以下的后续成交作为买入上界，并另给容量口径。",
        "- 公司行动、停牌和官方前收盘仍需授权日线复核；本工具不修改 NAS 数据。",
        "- 任一特征、入场或退出窗口跨越超过 4 个自然日的目录间隔时保守剔除，避免把明显缺失交易日当作连续板；该规则不替代官方交易日历。",
        "- 描述性全样本因子只用于解释二板/三板差异；策略资格只看逐月 expanding walk-forward 结果。",
    ])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="二板/三板历史 Tick 因子与逐月样本外回测")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--name-cache", type=Path, required=True)
    parser.add_argument("--start", default="20260601")
    parser.add_argument("--end", default="20991231")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--qualification-through", default="999912",
                        help="last complete YYYYMM included in every-month qualification")
    parser.add_argument("--reuse-events", action="store_true",
                        help="reuse output-dir/events.jsonl.gz and recompute only analysis/report")
    args = parser.parse_args()

    started = time.monotonic()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    progress_path = output / "progress.jsonl"

    def progress(row: dict) -> None:
        row = {"observedAt": datetime.now().astimezone().isoformat(), **row}
        with progress_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")
        print(json.dumps(row, ensure_ascii=False), flush=True)

    root = args.root.resolve()
    name_cache = args.name_cache.resolve()
    names = load_names(name_cache)
    name_sha = sha256_file(name_cache)
    dates = discover_dates(root, args.start, args.end)
    coverage = month_coverage(dates, args.start, args.end)
    cache_path = output / "close-cache.json.gz"
    cached = load_close_cache(cache_path, dates, name_sha)
    if cached:
        closes, file_counts = cached
        progress({"stage": "close_cache_hit", "dates": len(dates)})
    else:
        closes, file_counts = build_close_cache(root, dates, names, args.workers, progress)
        save_close_cache(cache_path, dates, closes, file_counts, name_sha)
        progress({"stage": "close_cache_saved", "path": str(cache_path)})

    close_identity = hashlib.sha256()
    for day in dates:
        for symbol, value in sorted(closes[day].items()):
            close_identity.update(f"{day},{symbol},{value[0]:.2f},{value[1]}\n".encode("ascii"))
    events_path = output / "events.jsonl.gz"
    prior_report_path = output / "report.json"
    if args.reuse_events and events_path.exists() and prior_report_path.exists():
        with gzip.open(events_path, "rt", encoding="utf-8") as handle:
            events = [json.loads(line) for line in handle if line.strip()]
        prior_report = json.loads(prior_report_path.read_text(encoding="utf-8"))
        if prior_report.get("schemaVersion") != 2:
            raise ValueError("events cache schema is not current; run without --reuse-events")
        extraction = dict(prior_report.get("eventExtraction", {}))
        legacy_keys = int(extraction.pop("fullFilesParsed", 0))
        extraction.setdefault("fullFileKeysRequested", legacy_keys)
        full_file_tasks = 0
        if progress_path.exists():
            for line in progress_path.read_text(encoding="utf-8").splitlines():
                row = json.loads(line)
                if row.get("stage") == "full_file_plan":
                    full_file_tasks = int(row.get("uniqueFiles", 0))
        extraction["fullFileTasks"] = full_file_tasks
        extraction.setdefault("fullFileSummaries", full_file_tasks)
        progress({"stage": "events_cache_hit", "events": len(events)})
    else:
        events, extraction = build_events(root, dates, closes, names, args.workers, progress)
        with gzip.open(events_path, "wt", encoding="utf-8") as handle:
            for event in events:
                handle.write(json.dumps(event, ensure_ascii=False, separators=(",", ":")) + "\n")
    extraction["events"] = len(events)
    walk_forward = run_walk_forward(events, args.qualification_through, coverage)
    report = {
        "schemaVersion": 2,
        "generatedAt": datetime.now().astimezone().isoformat(),
        "researchOnly": True,
        "qualification": {
            "minimumMonthlyCapturedBoards": 5,
            "minimumMonthlyNetReturn": 0.50,
            "requiresEveryOutOfSampleMonth": True,
        },
        "execution": {
            "startingCapital": STARTING_CAPITAL,
            "commissionRate": COMMISSION_RATE,
            "minimumCommission": MINIMUM_COMMISSION,
            "stampDutyRate": STAMP_DUTY_RATE,
            "slippageEachSide": SLIPPAGE_RATE,
            "participationRateOfNext60Seconds": PARTICIPATION_RATE,
            "capacitySides": "minimum of entry and executable exit next-60-second notional",
            "lotShares": 100,
            "selection": "signal-time features only; future fill/exit availability cannot remove a Top-N candidate",
            "exit": "next trading day first Tick at or after 09:35 strictly above the rounded lower limit",
        },
        "source": {
            "root": str(root),
            "dates": dates,
            "monthCoverage": coverage,
            "directoryFileCounts": file_counts,
            "validCloseCounts": {day: len(closes[day]) for day in dates},
            "nameCache": str(name_cache),
            "nameCacheSha256": name_sha,
            "closeIdentitySha256": close_identity.hexdigest().upper(),
        },
        "eventExtraction": extraction,
        "groupStatistics": group_statistics(events),
        "factorStatistics": factor_statistics(events, "09:45:00"),
        "walkForward": walk_forward,
        "durationSeconds": time.monotonic() - started,
    }
    report["verification"] = verify_report(report, events)
    report_path = output / "report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    write_markdown(report, output / "report.md")
    progress({
        "stage": "completed",
        "events": len(events),
        "oosMonths": len(walk_forward["evaluations"]),
        "allOosMonthsMeetBoth": walk_forward["allOosMonthsMeetBoth"],
        "durationSeconds": report["durationSeconds"],
    })
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
