// QVariantList/QList values are exposed as array-like sequences in Qt 6.11.
// They are indexable but are not guaranteed to satisfy Array.isArray() inside
// a shared .pragma library script context.
function isSequence(value) {
    return Array.isArray(value)
        || (value && typeof value !== "string" && typeof value.length === "number")
}

function sequence(value) {
    return isSequence(value) ? value : []
}

function conditionGroup(conditions) {
    if (conditions.length === 1)
        return conditions[0]
    return { op: "and", conditions: conditions }
}

function indicatorSchema(schema, name) {
    const definitions = sequence(schema ? schema.indicatorSchemas : null)
    for (let index = 0; index < definitions.length; ++index) {
        if (definitions[index].name === name)
            return definitions[index]
    }
    return null
}

function newIndicatorRow(schema, name, alias, color) {
    const definition = indicatorSchema(schema, name)
    if (!definition)
        return {}
    let row = {
        name: name,
        alias: alias,
        period: 0,
        fastPeriod: 0,
        slowPeriod: 0,
        signalPeriod: 0,
        kPeriod: 0,
        dPeriod: 0,
        output: "",
        standardDeviations: 0,
        seriesVisible: true,
        seriesColor: color
    }
    const parameters = sequence(definition.parameters)
    for (let index = 0; index < parameters.length; ++index) {
        const parameter = parameters[index]
        row[parameter.key] = parameter.defaultValue
    }
    return row
}

function seriesDocument(schema, row) {
    const definition = indicatorSchema(schema, row.name)
    if (!definition)
        return {}
    let document = { alias: row.alias, indicator: row.name }
    const parameters = sequence(definition.parameters)
    for (let index = 0; index < parameters.length; ++index) {
        const parameter = parameters[index]
        document[parameter.key] = row[parameter.key]
    }
    return document
}

function validateIndicatorRow(schema, row) {
    const definition = indicatorSchema(schema, row.name)
    if (!definition)
        return { valid: false, key: "name", error: "指标不在当前白名单中" }
    if (!/^[a-z][A-Za-z0-9_]{0,31}$/.test(String(row.alias || ""))) {
        return { valid: false, key: "alias",
                 error: "指标别名必须以小写字母开头，且只含字母、数字或下划线" }
    }
    const parameters = sequence(definition.parameters)
    for (let index = 0; index < parameters.length; ++index) {
        const parameter = parameters[index]
        const value = row[parameter.key]
        if (parameter.type === "choice") {
            const choices = parameter.choices || []
            let matched = false
            for (let choiceIndex = 0; choiceIndex < choices.length; ++choiceIndex) {
                if (String(choices[choiceIndex]) === String(value)) {
                    matched = true
                    break
                }
            }
            if (!matched) {
                return { valid: false, key: parameter.key,
                         error: parameter.displayName + "不是允许的选项" }
            }
            continue
        }
        const number = Number(value)
        const integerValid = parameter.type !== "integer" || Number.isInteger(number)
        if (!Number.isFinite(number) || !integerValid
                || number < Number(parameter.minimumValue)
                || number > Number(parameter.maximumValue)) {
            return { valid: false, key: parameter.key,
                     error: parameter.displayName + "必须在 " + parameter.minimumValue
                            + "–" + parameter.maximumValue + " 之间" }
        }
    }
    const relations = sequence(definition.relations)
    for (let index = 0; index < relations.length; ++index) {
        const relation = relations[index]
        if (relation.op === "lt"
                && !(Number(row[relation.left]) < Number(row[relation.right]))) {
            return { valid: false, key: relation.left,
                     error: relation.message || "指标参数关系无效" }
        }
    }
    return { valid: true, key: "", error: "" }
}

function validateIndicatorRows(schema, rows) {
    if (!isSequence(rows) || rows.length === 0)
        return { valid: false, row: -1, key: "", error: "请至少添加一个指标" }
    let aliases = {}
    for (let index = 0; index < rows.length; ++index) {
        const result = validateIndicatorRow(schema, rows[index])
        if (!result.valid)
            return { valid: false, row: index, key: result.key, error: result.error }
        const alias = rows[index].alias
        if (aliases[alias]) {
            return { valid: false, row: index, key: "alias",
                     error: "指标别名重复：" + alias }
        }
        aliases[alias] = true
    }
    return { valid: true, row: -1, key: "", error: "" }
}

