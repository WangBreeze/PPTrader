import sqlite3
from pathlib import Path

database_path = Path(__file__).parents[2] / "out/build/vs2022-x64/Debug/data/pptrader.db"
connection = sqlite3.connect(f"file:{database_path.as_posix()}?mode=ro", uri=True)
try:
    raw_days = connection.execute(
        "select trading_date, count(*) from raw_trade_ticks "
        "where symbol = ? and trading_date between ? and ? "
        "group by trading_date order by trading_date",
        ("600519", "2026-08-05", "2026-08-07"),
    ).fetchall()
    candles = connection.execute(
        "select substr(start_time_utc, 1, 10), count(*) from candles "
        "where symbol = ? and period_seconds = 60 and source = ? "
        "group by substr(start_time_utc, 1, 10) order by 1 desc limit 5",
        ("600519", "raw_tick_cache_v1"),
    ).fetchall()
    print({"rawDays": raw_days, "recentOneMinuteCandles": candles})
finally:
    connection.close()
