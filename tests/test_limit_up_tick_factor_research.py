from __future__ import annotations

import tempfile
from pathlib import Path
import unittest

from scripts.research_limit_up_tick_factors import (
    EXIT_SECONDS,
    execute_trade,
    has_large_calendar_gap,
    limit_price,
    month_coverage,
    parse_seconds,
    read_close_tail,
    sample_for,
    simulate_month,
    summarize_tick_day,
)


class LimitUpTickFactorResearchTest(unittest.TestCase):
    def make_csv(self, root: Path, rows: list[str]) -> Path:
        path = root / "000001.csv"
        path.write_text("time,price,volume,amount,type\n" + "\n".join(rows) + "\n",
                        encoding="utf-8")
        return path

    def test_limit_rounding_and_close_cutoff(self) -> None:
        self.assertEqual(11.01, limit_price(10.01))
        with tempfile.TemporaryDirectory() as directory:
            path = self.make_csv(Path(directory), [
                "09:30:00,10.20,1,1020,B",
                "15:00:00,11.01,2,2202,B",
                "15:10:00,10.80,1,1080,S",
            ])
            self.assertEqual((11.01, 15 * 3600), read_close_tail(path))

    def test_board_break_and_fill_are_measured_before_exit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self.make_csv(Path(directory), [
                "09:30:00,10.20,10,10200,B",
                "09:35:00,10.50,10,10500,B",
                "09:36:00,11.00,10,11000,B",
                "09:37:00,10.90,10,10900,S",
                "09:45:00,10.85,10,10850,S",
                "09:45:01,10.80,50,54000,S",
                "09:46:00,11.00,50,55000,B",
                "15:00:00,11.00,10,11000,B",
            ])
            summary = summarize_tick_day(path, 10.00)
            assert summary is not None
            self.assertEqual(2, summary["touchSegments"])
            self.assertEqual(1, summary["breakCount"])
            self.assertTrue(summary["closedLimit"])
            snap = summary["snapshots"][9 * 3600 + 45 * 60]
            self.assertEqual(10.80, snap["entry"][1])
            self.assertEqual(9 * 3600 + 45 * 60 + 1, snap["entry"][0])
            self.assertTrue(snap["hitAfterEntry"])
            self.assertGreater(snap["entryNotional60s"], 0)

    def test_capacity_execution_does_not_invent_full_fill(self) -> None:
        sample = {
            "row": {
                "entryPrice": 10.0,
                "exitPrice": 11.0,
                "entryNotional60s": 2_000_000.0,
                "exitNotional60s": 200_000.0,
            }
        }
        fill = execute_trade(1_000_000.0, 1_000_000.0, sample, True)
        assert fill is not None
        self.assertEqual(100, fill["shares"])
        self.assertLess(fill["deployed"], 2_000.0)

    def test_exit_waits_until_price_leaves_lower_limit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self.make_csv(Path(directory), [
                "09:30:00,9.00,10,9000,S",
                "09:35:00,9.00,10,9000,S",
                "09:36:00,9.00,10,9000,S",
                "09:37:00,9.01,10,9010,B",
                "09:38:00,9.02,10,9020,B",
                "15:00:00,9.20,10,9200,B",
            ])
            summary = summarize_tick_day(path, 10.00)
            assert summary is not None
            exit_tick = summary["snapshots"][EXIT_SECONDS]["exit"]
            self.assertEqual(9 * 3600 + 37 * 60, exit_tick[0])
            self.assertEqual(9.01, exit_tick[1])

    def test_signal_sample_does_not_peek_at_future_fill_or_exit(self) -> None:
        event = {
            "cutoffs": {
                "09:45:00": {
                    "lastPrice": 10.0,
                    "entryPrice": None,
                    "exitPrice": None,
                    "netReturn": None,
                    "hitAfterEntry": False,
                    "features": {"early_return": 0.0},
                }
            }
        }
        sample = sample_for(event, "09:45:00")
        self.assertIsNotNone(sample)
        self.assertIsNone(sample["return"])

    def test_unfilled_top_ranked_signal_consumes_top_n_slot(self) -> None:
        def candidate(score: float, entry: float | None) -> dict:
            return {
                "event": {"targetDate": "20260803", "symbol": f"{score:.0f}",
                          "group": "thirdBoard"},
                "score": score,
                "row": {
                    "features": {"early_return": 0.01},
                    "touchedBeforeSignal": False,
                    "entryPrice": entry,
                    "exitPrice": 11.0 if entry is not None else None,
                    "entryNotional60s": 1_000_000.0,
                    "exitNotional60s": 1_000_000.0,
                    "entrySeconds": 9 * 3600 + 36 * 60,
                    "exitSeconds": EXIT_SECONDS,
                    "hitAfterEntry": entry is not None,
                },
            }
        result = simulate_month(
            [candidate(2.0, None), candidate(1.0, 10.0)],
            {"group": "both", "minimumScore": 0.0, "topN": 1,
             "maximumEntryReturn": 0.05, "excludeTouchedBeforeSignal": False},
            True)
        self.assertEqual(0, result["tradeCount"])

    def test_large_calendar_gap_is_not_treated_as_continuous_board_history(self) -> None:
        self.assertTrue(has_large_calendar_gap(["20260713", "20260720"]))
        self.assertFalse(has_large_calendar_gap(["20260710", "20260713"]))

    def test_time_parser(self) -> None:
        self.assertEqual(EXIT_SECONDS, parse_seconds("09:35:00"))
        self.assertIsNone(parse_seconds("25:00:00"))

    def test_month_coverage_marks_missing_weekdays(self) -> None:
        coverage = month_coverage(
            ["20260713", "20260720", "20260721"], "20260713", "20260721")
        self.assertEqual(
            ["2026-07-14", "2026-07-15", "2026-07-16", "2026-07-17"],
            coverage["202607"]["missingWeekdaysWithoutCalendarAdjustment"])
        self.assertFalse(coverage["202607"]["completeByDirectoryHeuristic"])

    def test_month_coverage_rejects_partial_calendar_month(self) -> None:
        coverage = month_coverage(
            ["20260901", "20260902", "20260903"], "20260901", "20260903")
        self.assertEqual([], coverage["202609"]["missingWeekdaysWithoutCalendarAdjustment"])
        self.assertFalse(coverage["202609"]["requestedRangeCoversWholeMonth"])
        self.assertFalse(coverage["202609"]["completeByDirectoryHeuristic"])


if __name__ == "__main__":
    unittest.main()
