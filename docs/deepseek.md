# DeepSeek 配置

PPTrader 使用 DeepSeek 的 OpenAI 兼容接口作为研究辅助能力。模型只用于生成候选研究内容，不能直接下单、修改历史结果或绕过人工审核。

## 配置

复制 `.env.example` 中的配置到当前终端环境，并替换 API 密钥：

```powershell
$env:PPTRADER_LLM_PROVIDER = "deepseek"
$env:DEEPSEEK_API_KEY = "sk-..."
$env:DEEPSEEK_BASE_URL = "https://api.deepseek.com"
$env:DEEPSEEK_MODEL = "deepseek-v4-flash"
```

不要把真实密钥写入仓库、日志或 `data/`。`DEEPSEEK_BASE_URL` 和 `DEEPSEEK_MODEL` 可按需要覆盖。

## 验证

```powershell
python scripts/deepseek_client.py "请用三句话说明 SMA 与 ROC 的区别"
```

如果尚未配置密钥，客户端会明确报错，不会发送请求。
