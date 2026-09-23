#!/usr/bin/env python3
"""回归 Windows CP936 重定向下的 mootdx JSON 行协议。"""

import io
import json
import runpy
import sys
import types


def load_bridge(path):
    mootdx = types.ModuleType("mootdx")
    quotes = types.ModuleType("mootdx.quotes")
    server = types.ModuleType("mootdx.server")
    quotes.Quotes = object
    server.server = []
    sys.modules.setdefault("mootdx", mootdx)
    sys.modules.setdefault("mootdx.quotes", quotes)
    sys.modules.setdefault("mootdx.server", server)
    return runpy.run_path(path)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_mootdx_bridge_protocol.py BRIDGE_PATH")
    bridge = load_bridge(sys.argv[1])
    raw = io.BytesIO()
    redirected = io.TextIOWrapper(raw, encoding="cp936", newline="\n")
    original = sys.stdout
    try:
        sys.stdout = redirected
        bridge["emit"]({"status": "行情已停止", "error": "无"})
        redirected.flush()
    finally:
        sys.stdout = original
    payload = raw.getvalue()
    payload.decode("ascii")
    decoded = json.loads(payload.decode("utf-8"))
    assert decoded == {"status": "行情已停止", "error": "无"}


if __name__ == "__main__":
    main()
