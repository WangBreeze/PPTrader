#!/usr/bin/env python3
"""Capture a read-only SQLite snapshot after a visible batch-backtest acceptance run."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import sqlite3
from pathlib import Path


ARCHIVE_TABLES = (
    "backtest_results",
    "backtest_candles",
    "backtest_equity_points",
    "backtest_raw_ticks",
    "backtest_trades",
    "portfolio_backtest_results",
)
BATCH_TABLES = ("batch_backtest_runs", "batch_backtest_run_items")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(8 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def query_one(connection: sqlite3.Connection, statement: str) -> list[object]:
    return list(connection.execute(statement).fetchone())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-rounds", type=int, required=True)
    parser.add_argument("--expected-tasks", type=int, required=True)
    args = parser.parse_args()

    with args.baseline.open(encoding="utf-8") as handle:
        baseline = json.load(handle)

    database_uri = f"file:{args.database.as_posix()}?mode=ro"
    connection = sqlite3.connect(database_uri, uri=True)
    try:
        facts = {
            "raw_trade_ticks": query_one(
                connection,
                "select count(*), min(id), max(id), sum(id), sum(source_line), "
                "round(sum(price),6), round(sum(volume_shares),6), round(sum(amount),6), "
                "min(timestamp_utc), max(timestamp_utc) from raw_trade_ticks",
            ),
            "ticks": query_one(
                connection,
                "select count(*), coalesce(min(id),0), coalesce(max(id),0), "
                "coalesce(sum(id),0), coalesce(sum(price),0), coalesce(sum(volume),0), "
                "coalesce(sum(amount),0), min(timestamp_utc), max(timestamp_utc) from ticks",
            ),
            "candles": query_one(
                connection,
                "select count(*), min(id), max(id), sum(id), "
                "round(sum(open+high+low+close),6), "
                "round(sum(volume),6), min(start_time_utc), max(start_time_utc) from candles",
            ),
            "market_snapshots": query_one(
                connection,
                "select count(*), min(snapshot_time_utc), max(snapshot_time_utc), "
                "coalesce(sum(expected_count),0), coalesce(sum(received_count),0) "
                "from market_snapshots",
            ),
        }
        tables = ARCHIVE_TABLES + BATCH_TABLES
        table_counts = {
            table: connection.execute(f"select count(*) from {table}").fetchone()[0]
            for table in tables
        }
        run_statuses = dict(
            connection.execute(
                "select status, count(*) from batch_backtest_runs group by status"
            ).fetchall()
        )
        item_statuses = dict(
            connection.execute(
                "select status, count(*) from batch_backtest_run_items group by status"
            ).fetchall()
        )
        batch_checks = {
            "orphanItems": connection.execute(
                "select count(*) from batch_backtest_run_items i "
                "left join batch_backtest_runs r on r.run_id=i.run_id "
                "where r.run_id is null"
            ).fetchone()[0],
            "nonTerminalRuns": connection.execute(
                "select count(*) from batch_backtest_runs "
                "where status in ('ready','running')"
            ).fetchone()[0],
            "nonTerminalItems": connection.execute(
                "select count(*) from batch_backtest_run_items "
                "where status in ('ready','running')"
            ).fetchone()[0],
            "runErrors": connection.execute(
                "select count(*) from batch_backtest_runs where error<>''"
            ).fetchone()[0],
            "itemErrors": connection.execute(
                "select count(*) from batch_backtest_run_items where error<>''"
            ).fetchone()[0],
            "runTotalSymbols": connection.execute(
                "select coalesce(sum(total_symbols),0) from batch_backtest_runs"
            ).fetchone()[0],
            "runCompletedSymbols": connection.execute(
                "select coalesce(sum(completed_symbols),0) from batch_backtest_runs"
            ).fetchone()[0],
            "runSucceededSymbols": connection.execute(
                "select coalesce(sum(succeeded_symbols),0) from batch_backtest_runs"
            ).fetchone()[0],
            "runFailedSymbols": connection.execute(
                "select coalesce(sum(failed_symbols),0) from batch_backtest_runs"
            ).fetchone()[0],
        }
        integrity = connection.execute("pragma integrity_check").fetchone()[0]
        user_version = connection.execute("pragma user_version").fetchone()[0]
    finally:
        connection.close()

    comparison = {
        "integrityMatches": integrity == "ok",
        "userVersionMatches": user_version == baseline["userVersion"],
        "factSignaturesMatch": facts == baseline["factSignatures"],
        "archiveCountsUnchanged": all(
            table_counts[table] == baseline["tableCounts"][table]
            for table in ARCHIVE_TABLES
        ),
        "batchRunCountMatchesRounds": (
            table_counts["batch_backtest_runs"] == args.expected_rounds
        ),
        "batchItemCountMatchesTasks": (
            table_counts["batch_backtest_run_items"] == args.expected_tasks
        ),
        "batchStatusesTerminal": (
            batch_checks["nonTerminalRuns"] == 0
            and batch_checks["nonTerminalItems"] == 0
            and batch_checks["orphanItems"] == 0
        ),
    }
    wal_path = Path(f"{args.database}-wal")
    result = {
        "capturedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "databasePath": str(args.database),
        "databaseBytes": args.database.stat().st_size,
        "databaseSha256": sha256_file(args.database),
        "walBytes": wal_path.stat().st_size if wal_path.exists() else 0,
        "userVersion": user_version,
        "integrityCheck": integrity,
        "factSignatures": facts,
        "tableCounts": table_counts,
        "batchRunStatuses": run_statuses,
        "batchItemStatuses": item_statuses,
        "batchChecks": batch_checks,
        "baselineComparison": comparison,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary_output = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary_output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    os.replace(temporary_output, args.output)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if all(comparison.values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
