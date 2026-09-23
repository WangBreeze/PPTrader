from __future__ import annotations

import csv
from datetime import date
import json
from pathlib import Path
import tempfile
import unittest

from scripts.build_limit_up_strategy_plan import build_plan


def write_ticks(path: Path, close: str, after_close: str | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["time", "price", "volume", "amount", "type"])
        writer.writerow(["09:25:00", close, "1", close, "B"])
        writer.writerow(["15:00:00", close, "1", close, "B"])
        if after_close is not None:
            writer.writerow(["15:15:00", after_close, "1", after_close, "S"])


class LimitUpStrategyPlanTest(unittest.TestCase):
    def test_freezes_second_and_third_board_without_using_after_close_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            days = [date(2026, 9, 9), date(2026, 9, 10), date(2026, 9, 11)]
            names = root / "names.json"
            names.write_text(json.dumps([
                {"code": "000001", "name": "甲公司", "market": "深圳"},
                {"code": "000002", "name": "乙公司", "market": "深圳"},
                {"code": "000003", "name": "ST丙", "market": "深圳"},
            ], ensure_ascii=False), encoding="utf-8")

            prices = {
                "000001": ("10.00", "11.00", "12.10"),
                "000002": ("10.00", "10.50", "11.55"),
                "000003": ("10.00", "11.00", "12.10"),
            }
            for symbol, closes in prices.items():
                for day, close in zip(days, closes):
                    write_ticks(
                        root / day.strftime("%Y%m%d") / f"{symbol}.csv",
                        close,
                        "99.99" if day == days[-1] else None,
                    )

            plan = build_plan(root, names, days, date(2026, 9, 14))
            self.assertEqual(plan["groups"]["thirdBoard"], ["000001"])
            self.assertEqual(plan["groups"]["secondBoard"], ["000002"])
            self.assertEqual(plan["groups"]["reversalBoard"], [])
            self.assertEqual(plan["historyThrough"], "2026-09-11")
            self.assertEqual(plan["targetTradingDay"], "2026-09-14")
            self.assertEqual(plan["source"]["excluded"]["stOrDelistingName"], 1)
            details = {row["symbol"]: row for row in plan["candidateDetails"]}
            self.assertEqual(details["000001"]["closes"][-1], "12.10")


if __name__ == "__main__":
    unittest.main()