function migrateRows(rows, legacyLeft, currentLeft, legacyRight, currentRight) {
    if (!isSequence(rows))
        return []
    let migrated = []
    for (let index = 0; index < rows.length; ++index) {
        let row = Object.assign({}, rows[index])
        if (legacyLeft && row[legacyLeft] !== undefined && row[currentLeft] === undefined)
            row[currentLeft] = row[legacyLeft]
        if (legacyRight && row[legacyRight] !== undefined && row[currentRight] === undefined)
            row[currentRight] = row[legacyRight]
        if (legacyLeft)
            delete row[legacyLeft]
        if (legacyRight)
            delete row[legacyRight]
        migrated.push(row)
    }
    return migrated
}

function migrateIndicatorRows(rows, schema) {
    const renamed = migrateRows(rows, "visible", "seriesVisible", "", "")
    let migrated = []
    for (let index = 0; index < renamed.length; ++index) {
        const source = renamed[index]
        const defaults = newIndicatorRow(schema, source.name, source.alias,
                                         source.seriesColor || "")
        migrated.push(Object.assign(defaults, source))
    }
    return migrated
}

function operandKind(value, aliases, fallback) {
    const fields = ["open", "high", "low", "close", "volume"]
    if (aliases.indexOf(value) >= 0)
        return "series"
    if (fields.indexOf(value) >= 0)
        return "field"
    if (value === "" || value === undefined || value === null)
        return fallback
    return fallback
}

function migrateConditionRows(rows, aliases) {
    const renamed = migrateRows(rows, "left", "leftOperand", "right", "rightOperand")
    let migrated = []
    for (let index = 0; index < renamed.length; ++index) {
        let row = Object.assign({}, renamed[index])
        row.leftOperand = row.leftOperand || "close"
        row.rightOperand = row.rightOperand || ""
        row.leftKind = row.leftKind || operandKind(row.leftOperand, aliases, "field")
        row.rightKind = row.rightKind || operandKind(row.rightOperand, aliases, "value")
        row.op = row.op || "gt"
        if (row.rightConstant === undefined || row.rightConstant === null)
            row.rightConstant = 0
        migrated.push(row)
    }
    return migrated
}

function editorOperand(operand, fallbackKind) {
    const value = operand && typeof operand === "object" ? operand : {}
    if (value.field !== undefined)
        return { kind: "field", operand: String(value.field), constant: 0 }
    if (value.series !== undefined)
        return { kind: "series", operand: String(value.series), constant: 0 }
    if (value.value !== undefined)
        return { kind: "value", operand: "", constant: Number(value.value) }
    return { kind: fallbackKind, operand: fallbackKind === "field" ? "close" : "",
             constant: 0 }
}

function migrateConditionNode(input, aliases) {
    const source = input && typeof input === "object" ? input : {}
    if (source.op === "and" || source.op === "or") {
        const children = isSequence(source.conditions) ? source.conditions : []
        return {
            op: source.op,
            conditions: children.map(function (child) {
                return migrateConditionNode(child, aliases)
            })
        }
    }
    if (source.op === "not") {
        return {
            op: "not",
            condition: migrateConditionNode(source.condition, aliases)
        }
    }
    if (source.left && typeof source.left === "object"
            && source.right && typeof source.right === "object") {
        const left = editorOperand(source.left, "field")
        const right = editorOperand(source.right, "value")
        return {
            leftOperand: left.operand,
            leftKind: left.kind,
            op: source.op || "gt",
            rightOperand: right.operand,
            rightKind: right.kind,
            rightConstant: right.constant
        }
    }
    return migrateConditionRows([source], aliases)[0]
}

function migrateConditionTree(input, aliases) {
    const knownAliases = isSequence(aliases) ? aliases : []
    if (isSequence(input)) {
        return {
            op: "and",
            conditions: migrateConditionRows(input, knownAliases)
        }
    }
    return migrateConditionNode(input, knownAliases)
}

function conditionOperand(kind, operand, constant) {
    if (kind === "value")
        return { value: Number(constant) }
    if (kind === "field")
        return { field: operand }
    return { series: operand }
}

function conditionDocument(input) {
    const source = input && typeof input === "object" ? input : {}
    if (source.op === "and" || source.op === "or") {
        const children = isSequence(source.conditions)
            ? source.conditions.map(conditionDocument) : []
        if (children.length === 1)
            return children[0]
        return { op: source.op, conditions: children }
    }
    if (source.op === "not")
        return { op: "not", condition: conditionDocument(source.condition) }
    if (source.left && typeof source.left === "object"
            && source.right && typeof source.right === "object") {
        return {
            op: source.op || "gt",
            left: cloneConditionTree(source.left),
            right: cloneConditionTree(source.right)
        }
    }
    return {
        op: source.op || "gt",
        left: conditionOperand(source.leftKind, source.leftOperand, 0),
        right: conditionOperand(source.rightKind, source.rightOperand,
                                source.rightConstant)
    }
}

