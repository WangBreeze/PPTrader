function sequence(value) {
    return value && typeof value.length === "number" ? value : []
}

function periodOptions(catalog) {
    const source = sequence(catalog)
    const byPeriod = {}
    for (let index = 0; index < source.length; ++index) {
        const item = source[index]
        if (!item || !item.backtestEligible || item.source !== "raw_tick_cache_v1"
                || !item.datasetVersion || Number(item.datasetStaleDayCount) > 0)
            continue
        const period = Number(item.periodSeconds)
        if (!(period > 0))
            continue
        if (!byPeriod[period]) {
            byPeriod[period] = {
                periodSeconds: period,
                frequency: String(item.frequency || period + "s"),
                count: 0
            }
        }
        byPeriod[period].count += 1
    }
    const result = []
    for (const key in byPeriod)
        result.push(byPeriod[key])
    result.sort((left, right) => left.periodSeconds - right.periodSeconds)
    for (let index = 0; index < result.length; ++index)
        result[index].label = result[index].frequency + " · " + result[index].count + " 只"
    return result
}

function eligibleDatasets(catalog, periodSeconds, query) {
    const source = sequence(catalog)
    const normalizedQuery = String(query || "").trim().toLowerCase()
    const result = []
    for (let index = 0; index < source.length; ++index) {
        const item = source[index]
        if (!item || Number(item.periodSeconds) !== Number(periodSeconds)
                || !item.backtestEligible || item.source !== "raw_tick_cache_v1"
                || !item.datasetVersion || Number(item.datasetStaleDayCount) > 0)
            continue
        const haystack = (String(item.symbol || "") + " " + String(item.name || "")).toLowerCase()
        if (normalizedQuery.length > 0 && haystack.indexOf(normalizedQuery) < 0)
            continue
        result.push(item)
    }
    result.sort((left, right) => String(left.symbol).localeCompare(String(right.symbol)))
    return result
}

function normalizeSymbols(symbols) {
    const source = sequence(symbols)
    const seen = {}
    const result = []
    for (let index = 0; index < source.length; ++index) {
        const symbol = String(source[index] || "").trim()
        if (/^[0-9]{6}$/.test(symbol) && !seen[symbol]) {
            seen[symbol] = true
            result.push(symbol)
        }
    }
    return result
}

function indicatorDocument(row) {
    const name = String(row.indicatorName || "").toUpperCase()
    const item = { alias: String(row.alias || ""), indicator: name }
    if (name === "PEER") {
        item.symbol = String(row.peerSymbol || "")
        item.field = String(row.sourceField || "close")
        return item
    }
    if (String(row.sourceKind || "field") === "ROC") {
        item.of = { indicator: "ROC", period: Number(row.periodValue) }
    } else {
        item.of = { field: String(row.sourceField || "close") }
    }
    if (name === "RANK")
        item.ascending = !!row.ascendingValue
    return item
}

function indicatorAliases(indicators) {
    const rows = sequence(indicators)
    const result = []
    for (let index = 0; index < rows.length; ++index)
        result.push(String(rows[index].alias || ""))
    return result
}

