"""只读汇总模拟流程验收，核验隔离 SQLite；不访问 NAS、网络或开发库。"""
import argparse
import json
import math
import sqlite3
from pathlib import Path


def pctl(values, p):
    values = sorted(values)
    return values[max(0, math.ceil(p * len(values)) - 1)] if values else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--directories", nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    summaries = []
    stage_signatures = []
    for name in args.directories:
        directory = (args.root / name).resolve()
        if directory.parent != args.root.resolve():
            raise ValueError("仅允许证据根目录的直接子目录")
        read = lambda filename: json.loads((directory / filename).read_text(encoding="utf-8-sig"))
        report, launch, exit_info = read("report.json"), read("launch.json"), read("exit.json")
        resources = [json.loads(line) for line in (directory / "resources.jsonl").read_text(encoding="utf-8-sig").splitlines() if line]
        db = directory / "isolated.db"
        connection = sqlite3.connect(db.as_uri() + "?mode=ro", uri=True)
        try:
            integrity = connection.execute("pragma integrity_check").fetchone()[0]
            schema = connection.execute("pragma user_version").fetchone()[0]
            counts = {table: connection.execute(f"select count(*) from {table}").fetchone()[0]
                      for table in ("ticks", "candles", "raw_trade_ticks", "market_snapshots", "backtest_results", "portfolio_backtest_results", "batch_backtest_runs", "batch_backtest_run_items")}
        finally:
            connection.close()
        runs = report["runs"]
        signature_keys = ("definitionSha256", "barCount", "signalCount", "orderCount", "fillCount", "cash", "quantity", "commission", "lastBarUtc", "lastQuoteUtc")
        signatures = [json.dumps({key: row[key] for key in signature_keys}, sort_keys=True) for row in runs]
        if signatures:
            stage_signatures.append(signatures[0])
        t1_errors = 0
        for row in runs:
            last_buy_date = None
            for event in row["events"]:
                if event["kind"] != "fill":
                    continue
                date = event["atUtc"][:10]  # 本轮 A 股源时刻均在同一 UTC 日。
                if event["message"].startswith("买入"):
                    last_buy_date = date
                elif event["message"].startswith("卖出") and last_buy_date == date:
                    t1_errors += 1
        summaries.append({
            "directory": name, "candidates": len(runs), "processId": launch["processId"],
            "binarySha256": launch["binarySha256"], "flowPassed": report["flowPassed"],
            "exit": exit_info, "inputTicks": report["ticksConsumed"], "barsPerRun": [r["barCount"] for r in runs],
            "fillsPerRun": [r["fillCount"] for r in runs], "rejections": sum(r["rejectedCount"] for r in runs),
            "distinctAccountIds": len({r["runId"] for r in runs}), "identicalFixedInputResults": len(set(signatures)) == 1,
            "tPlusOneViolationsInRecordedFills": t1_errors, "elapsedMs": report["elapsedMs"],
            "sliceP95Ms": report["sliceP95Ms"], "sliceMaxMs": report["sliceMaxMs"],
            "heartbeatP95Ms": report["heartbeatP95Ms"], "heartbeatMaxMs": report["heartbeatMaxMs"],
            "privateMBp50": pctl([r["privateMB"] for r in resources], .5),
            "privateMBp95": pctl([r["privateMB"] for r in resources], .95),
            "privateMBmax": pctl([r["privateMB"] for r in resources], 1),
            "workingSetMBmax": pctl([r["workingSetMB"] for r in resources], 1),
            "handlesMax": max(r["handles"] for r in resources), "threadsMax": max(r["threads"] for r in resources),
            "stderrBytes": (directory / "stderr.log").stat().st_size,
            "database": {"integrity": integrity, "schema": schema, "counts": counts},
        })
    passed = all(r["flowPassed"] and r["exit"]["exitCode"] == 0 and not r["exit"]["forced"]
                 and r["distinctAccountIds"] == r["candidates"] and r["identicalFixedInputResults"]
                 and r["tPlusOneViolationsInRecordedFills"] == 0 and r["database"]["integrity"] == "ok"
                 and not any(r["database"]["counts"].values()) for r in summaries)
    cross_stage_deterministic = len(stage_signatures) == len(summaries) and len(set(stage_signatures)) == 1
    passed = passed and cross_stage_deterministic
    result = {"passed": passed, "crossStageDeterministic": cross_stage_deterministic, "profitabilityAccepted": False, "stages": summaries,
              "scope": "单一 SMA5/WMA10 策略，000001 三日指定窗口；非全天数据、NAS I/O 长稳、真实交易或收益证明"}
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
