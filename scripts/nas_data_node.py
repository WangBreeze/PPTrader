#!/usr/bin/env python3
"""只读 TickData 局域网数据节点。

根目录、监听地址、证书和令牌只从命令行/环境取得；API 永不返回服务器
绝对路径。默认仅监听回环地址，监听局域网地址时必须配置令牌。
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import re
import ssl
from datetime import datetime, timedelta
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

SYMBOL_RE = re.compile(r"^[0-9]{6}$")
DATE_RE = re.compile(r"^[0-9]{8}$")
SERVER_VERSION = "pptrader-nas/1"
MAX_MANIFEST_SYMBOLS = 6000
MAX_MANIFEST_DAYS = 31
MAX_MANIFEST_ENTRIES = 12000
MAX_MANIFEST_BODY_BYTES = 128 * 1024


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


class TickDataNode:
    def __init__(self, root: Path, token: str, token_file: str = "") -> None:
        self.root = root.resolve(strict=True)
        self.token_file = token_file
        self.environment_token = token
        self.reload_tokens()

    def reload_tokens(self) -> None:
        """重建有效 Token 集合：环境 Token 始终保留，文件内容整体替换，
        使被撤销的旧 Token 在文件更新后立即失效。每次鉴权前调用。"""
        tokens: set[str] = set()
        if self.environment_token:
            tokens.add(self.environment_token)
        if not self.token_file:
            self.tokens = tokens
            return
        path = Path(self.token_file)
        if path.is_file():
            try:
                for line in path.read_text(encoding="utf-8").splitlines():
                    value = line.strip()
                    if value:
                        tokens.add(value)
            except OSError:
                pass
        self.tokens = tokens

    def tick_path(self, symbol: str, trading_date: str) -> Path | None:
        if not SYMBOL_RE.fullmatch(symbol) or not DATE_RE.fullmatch(trading_date):
            return None
        candidate = (self.root / trading_date / f"{symbol}.csv").resolve()
        try:
            candidate.relative_to(self.root)
        except ValueError:
            return None
        return candidate if candidate.is_file() else None

    def catalog(self, symbol: str, start: str, end: str) -> list[dict[str, object]]:
        if not SYMBOL_RE.fullmatch(symbol):
            return []
        if not DATE_RE.fullmatch(start) or not DATE_RE.fullmatch(end) or start > end:
            return []
        try:
            first = datetime.strptime(start, "%Y%m%d").date()
            last = datetime.strptime(end, "%Y%m%d").date()
        except ValueError:
            return []
        if (last - first).days > 366:
            return []
        entries: list[dict[str, object]] = []
        # 只检查用户给定的最多 367 个确定路径，不枚举或递归扫描数据根目录。
        current = first
        while current <= last:
            name = current.strftime("%Y%m%d")
            path = self.tick_path(symbol, name)
            if path is not None:
                stat = path.stat()
                digest = sha256_file(path)
                entries.append({
                    "symbol": symbol,
                    "tradingDate": current.isoformat(),
                    "format": "tick_csv",
                    "size": stat.st_size,
                    "sha256": digest,
                    "version": f"sha256:{digest}",
                    "downloadPath": f"/v1/ticks/{symbol}/{name}",
                })
            current += timedelta(days=1)
        entries.sort(key=lambda item: str(item["tradingDate"]))
        return entries

    def changes(self, since: str = "") -> dict[str, object]:
        """变更摘要：只扫描根目录下一层日期目录的修改时间，不递归文件。
        since 为 epoch 毫秒；返回目录总数、最新目录时间与变更目录列表。
        仅用于提示 catalog 变化，不触发下载或回测。"""
        since_ms = 0
        if since:
            try:
                since_ms = int(since)
            except ValueError:
                return {
                    "schemaVersion": 1,
                    "error": "since must be epoch milliseconds",
                }
        changed: list[dict[str, object]] = []
        latest = 0
        total = 0
        for child in self.root.iterdir():
            if not child.is_dir() or not DATE_RE.fullmatch(child.name):
                continue
            total += 1
            try:
                modified_ms = int(child.stat().st_mtime * 1000)
            except OSError:
                continue
            if modified_ms > since_ms:
                changed.append({
                    "tradingDate": child.name,
                    "modifiedAt": modified_ms,
                })
            latest = max(latest, modified_ms)
        changed.sort(key=lambda item: str(item["tradingDate"]))
        return {
            "schemaVersion": 1,
            "totalDirectories": total,
            "latestDirectoryAt": latest,
            "changedDirectories": changed,
        }

    def manifest(self, symbols: list[str], start: str, end: str) -> list[dict[str, object]]:
        if not symbols or len(symbols) > MAX_MANIFEST_SYMBOLS:
            raise ValueError("invalid_symbol_count")
        if len(set(symbols)) != len(symbols) or any(
                not SYMBOL_RE.fullmatch(symbol) for symbol in symbols):
            raise ValueError("invalid_symbols")
        if not DATE_RE.fullmatch(start) or not DATE_RE.fullmatch(end) or start > end:
            raise ValueError("invalid_date_range")
        try:
            first = datetime.strptime(start, "%Y%m%d").date()
            last = datetime.strptime(end, "%Y%m%d").date()
        except ValueError as exc:
            raise ValueError("invalid_date_range") from exc
        if (last - first).days >= MAX_MANIFEST_DAYS:
            raise ValueError("manifest_range_too_wide")
        entries: list[dict[str, object]] = []
        # 仅检查用户显式列出的代码与日期笛卡尔积，不枚举目录或递归扫描仓库。
        current = first
        while current <= last:
            name = current.strftime("%Y%m%d")
            for symbol in symbols:
                path = self.tick_path(symbol, name)
                if path is None:
                    continue
                stat = path.stat()
                digest = sha256_file(path)
                entries.append({
                    "symbol": symbol,
                    "tradingDate": current.isoformat(),
                    "format": "tick_csv",
                    "size": stat.st_size,
                    "sha256": digest,
                    "version": f"sha256:{digest}",
                    "downloadPath": f"/v1/ticks/{symbol}/{name}",
                })
                if len(entries) > MAX_MANIFEST_ENTRIES:
                    raise ValueError("manifest_entry_limit_exceeded")
            current += timedelta(days=1)
        return entries


class Handler(BaseHTTPRequestHandler):
    server_version = SERVER_VERSION

    @property
    def node(self) -> TickDataNode:
        return self.server.node  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: object) -> None:
        # 不记录 Authorization、查询字符串或服务器文件路径。
        print(f"{self.client_address[0]} {self.command} {urlparse(self.path).path} " + fmt % args)

    def json_response(self, status: HTTPStatus, body: object) -> None:
        payload = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(payload)

    def authorized(self) -> bool:
        self.node.reload_tokens()
        if not self.node.tokens:
            return True
        supplied = self.headers.get("Authorization", "")
        for token in self.node.tokens:
            if hmac.compare_digest(supplied, f"Bearer {token}"):
                return True
        return False

    def do_HEAD(self) -> None:
        self.do_GET()

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/v1/health":
            self.json_response(HTTPStatus.OK, {"status": "ok", "version": SERVER_VERSION})
            return
        if not self.authorized():
            self.json_response(HTTPStatus.UNAUTHORIZED, {"error": "unauthorized"})
            return
        if parsed.path == "/v1/changes":
            since = parse_qs(parsed.query).get("since", [""])[0]
            self.json_response(HTTPStatus.OK, self.node.changes(since))
            return
        if parsed.path == "/v1/catalog":
            query = parse_qs(parsed.query)
            symbol = query.get("symbol", [""])[0]
            start = query.get("start", [""])[0].replace("-", "")
            end = query.get("end", [""])[0].replace("-", "")
            self.json_response(HTTPStatus.OK, {
                "schemaVersion": 1,
                "entries": self.node.catalog(symbol, start, end),
            })
            return
        match = re.fullmatch(r"/v1/ticks/([0-9]{6})/([0-9]{8})", parsed.path)
        if match:
            self.send_tick(match.group(1), match.group(2))
            return
        self.json_response(HTTPStatus.NOT_FOUND, {"error": "not_found"})

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        if not self.authorized():
            self.json_response(HTTPStatus.UNAUTHORIZED, {"error": "unauthorized"})
            return
        if parsed.path != "/v1/manifest":
            self.json_response(HTTPStatus.NOT_FOUND, {"error": "not_found"})
            return
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            content_length = 0
        if content_length <= 0 or content_length > MAX_MANIFEST_BODY_BYTES:
            self.json_response(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {
                "error": "manifest_body_limit",
            })
            return
        try:
            body = json.loads(self.rfile.read(content_length).decode("utf-8"))
            symbols = body.get("symbols", [])
            start = str(body.get("start", "")).replace("-", "")
            end = str(body.get("end", "")).replace("-", "")
            if not isinstance(symbols, list) or any(
                    not isinstance(symbol, str) for symbol in symbols):
                raise ValueError("invalid_symbols")
            entries = self.node.manifest(symbols, start, end)
        except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
            self.json_response(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
            return
        available = len({str(entry["symbol"]) for entry in entries})
        self.json_response(HTTPStatus.OK, {
            "schemaVersion": 1,
            "requestedSymbols": len(symbols),
            "availableSymbols": available,
            "missingSymbols": len(symbols) - available,
            "entries": entries,
        })

    def send_tick(self, symbol: str, trading_date: str) -> None:
        path = self.node.tick_path(symbol, trading_date)
        if path is None:
            self.json_response(HTTPStatus.NOT_FOUND, {"error": "tick_file_not_found"})
            return
        size = path.stat().st_size
        digest = sha256_file(path)
        start = 0
        range_header = self.headers.get("Range", "")
        if range_header:
            match = re.fullmatch(r"bytes=([0-9]+)-", range_header.strip())
            if not match or int(match.group(1)) >= size:
                self.send_response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
                self.send_header("Content-Range", f"bytes */{size}")
                self.end_headers()
                return
            start = int(match.group(1))
        status = HTTPStatus.PARTIAL_CONTENT if start else HTTPStatus.OK
        self.send_response(status)
        self.send_header("Content-Type", "text/csv")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("ETag", f'"sha256:{digest}"')
        self.send_header("X-Content-SHA256", digest)
        self.send_header("Content-Length", str(size - start))
        if start:
            self.send_header("Content-Range", f"bytes {start}-{size - 1}/{size}")
        self.end_headers()
        if self.command == "HEAD":
            return
        with path.open("rb") as stream:
            stream.seek(start)
            while block := stream.read(1024 * 1024):
                self.wfile.write(block)


def main() -> int:
    parser = argparse.ArgumentParser(description="PPTrader read-only TickData NAS node")
    parser.add_argument("--root", required=True)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--cert")
    parser.add_argument("--key")
    args = parser.parse_args()
    root = Path(args.root)
    if not root.is_dir():
        parser.error("--root must be an existing directory")
    token = os.environ.get("PPTRADER_NAS_TOKEN", "")
    token_file = os.environ.get("PPTRADER_NAS_TOKEN_FILE", "")
    if args.bind not in {"127.0.0.1", "::1", "localhost"} and not token and not token_file:
        parser.error("PPTRADER_NAS_TOKEN or PPTRADER_NAS_TOKEN_FILE is required for non-loopback binding")
    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    server.node = TickDataNode(root, token, token_file)  # type: ignore[attr-defined]
    if bool(args.cert) != bool(args.key):
        parser.error("--cert and --key must be supplied together")
    if args.cert:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(args.cert, args.key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