function validateDraft(catalog, draft, allowedCrossIndicators, allowedFields) {
    const symbols = normalizeSymbols(draft.symbols)
    if (!(Number(draft.periodSeconds) > 0))
        return { valid: false, error: "请选择本地历史数据周期" }
    if (symbols.length < 2)
        return { valid: false, error: "请至少选择 2 个同周期本地标的" }
    const barCount = Number(draft.barCount)
    if (!Number.isInteger(barCount) || barCount < 2 || barCount > 5000)
        return { valid: false, error: "回测根数必须是 2–5000 的整数" }

    const datasets = eligibleDatasets(catalog, draft.periodSeconds, "")
    const bySymbol = {}
    for (let index = 0; index < datasets.length; ++index)
        bySymbol[datasets[index].symbol] = datasets[index]
    for (let index = 0; index < symbols.length; ++index) {
        const dataset = bySymbol[symbols[index]]
        if (!dataset)
            return { valid: false, error: "本地目录缺少同周期 Tick 数据：" + symbols[index] }
        if (Number(dataset.candleCount) < barCount)
            return { valid: false, error: "K 线不足：" + symbols[index] + "（"
                    + dataset.candleCount + "/" + barCount + "）" }
    }

    const rows = sequence(draft.indicators)
    if (rows.length < 1)
        return { valid: false, error: "请至少添加一个横截面指标" }
    const allowedNames = sequence(allowedCrossIndicators)
    const fields = sequence(allowedFields)
    const aliases = {}
    for (let index = 0; index < rows.length; ++index) {
        const row = rows[index]
        const alias = String(row.alias || "")
        const name = String(row.indicatorName || "").toUpperCase()
        if (!/^[a-z][A-Za-z0-9_]{0,31}$/.test(alias))
            return { valid: false, error: "横截面指标别名格式无效：" + alias }
        if (aliases[alias])
            return { valid: false, error: "横截面指标别名重复：" + alias }
        aliases[alias] = true
        if (allowedNames.indexOf(name) < 0)
            return { valid: false, error: "横截面指标不在 schema 白名单中：" + name }
        if (fields.indexOf(String(row.sourceField || "")) < 0)
            return { valid: false, error: "横截面指标字段无效：" + row.sourceField }
        if (name === "PEER" && !/^[0-9]{6}$/.test(String(row.peerSymbol || "")))
            return { valid: false, error: "PEER 指标需要 6 位对照代码" }
        if (name !== "PEER" && String(row.sourceKind || "field") === "ROC") {
            const period = Number(row.periodValue)
            if (!Number.isInteger(period) || period < 2 || period > 5000)
                return { valid: false, error: "ROC 周期必须是 2–5000 的整数" }
        }
    }

    const rankBy = String(draft.rankBy || "")
    if (!aliases[rankBy])
        return { valid: false, error: "请选择 selection 使用的横截面指标" }
    const topN = Number(draft.topN)
    if (!Number.isInteger(topN) || topN < 1 || topN > symbols.length)
        return { valid: false, error: "Top-N 必须在 1–股票池数量之间" }
    if (!aliases[String(draft.entryAlias || "")] || !aliases[String(draft.exitAlias || "")])
        return { valid: false, error: "入场/出场条件必须引用已定义的横截面指标" }
    if (!Number.isFinite(Number(draft.entryValue)) || !Number.isFinite(Number(draft.exitValue)))
        return { valid: false, error: "入场/出场阈值必须是有限数字" }
    return { valid: true, error: "", symbolCount: symbols.length,
             periodSeconds: Number(draft.periodSeconds), barCount: barCount }
}

function buildDefinition(draft, createdAt) {
    const rows = sequence(draft.indicators)
    const indicators = []
    for (let index = 0; index < rows.length; ++index)
        indicators.push(indicatorDocument(rows[index]))
    return {
        schemaVersion: 3,
        strategyId: String(draft.strategyId || "custom_cross_draft"),
        version: Math.max(1, Number(draft.version) || 1),
        name: String(draft.name || "横截面轮动策略"),
        description: String(draft.description || "本地多股横截面回测"),
        author: String(draft.author || "本地用户"),
        market: "A股",
        executionScope: "backtest_only",
        createdAt: createdAt,
        parameters: [],
        universe: { mode: "explicit", symbols: normalizeSymbols(draft.symbols) },
        crossIndicators: indicators,
        selection: { mode: "topN", rankBy: String(draft.rankBy),
                     count: Number(draft.topN), ascending: !!draft.selectionAscending },
        position: { type: "equalWeight" },
        entry: { op: String(draft.entryOp || "gte"),
                 left: { cross: String(draft.entryAlias) },
                 right: { value: Number(draft.entryValue) } },
        exit: { op: String(draft.exitOp || "lt"),
                left: { cross: String(draft.exitAlias) },
                right: { value: Number(draft.exitValue) } }
    }
}