function defaultConditionComparison() {
    return {
        leftOperand: "close",
        leftKind: "field",
        op: "gt",
        rightOperand: "",
        rightKind: "value",
        rightConstant: 0
    }
}

function cloneConditionTree(tree) {
    return JSON.parse(JSON.stringify(tree))
}

function pathParts(path) {
    if (path === undefined || path === null || String(path) === "")
        return []
    return String(path).split("/").map(function (part) { return Number(part) })
}

function unwrappedConditionNode(node) {
    return node && node.op === "not" ? node.condition : node
}

function conditionNodeAtPath(tree, path) {
    let node = tree
    const parts = pathParts(path)
    for (let index = 0; index < parts.length; ++index) {
        const group = unwrappedConditionNode(node)
        if (!group || (group.op !== "and" && group.op !== "or")
                || !isSequence(group.conditions))
            return null
        node = group.conditions[parts[index]]
    }
    return node || null
}

function replaceConditionNode(tree, path, replacement) {
    const result = cloneConditionTree(tree)
    const parts = pathParts(path)
    if (parts.length === 0)
        return replacement
    const childIndex = parts.pop()
    const parentPath = parts.join("/")
    const parent = unwrappedConditionNode(conditionNodeAtPath(result, parentPath))
    if (!parent || !isSequence(parent.conditions)
            || childIndex < 0 || childIndex >= parent.conditions.length)
        return result
    parent.conditions[childIndex] = replacement
    return result
}

function appendCondition(tree, parentPath) {
    const result = cloneConditionTree(tree)
    const parent = unwrappedConditionNode(conditionNodeAtPath(result, parentPath))
    if (parent && (parent.op === "and" || parent.op === "or")
            && isSequence(parent.conditions))
        parent.conditions.push(defaultConditionComparison())
    return result
}

function appendConditionGroup(tree, parentPath, op) {
    const result = cloneConditionTree(tree)
    const parent = unwrappedConditionNode(conditionNodeAtPath(result, parentPath))
    if (parent && (parent.op === "and" || parent.op === "or")
            && isSequence(parent.conditions)) {
        parent.conditions.push({
            op: op === "or" ? "or" : "and",
            conditions: [defaultConditionComparison(), defaultConditionComparison()]
        })
    }
    return result
}

function updateConditionNode(tree, path, patch) {
    const source = conditionNodeAtPath(tree, path)
    if (!source)
        return cloneConditionTree(tree)
    const wasNegated = source.op === "not"
    let inner = cloneConditionTree(unwrappedConditionNode(source))
    const changes = patch && typeof patch === "object" ? patch : {}
    for (const key of Object.keys(changes)) {
        if (key !== "negated")
            inner[key] = changes[key]
    }
    const shouldNegate = changes.negated !== undefined
        ? Boolean(changes.negated) : wasNegated
    const replacement = shouldNegate ? { op: "not", condition: inner } : inner
    return replaceConditionNode(tree, path, replacement)
}

function removeConditionNode(tree, path) {
    const result = cloneConditionTree(tree)
    const parts = pathParts(path)
    if (parts.length === 0)
        return result
    const childIndex = parts.pop()
    const parent = unwrappedConditionNode(
        conditionNodeAtPath(result, parts.join("/")))
    if (parent && isSequence(parent.conditions)
            && childIndex >= 0 && childIndex < parent.conditions.length)
        parent.conditions.splice(childIndex, 1)
    return result
}

function conditionEditorRows(tree) {
    let rows = []
    function visit(node, path, depth) {
        const negated = node && node.op === "not"
        const inner = unwrappedConditionNode(node) || {}
        if (inner.op === "and" || inner.op === "or") {
            const children = isSequence(inner.conditions) ? inner.conditions : []
            rows.push({
                path: path,
                nodeType: "group",
                depth: depth,
                negated: negated,
                groupOp: inner.op,
                childCount: children.length,
                // Qt 6.4 的 ListModel delegate 会对每一行校验全部 required
                // role；组行也必须提供比较行角色，即使对应控件不可见。
                leftOperand: "",
                leftKind: "field",
                op: "gt",
                rightOperand: "",
                rightKind: "value",
                rightConstant: 0
            })
            for (let index = 0; index < children.length; ++index) {
                visit(children[index], path === "" ? String(index)
                                                    : path + "/" + index,
                      depth + 1)
            }
            return
        }
        rows.push({
            path: path,
            nodeType: "comparison",
            depth: depth,
            negated: negated,
            groupOp: "",
            childCount: 0,
            leftOperand: inner.leftOperand || "close",
            leftKind: inner.leftKind || "field",
            op: inner.op || "gt",
            rightOperand: inner.rightOperand || "",
            rightKind: inner.rightKind || "value",
            rightConstant: Number(inner.rightConstant || 0)
        })
    }
    visit(tree, "", 0)
    return rows
}

