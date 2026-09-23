"""只读暂存二板/三板原始 Tick 样本，实际回测由 PPTrader 执行。"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import re
import shlex
import subprocess
from collections import defaultdict
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--events", type=Path, required=True)
    parser.add_argument("--dates", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--per-group-month", type=int, default=6)
    parser.add_argument("--selection", choices=("first", "distributed"), default="first")
    parser.add_argument("--skip-first", type=int, default=0)
    parser.add_argument("--host", default="wanglc@192.168.3.7")
    args = parser.parse_args()
    if args.per_group_month < 1 or args.per_group_month > 20:
        parser.error("--per-group-month must be between 1 and 20")

    with gzip.open(args.dates, "rt", encoding="utf-8") as stream:
        dates = json.load(stream)["dates"]
    date_index = {day: index for index, day in enumerate(dates)}
    candidates: dict[tuple[str, str], list[dict]] = defaultdict(list)
    with gzip.open(args.events, "rt", encoding="utf-8") as stream:
        for line in stream:
            event = json.loads(line)
            month = event["month"]
            group = event["group"]
            if month not in {"202607", "202608", "202609"}:
                continue
            cutoff = event["cutoffs"]["09:35:00"]
            factor = cutoff["features"]
            maximum_seal = 90 if group == "secondBoard" else 60
            if (factor["prior_final_seal_minute"] > maximum_seal
                or factor["early_return"] < 0.01
                or factor["early_drawdown"] > 0.03
                or cutoff["touchedBeforeSignal"]):
                continue
            key = (month, group)
            candidates[key].append(event)

    expected_groups = {(month, group) for month in ("202607", "202608", "202609")
                       for group in ("secondBoard", "thirdBoard")}
    if set(candidates) != expected_groups:
        raise SystemExit(f"missing group/month: {sorted(expected_groups - set(candidates))}")
    selected: dict[tuple[str, str], list[dict]] = {}
    for key in sorted(expected_groups):
        # 与首轮完全使用同一事件缓存顺序，skip-first 才不会重复首轮。
        ordered = candidates[key]
        if args.selection == "first":
            selected[key] = ordered[:args.per_group_month]
        else:
            remaining = ordered[args.skip_first:]
            count = min(args.per_group_month, len(remaining))
            if count == 0:
                raise SystemExit(f"no remaining candidates: {key}")
            indices = ([len(remaining) - 1] if count == 1 else
                       [round(index * (len(remaining) - 1) / (count - 1))
                        for index in range(count)])
            selected[key] = [remaining[index] for index in indices]

    files: set[str] = set()
    manifest_events = []
    for key in sorted(selected):
        for event in selected[key]:
            day = event["targetDate"]
            symbol = event["symbol"]
            if not re.fullmatch(r"\d{8}", day) or not re.fullmatch(r"\d{6}", symbol):
                raise SystemExit("invalid date or symbol in event cache")
            index = date_index[day]
            history = 3 if event["group"] == "thirdBoard" else 2
            if index < history or index + 1 >= len(dates):
                raise SystemExit(f"incomplete trading-day window: {day}/{symbol}")
            window = dates[index - history:index + 2]
            for trading_day in window:
                files.add(f"{trading_day}/{symbol}.csv")
            manifest_events.append({
                "targetDate": day,
                "symbol": symbol,
                "group": event["group"],
                "window": window,
                "targetClosedLimitForPostRunComparisonOnly": event["closedLimit"],
            })

    destination = args.destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    if any(destination.iterdir()):
        raise SystemExit(f"destination must be empty: {destination}")
    sorted_files = sorted(files)
    remote_root = "/home/wanglc/workspace/TickData/data"
    remote_command = shlex.join(["tar", "-czf", "-", "-C", remote_root,
                                  "--", *sorted_files])
    ssh = subprocess.Popen(
        ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
         args.host, remote_command],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    assert ssh.stdout is not None
    extractor = subprocess.Popen(
        ["tar", "-xzf", "-", "-C", str(destination)],
        stdin=ssh.stdout, stderr=subprocess.PIPE,
    )
    ssh.stdout.close()
    extract_error = extractor.communicate()[1]
    assert ssh.stderr is not None
    ssh_error = ssh.stderr.read()
    ssh.wait()
    if ssh.returncode != 0 or extractor.returncode != 0:
        raise SystemExit(f"transfer failed: ssh={ssh.returncode} tar={extractor.returncode}; "
                         f"{ssh_error.decode(errors='replace')} "
                         f"{extract_error.decode(errors='replace')}")
    missing = [name for name in sorted_files if not (destination / name).is_file()]
    if missing:
        raise SystemExit(f"missing {len(missing)} staged files: {missing[:5]}")
    file_hashes = {name: hashlib.sha256((destination / name).read_bytes()).hexdigest()
                   for name in sorted_files}
    manifest = {
        "selection": ("各月各板组取最早 N 个" if args.selection == "first" else
                      f"各月各板组跳过最早 {args.skip_first} 个后均匀取 N 个")
                     + "；仅使用目标日 09:35 前因子，不按未来是否封板选样",
        "perGroupMonth": args.per_group_month,
        "nasRootReadOnly": f"{args.host}:{remote_root}",
        "events": manifest_events,
        "fileCount": len(sorted_files),
        "symbols": sorted({item["symbol"] for item in manifest_events}),
        "filesSha256": file_hashes,
    }
    (destination.parent / "staging-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"events": len(manifest_events), "files": len(sorted_files),
                      "symbols": len(manifest["symbols"]),
                      "bytes": sum((destination / name).stat().st_size
                                   for name in sorted_files)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
