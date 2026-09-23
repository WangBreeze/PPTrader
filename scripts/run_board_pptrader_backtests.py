"""串行调用 PPTrader 批量回测程序；不实现第二套交易逻辑。"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--qt-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    args.output.mkdir(parents=True, exist_ok=True)
    if not args.database.is_file() or not args.probe.is_file():
        parser.error("database and PPTrader probe must exist")
    environment = os.environ.copy()
    environment["PATH"] = str(args.qt_bin) + os.pathsep + environment.get("PATH", "")
    rows = []
    totals = defaultdict(lambda: {"cases": 0, "succeeded": 0, "failed": 0,
                                 "buySignals": 0, "completedTrades": 0,
                                 "capturedClosedBoards": 0,
                                 "entryDayClosedBoardSignals": 0,
                                 "entryDayOutcomeKnownSignals": 0,
                                 "missedClosedBoards": 0, "forcedExits": 0,
                                 "openPositions": 0, "tPlusOneViolations": 0})
    for event in manifest["events"]:
        strategy = ("tick_second_board" if event["group"] == "secondBoard"
                    else "tick_third_board")
        key = f"{event['targetDate']}-{event['symbol']}-{event['group']}"
        result_file = args.output / f"{key}.json"
        command = [str(args.probe), "--existing-db", str(args.database),
                   "--no-archive", "--symbols", event["symbol"],
                   "--bars", "0", "--period", "60", "--data-mode", "raw_ticks",
                   "--strategy", strategy,
                   "--start-date", f"{event['window'][0][:4]}-{event['window'][0][4:6]}-{event['window'][0][6:]}",
                   "--end-date", f"{event['window'][-1][:4]}-{event['window'][-1][4:6]}-{event['window'][-1][6:]}",
                   "--output", str(result_file)]
        process = subprocess.run(command, env=environment, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.PIPE, timeout=180, check=False)
        group_key = f"{event['targetDate'][:6]}:{event['group']}"
        aggregate = totals[group_key]
        aggregate["cases"] += 1
        row = {"targetDate": event["targetDate"], "symbol": event["symbol"],
               "group": event["group"], "strategyId": strategy,
               "report": result_file.name, "processExitCode": process.returncode,
               "stderr": process.stderr.decode(errors="replace")[-2000:]}
        if process.returncode == 0 and result_file.is_file():
            report = json.loads(result_file.read_text(encoding="utf-8"))
            backtest = report.get("backtest", {})
            board = backtest.get("boardResults", [{}])
            board = board[0] if board else {}
            row.update({"completed": backtest.get("completed", 0),
                        "succeeded": backtest.get("succeeded", 0),
                        "errors": backtest.get("errors", []),
                        "board": board,
                        "tPlusOneViolationCount": backtest.get("tPlusOneViolationCount", 0),
                        "researchOnlySample": True})
            if row["completed"] == 1 and row["succeeded"] == 1 and board:
                aggregate["succeeded"] += 1
                for source, target in (("buySignals", "buySignals"),
                                       ("completedTrades", "completedTrades"),
                                       ("capturedClosedBoards", "capturedClosedBoards"),
                                       ("entryDayClosedBoardSignals", "entryDayClosedBoardSignals"),
                                       ("entryDayOutcomeKnownSignals", "entryDayOutcomeKnownSignals"),
                                       ("missedClosedBoards", "missedClosedBoards"),
                                       ("forcedExits", "forcedExits")):
                    aggregate[target] += int(board.get(source, 0))
                aggregate["openPositions"] += int(board.get("openPosition", False))
                aggregate["tPlusOneViolations"] += row["tPlusOneViolationCount"]
            else:
                aggregate["failed"] += 1
        else:
            aggregate["failed"] += 1
        rows.append(row)
        print(f"{len(rows)}/{len(manifest['events'])} {key} "
              f"success={row.get('succeeded', 0)} "
              f"captures={row.get('board', {}).get('capturedClosedBoards', 0)}",
              flush=True)

    summary = {
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "engine": "PPTrader pptrader_real_load_probe / BatchBacktestService / BacktestEngine",
        "probeSha256": sha256(args.probe),
        "manifestSha256": sha256(args.manifest),
        "databaseSha256After": sha256(args.database),
        "strictDataQualityGate": True,
        "archiveSkipped": True,
        "caseCount": len(rows),
        "byMonthAndGroup": dict(sorted(totals.items())),
        "cases": rows,
    }
    (args.output.parent / "pptrader-summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0 if all(value["failed"] == 0 for value in totals.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