function removeConditionSeries(tree, alias) {
    function prune(node) {
        if (!node || typeof node !== "object")
            return null
        if (node.op === "not") {
            const child = prune(node.condition)
            return child ? { op: "not", condition: child } : null
        }
        if (node.op === "and" || node.op === "or") {
            const children = isSequence(node.conditions) ? node.conditions : []
            let kept = []
            for (let index = 0; index < children.length; ++index) {
                const child = prune(children[index])
                if (child)
                    kept.push(child)
            }
            return { op: node.op, conditions: kept }
        }
        if ((node.leftKind === "series" && node.leftOperand === alias)
                || (node.rightKind === "series" && node.rightOperand === alias))
            return null
        return cloneConditionTree(node)
    }
    return prune(tree) || { op: "and", conditions: [] }
}

function conditionLeafCount(tree) {
    let count = 0
    const rows = conditionEditorRows(tree)
    for (let index = 0; index < rows.length; ++index) {
        if (rows[index].nodeType === "comparison")
            ++count
    }
    return count
}

function migrateDraft(input, schema) {
    const source = input && typeof input === "object" ? input : {}
    let result = Object.assign({}, source)
    result.strategyId = source.strategyId || "custom_wizard_draft"
    result.name = source.name || "多指标趋势策略"
    result.description = source.description !== undefined
                         ? source.description : "由策略创建向导生成，仅用于本地历史回测"
    result.author = source.author || "本地用户"
    result.activeStep = Number.isInteger(source.activeStep)
                        && source.activeStep >= 1 && source.activeStep <= 8
                        ? source.activeStep : 1
    result.series = migrateIndicatorRows(source.series, schema)
    const aliases = result.series.map(function (row) { return row.alias || "" })
    result.entry = migrateConditionRows(source.entry, aliases)
    result.exit = migrateConditionRows(source.exit, aliases)
    result.entryCondition = migrateConditionTree(
        source.entryCondition !== undefined ? source.entryCondition : source.entry,
        aliases)
    result.exitCondition = migrateConditionTree(
        source.exitCondition !== undefined ? source.exitCondition : source.exit,
        aliases)
    return result
}

function isCalendarDate(text) {
    if (text === "")
        return true
    const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(text)
    if (!match)
        return false
    const year = Number(match[1])
    const month = Number(match[2])
    const day = Number(match[3])
    const date = new Date(Date.UTC(year, month - 1, day))
    return date.getUTCFullYear() === year
        && date.getUTCMonth() === month - 1
        && date.getUTCDate() === day
}

function sampleWindowRequest(fromText, toText) {
    const from = String(fromText || "").trim()
    const to = String(toText || "").trim()
    if (!isCalendarDate(from) || !isCalendarDate(to)) {
        return { valid: false, from: from, to: to,
                 error: "样本窗日期必须使用 yyyy-MM-dd 且为有效日期" }
    }
    if (from !== "" && to !== "" && from > to) {
        return { valid: false, from: from, to: to,
                 error: "样本窗起始日期不能晚于结束日期" }
    }
    return { valid: true, from: from, to: to, error: "" }
}

function buildDefinition(metadata, series, entry, exit, rules, timestamp) {
    return {
        schemaVersion: 2,
        strategyId: metadata.strategyId,
        version: Number.isInteger(metadata.version) && metadata.version > 0
                 ? metadata.version : 1,
        name: metadata.name,
        description: metadata.description,
        author: metadata.author,
        market: "A股",
        executionScope: "backtest_only",
        createdAt: timestamp,
        updatedAt: timestamp,
        parameters: [],
        series: series,
        position: rules.position,
        risk: rules.risk,
        signal: rules.signal,
        entry: isSequence(entry) ? conditionGroup(entry) : conditionDocument(entry),
        exit: isSequence(exit) ? conditionGroup(exit) : conditionDocument(exit)
    }
}
