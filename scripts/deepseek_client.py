#!/usr/bin/env python3
"""PPTrader 的 DeepSeek OpenAI 兼容接口客户端。

密钥只从 DEEPSEEK_API_KEY 读取，不写入代码、数据库或日志。
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request


def _required_env(name: str) -> str:
    value = os.environ.get(name, "").strip()
    if not value:
        raise RuntimeError(f"未配置环境变量 {name}")
    return value


def chat(prompt: str, *, system: str | None = None) -> str:
    api_key = _required_env("DEEPSEEK_API_KEY")
    base_url = os.environ.get("DEEPSEEK_BASE_URL", "https://api.deepseek.com").rstrip("/")
    model = os.environ.get("DEEPSEEK_MODEL", "deepseek-v4-flash")
    timeout = float(os.environ.get("DEEPSEEK_TIMEOUT_SECONDS", "60"))

    messages = []
    if system:
        messages.append({"role": "system", "content": system})
    messages.append({"role": "user", "content": prompt})
    payload = json.dumps({"model": model, "messages": messages, "stream": False}).encode("utf-8")
    request = urllib.request.Request(
        f"{base_url}/chat/completions",
        data=payload,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            result = json.load(response)
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"DeepSeek 请求失败（HTTP {error.code}）: {detail}") from error
    except urllib.error.URLError as error:
        raise RuntimeError(f"无法连接 DeepSeek: {error.reason}") from error

    try:
        return result["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError) as error:
        raise RuntimeError("DeepSeek 返回了无法识别的响应") from error


def main() -> int:
    parser = argparse.ArgumentParser(description="调用 DeepSeek 进行研究辅助问答")
    parser.add_argument("prompt", help="发送给 DeepSeek 的问题")
    parser.add_argument("--system", help="可选的系统提示词")
    args = parser.parse_args()
    try:
        print(chat(args.prompt, system=args.system))
    except RuntimeError as error:
        print(f"错误: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
