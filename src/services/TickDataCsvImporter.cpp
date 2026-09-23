#include "services/TickDataCsvImporter.h"

#include "services/HistoricalTickSessionRules.h"
#include "services/SqliteLocalDataStore.h"

#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace pp {

namespace {

constexpr int MaximumDiagnostics = 100;

void appendDiagnostic(TickDataImportResult *result, qint64 lineNumber,
                      const QString &reason, bool fatal = false)
{
    if (fatal) result->qualityStatus = QStringLiteral("error");
    if (result->diagnostics.size() < MaximumDiagnostics) {
        result->diagnostics.append({result->filePath, lineNumber, reason, fatal});
    }
}

bool finiteNonNegative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

bool parseNumber(const QByteArray &text, double *value)
{
    bool ok = false;
    const double parsed = QString::fromUtf8(text.trimmed()).toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

QTime parseClock(const QByteArray &text)
{
    const QString value = QString::fromUtf8(text.trimmed());
    QTime time = QTime::fromString(value, QStringLiteral("HH:mm:ss"));
    if (!time.isValid()) time = QTime::fromString(value, QStringLiteral("HH:mm"));
    return time;
}

bool isWeekday(const QDate &date)
{
    return date.dayOfWeek() >= Qt::Monday && date.dayOfWeek() <= Qt::Friday;
}

QDateTime shanghaiToUtc(const QDate &date, const QTime &time)
{
    return QDateTime(date, time, QTimeZone("Asia/Shanghai")).toUTC();
}

void finalizeQuality(TickDataImportResult *result, int periodSeconds)
{
    result->expectedContinuousBars =
        HistoricalTickSessionRules::expectedContinuousBars(periodSeconds);
    int continuousBars = 0;
    for (const Candle &candle : result->candles) {
        const QTime localTime = candle.startTime.toTimeZone(QTimeZone("Asia/Shanghai")).time();
        if (localTime != QTime(9, 25)) ++continuousBars;
    }
    result->missingContinuousBars = std::max(
        0, result->expectedContinuousBars - continuousBars);
    if (result->skippedRecords > 0) {
        result->qualityMessages.append(
            QStringLiteral("跳过 %1 条无效或盘外记录").arg(result->skippedRecords));
    }
    if (result->outOfOrderRecords > 0) {
        result->qualityMessages.append(
            QStringLiteral("发现 %1 条时间倒序记录").arg(result->outOfOrderRecords));
    }
    if (result->missingContinuousBars > 0) {
        result->qualityMessages.append(
            QStringLiteral("连续竞价 Bar 比标准会话少 %1 根")
                .arg(result->missingContinuousBars));
    }
    if (result->succeeded) {
        result->qualityStatus = result->qualityMessages.isEmpty()
            ? QStringLiteral("healthy") : QStringLiteral("warning");
    } else {
        result->qualityStatus = QStringLiteral("error");
    }
}

struct CandleAccumulator {
    Candle candle;
    bool initialized = false;

    void append(const TickDataTradeRecord &trade)
    {
        if (!initialized) {
            candle.open = candle.high = candle.low = candle.close = trade.price;
            initialized = true;
        } else {
            candle.high = std::max(candle.high, trade.price);
            candle.low = std::min(candle.low, trade.price);
            candle.close = trade.price;
        }
        candle.volume += trade.volumeShares;
        switch (trade.direction) {
        case RawTradeDirection::Buy: candle.buyVolume += trade.volumeShares; break;
        case RawTradeDirection::Sell: candle.sellVolume += trade.volumeShares; break;
        case RawTradeDirection::Neutral: candle.neutralVolume += trade.volumeShares; break;
        }
    }
};

bool validateRequest(const TickDataImportRequest &request, TickDataImportResult *result)
{
    const QString root = QDir::fromNativeSeparators(request.rootDirectory.trimmed());
    const bool networkPath = root.startsWith(QStringLiteral("//"));
    static const QRegularExpression symbolPattern(QStringLiteral("^[0-9]{6}$"));
    if (root.isEmpty() || !QDir::isAbsolutePath(root) || networkPath) {
        appendDiagnostic(result, 0,
                         QStringLiteral("根目录必须是显式选择的本地或已挂载绝对路径"), true);
    } else if (!request.tradingDate.isValid() || !isWeekday(request.tradingDate)) {
        appendDiagnostic(result, 0, QStringLiteral("日期无效或不是工作日"), true);
    } else if (!symbolPattern.match(request.instrument.symbol).hasMatch()
               || request.instrument.market != Market::AShare) {
        appendDiagnostic(result, 0, QStringLiteral("仅允许六位 A 股代码"), true);
    } else if (request.format == TickDataCsvFormat::Tick
               && request.periodSeconds != 60 && request.periodSeconds != 300) {
        appendDiagnostic(result, 0, QStringLiteral("逐笔聚合周期仅支持 60 或 300 秒"), true);
    } else if (request.format == TickDataCsvFormat::FiveMinute
               && request.periodSeconds != 300) {
        appendDiagnostic(result, 0, QStringLiteral("5min 文件只能导入 300 秒 K 线"), true);
    } else if (request.format == TickDataCsvFormat::FiveMinute
               && request.fiveMinuteVolumeUnit == FiveMinuteVolumeUnit::Unknown) {
        appendDiagnostic(result, 1,
                         QStringLiteral("5min volume 单位未知；必须显式选择“股”或“手”"), true);
    }
    return result->diagnostics.isEmpty();
}

TickDataImportResult parseTickFile(QFile *file, const TickDataImportRequest &request,
                                   const TickDataCsvImporter::CancellationCheck &isCancelled,
                                   const TickDataCsvImporter::RawTradeSink &rawTradeSink,
                                   TickDataImportResult result)
{
    QHash<QTime, CandleAccumulator> accumulators;
    bool chineseHeaderLayout = false;
    bool layoutDetected = false;
    QVector<RawTradeTick> rawBatch;
    rawBatch.reserve(2048);
    const auto flushRawBatch = [&]() {
        if (rawBatch.isEmpty()) return true;
        if (rawTradeSink && !rawTradeSink(rawBatch)) return false;
        rawBatch.clear();
        return true;
    };
    std::optional<QTime> previousAcceptedTime;
    while (!file->atEnd()) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            appendDiagnostic(&result, result.linesRead, QStringLiteral("用户取消导入"), true);
            return result;
        }
        const QByteArray line = file->readLine();
        ++result.linesRead;
        result.maximumLineBytes = std::max(result.maximumLineBytes,
                                           static_cast<qint64>(line.size()));
        QByteArray trimmed = line.trimmed();
        if (result.linesRead == 1 && trimmed.startsWith("\xEF\xBB\xBF")) {
            trimmed.remove(0, 3);
        }
        if (trimmed.isEmpty()) {
            ++result.skippedRecords;
            appendDiagnostic(&result, result.linesRead, QStringLiteral("空行"));
            continue;
        }
        const QList<QByteArray> fields = trimmed.split(',');
        if (!layoutDetected) {
            layoutDetected = true;
            if (fields.size() == 5
                && fields.at(0).trimmed().compare("time", Qt::CaseInsensitive) == 0
                && fields.at(1).trimmed().compare("price", Qt::CaseInsensitive) == 0
                && fields.at(2).trimmed().compare("volume", Qt::CaseInsensitive) == 0
                && fields.at(3).trimmed().compare("amount", Qt::CaseInsensitive) == 0
                && fields.at(4).trimmed().compare("type", Qt::CaseInsensitive) == 0) {
                continue;
            }
            if (fields.size() == 6
                && QString::fromUtf8(fields.at(0).trimmed()) == QStringLiteral("成交时间")
                && QString::fromUtf8(fields.at(1).trimmed()) == QStringLiteral("成交价格")
                && QString::fromUtf8(fields.at(3).trimmed()) == QStringLiteral("成交量")
                && QString::fromUtf8(fields.at(4).trimmed()) == QStringLiteral("成交金额")
                && QString::fromUtf8(fields.at(5).trimmed()) == QStringLiteral("性质")) {
                chineseHeaderLayout = true;
                continue;
            }
        }
        const int expectedFieldCount = chineseHeaderLayout ? 6 : 5;
        if (fields.size() != expectedFieldCount) {
            ++result.skippedRecords;
            appendDiagnostic(&result, result.linesRead,
                             QStringLiteral("逐笔记录列数与已识别格式不一致"));
            continue;
        }
        const int volumeField = chineseHeaderLayout ? 3 : 2;
        const int amountField = chineseHeaderLayout ? 4 : 3;
        const int sideField = chineseHeaderLayout ? 5 : 4;
        const QTime time = parseClock(fields.at(0));
        double price = 0.0;
        double volumeLots = 0.0;
        double amount = 0.0;
        if (!time.isValid() || !parseNumber(fields.at(1), &price) || price <= 0.0
            || !parseNumber(fields.at(volumeField), &volumeLots)
            || !finiteNonNegative(volumeLots)
            || !parseNumber(fields.at(amountField), &amount)
            || !finiteNonNegative(amount)) {
            ++result.skippedRecords;
            appendDiagnostic(&result, result.linesRead,
                             QStringLiteral("时间、价格、成交手数或成交额无效"));
            continue;
        }
        RawTradeDirection direction;
        const QString side = QString::fromUtf8(fields.at(sideField).trimmed()).toUpper();
        if (side == QStringLiteral("B") || side == QStringLiteral("买")
            || side == QStringLiteral("买盘")) {
            direction = RawTradeDirection::Buy;
        } else if (side == QStringLiteral("S") || side == QStringLiteral("卖")
                   || side == QStringLiteral("卖盘")) {
            direction = RawTradeDirection::Sell;
        } else if (side == QStringLiteral("M") || side == QStringLiteral("中")
                   || side == QStringLiteral("中性") || side == QStringLiteral("中性盘")) {
            direction = RawTradeDirection::Neutral;
        }
        else {
            ++result.skippedRecords;
            appendDiagnostic(&result, result.linesRead,
                             QStringLiteral("原始成交方向必须是 B/S/M 或买盘/卖盘/中性盘"));
            continue;
        }
        const std::optional<QTime> bucket =
            HistoricalTickSessionRules::bucketStart(time, request.periodSeconds);
        if (!bucket.has_value()) {
            ++result.skippedRecords;
            appendDiagnostic(&result, result.linesRead,
                             QStringLiteral("记录位于 A 股有效时段之外"));
            continue;
        }
        if (previousAcceptedTime.has_value() && time < *previousAcceptedTime) {
            ++result.outOfOrderRecords;
            appendDiagnostic(&result, result.linesRead,
                             QStringLiteral("逐笔时间倒序；仍按交易时段和时间桶聚合"));
        }
        previousAcceptedTime = time;

        TickDataTradeRecord trade;
        trade.timestamp = shanghaiToUtc(request.tradingDate, time);
        trade.price = price;
        trade.volumeShares = volumeLots * 100.0;
        trade.amount = amount;
        trade.direction = direction;
        RawTradeTick rawTick;
        rawTick.instrument = request.instrument;
        rawTick.timestamp = trade.timestamp;
        rawTick.tradingDate = request.tradingDate;
        rawTick.sourceLine = result.linesRead;
        rawTick.price = trade.price;
        rawTick.volumeShares = trade.volumeShares;
        rawTick.amount = trade.amount;
        rawTick.direction = trade.direction;
        rawBatch.append(rawTick);
        if (rawBatch.size() >= 2048 && !flushRawBatch()) {
            appendDiagnostic(&result, result.linesRead,
                             QStringLiteral("原始逐笔数据事务写入失败"), true);
            return result;
        }
        CandleAccumulator &accumulator = accumulators[*bucket];
        accumulator.candle.instrument = request.instrument;
        accumulator.candle.startTime = shanghaiToUtc(request.tradingDate, *bucket);
        accumulator.candle.periodSeconds = request.periodSeconds;
        accumulator.candle.directionalVolumeKnown = true;
        accumulator.candle.source = request.periodSeconds == 60
            ? QStringLiteral("tickdata_csv_tick_1m")
            : QStringLiteral("tickdata_csv_tick_5m");
        accumulator.candle.adjustment = QStringLiteral("none");
        accumulator.candle.downloadedAt = QDateTime::currentDateTimeUtc();
        accumulator.append(trade);
        ++result.validRecords;
    }

    if (!flushRawBatch()) {
        appendDiagnostic(&result, result.linesRead,
                         QStringLiteral("原始逐笔数据事务写入失败"), true);
        return result;
    }

    QList<QTime> keys = accumulators.keys();
    std::sort(keys.begin(), keys.end());
    for (const QTime &key : keys) result.candles.append(accumulators.value(key).candle);
    result.source = request.periodSeconds == 60
        ? QStringLiteral("tickdata_csv_tick_1m") : QStringLiteral("tickdata_csv_tick_5m");
    result.succeeded = !result.candles.isEmpty();
    if (!result.succeeded) appendDiagnostic(&result, 0, QStringLiteral("没有可写入的有效逐笔 K 线"), true);
    finalizeQuality(&result, request.periodSeconds);
    return result;
}

TickDataImportResult parseFiveMinuteFile(
    QFile *file, const TickDataImportRequest &request,
    const TickDataCsvImporter::CancellationCheck &isCancelled,
    TickDataImportResult result)
{
    QByteArray header = file->readLine().trimmed();
    ++result.linesRead;
    result.maximumLineBytes = header.size();
    if (header.startsWith("\xEF\xBB\xBF")) header.remove(0, 3);
    if (header != "time,open,close,high,low,volume") {
        appendDiagnostic(&result, 1,
                         QStringLiteral("5min 文件表头必须是 time,open,close,high,low,volume"), true);
        return result;
    }

    QHash<QDateTime, bool> timestamps;
    std::optional<QDateTime> previousAcceptedTimestamp;
    while (!file->atEnd()) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            appendDiagnostic(&result, result.linesRead, QStringLiteral("用户取消导入"), true);
            return result;
        }
        const QByteArray line = file->readLine();
        ++result.linesRead;
        result.maximumLineBytes = std::max(result.maximumLineBytes,
                                           static_cast<qint64>(line.size()));
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            ++result.skippedRecords;
            appendDiagnostic(&result, result.linesRead, QStringLiteral("空行"));
            continue;
        }
        const QList<QByteArray> fields = trimmed.split(',');
        if (fields.size() != 6) {
            ++result.skippedRecords;
            appendDiagnostic(&result, result.linesRead, QStringLiteral("5min 记录必须恰好包含 6 列"));
            continue;
        }
        const QTime label = parseClock(fields.at(0));
        double open = 0.0, close = 0.0, high = 0.0, low = 0.0, volume = 0.0;
        if (!label.isValid() || !HistoricalTickSessionRules::validFiveMinuteLabel(label)
            || !parseNumber(fields.at(1), &open) || !parseNumber(fields.at(2), &close)
            || !parseNumber(fields.at(3), &high) || !parseNumber(fields.at(4), &low)
            || !parseNumber(fields.at(5), &volume)
            || open <= 0.0 || close <= 0.0 || high <= 0.0 || low <= 0.0
            || !finiteNonNegative(volume) || high < std::max(open, close)
            || low > std::min(open, close) || high < low) {
            ++result.skippedRecords;
            appendDiagnostic(&result, result.linesRead,
                             QStringLiteral("5min 时间网格或 OHLCV 数据无效"));
            continue;
        }
        const std::optional<QTime> bucket =
            HistoricalTickSessionRules::bucketStart(label, 300);
        if (!bucket.has_value()) {
            ++result.skippedRecords;
            appendDiagnostic(&result, result.linesRead,
                             QStringLiteral("5min 记录位于 A 股有效时段之外"));
            continue;
        }
        const QDateTime timestamp = shanghaiToUtc(request.tradingDate, *bucket);
        if (timestamps.contains(timestamp)) {
            ++result.skippedRecords;
            appendDiagnostic(&result, result.linesRead,
                             QStringLiteral("映射后的 5min K 线时间重复"));
            continue;
        }
        if (previousAcceptedTimestamp.has_value()
            && timestamp < *previousAcceptedTimestamp) {
            ++result.outOfOrderRecords;
            appendDiagnostic(&result, result.linesRead,
                             QStringLiteral("5min 时间倒序；写入前将按时间排序"));
        }
        previousAcceptedTimestamp = timestamp;
        timestamps.insert(timestamp, true);

        Candle candle;
        candle.instrument = request.instrument;
        candle.startTime = timestamp;
        candle.periodSeconds = 300;
        candle.open = open;
        candle.close = close;
        candle.high = high;
        candle.low = low;
        candle.volume = request.fiveMinuteVolumeUnit == FiveMinuteVolumeUnit::Lots
            ? volume * 100.0 : volume;
        candle.directionalVolumeKnown = false;
        candle.source = request.fiveMinuteVolumeUnit == FiveMinuteVolumeUnit::Lots
            ? QStringLiteral("tickdata_csv_5min_lots_to_shares")
            : QStringLiteral("tickdata_csv_5min_shares");
        candle.adjustment = QStringLiteral("none");
        candle.downloadedAt = QDateTime::currentDateTimeUtc();
        result.candles.append(candle);
        ++result.validRecords;
    }
    std::sort(result.candles.begin(), result.candles.end(), [](const Candle &left, const Candle &right) {
        return left.startTime < right.startTime;
    });
    result.source = request.fiveMinuteVolumeUnit == FiveMinuteVolumeUnit::Lots
        ? QStringLiteral("tickdata_csv_5min_lots_to_shares")
        : QStringLiteral("tickdata_csv_5min_shares");
    result.succeeded = !result.candles.isEmpty();
    if (!result.succeeded) appendDiagnostic(&result, 0, QStringLiteral("没有可写入的有效 5min K 线"), true);
    finalizeQuality(&result, 300);
    return result;
}

} // namespace

QString TickDataCsvImporter::expectedFilePath(const TickDataImportRequest &request)
{
    const QString folder = request.tradingDate.toString(QStringLiteral("yyyyMMdd"))
        + (request.format == TickDataCsvFormat::FiveMinute ? QStringLiteral("+5min") : QString{});
    return QDir(request.rootDirectory).filePath(
        folder + QLatin1Char('/') + request.instrument.symbol + QStringLiteral(".csv"));
}

TickDataImportResult TickDataCsvImporter::parse(
    const TickDataImportRequest &request, const CancellationCheck &isCancelled,
    const RawTradeSink &rawTradeSink)
{
    TickDataImportResult result;
    result.filePath = expectedFilePath(request);
    if (!validateRequest(request, &result)) return result;

    QFile file(result.filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        appendDiagnostic(&result, 0, QStringLiteral("无法打开显式请求的 CSV 文件"), true);
        return result;
    }
    if (request.format == TickDataCsvFormat::Tick) {
        return parseTickFile(&file, request, isCancelled, rawTradeSink, std::move(result));
    }
    return parseFiveMinuteFile(&file, request, isCancelled, std::move(result));
}

TickDataCsvImportService::TickDataCsvImportService(QObject *parent)
    : QObject(parent)
{
}

TickDataCsvImportService::~TickDataCsvImportService()
{
    if (m_thread) {
        disconnect(m_thread, nullptr, this, nullptr);
        m_thread->requestInterruption();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

quint64 TickDataCsvImportService::start(const TickDataImportRequest &request)
{
    if (m_thread) return 0;
    const quint64 requestId = ++m_nextRequestId;
    const QString databasePath = m_databasePath;
    QPointer<TickDataCsvImportService> guard(this);
    QThread *thread = QThread::create([guard, request, requestId, databasePath]() {
        std::unique_ptr<SqliteLocalDataStore> store;
        bool rawTransaction = false;
        qint64 rawDatabaseNanoseconds = 0;
        qint64 rawSinkNanoseconds = 0;
        if (request.format == TickDataCsvFormat::Tick && !databasePath.isEmpty()) {
            QElapsedTimer databaseTimer;
            databaseTimer.start();
            store = std::make_unique<SqliteLocalDataStore>();
            rawTransaction = store->initialize(databasePath)
                && store->beginRawTradeTickImport(
                    request.instrument, request.tradingDate,
                    QStringLiteral("tickdata_csv_tick"));
            rawDatabaseNanoseconds += databaseTimer.nsecsElapsed();
        }
        const auto rawSink = [&store, rawTransaction, &rawDatabaseNanoseconds,
                              &rawSinkNanoseconds](
                                 const QVector<RawTradeTick> &ticks) {
            QElapsedTimer databaseTimer;
            databaseTimer.start();
            const bool succeeded = rawTransaction && store && store->appendRawTradeTicks(ticks);
            const qint64 elapsed = databaseTimer.nsecsElapsed();
            rawDatabaseNanoseconds += elapsed;
            rawSinkNanoseconds += elapsed;
            return succeeded;
        };
        QElapsedTimer parseTimer;
        parseTimer.start();
        TickDataImportResult result = TickDataCsvImporter::parse(request, []() {
            QThread *thread = QThread::currentThread();
            return thread && thread->isInterruptionRequested();
        }, request.format == TickDataCsvFormat::Tick
               ? TickDataCsvImporter::RawTradeSink(rawSink)
               : TickDataCsvImporter::RawTradeSink{});
        const qint64 parseAndSinkNanoseconds = parseTimer.nsecsElapsed();
        if (request.format == TickDataCsvFormat::Tick) {
            if (!rawTransaction) {
                result.succeeded = false;
                result.diagnostics.append({result.filePath, 0,
                    QStringLiteral("原始逐笔数据库事务不可用"), true});
            } else if (result.succeeded && !result.cancelled
                       && store) {
                QElapsedTimer databaseTimer;
                databaseTimer.start();
                const bool committed = store->commitRawTradeTickImport(result);
                rawDatabaseNanoseconds += databaseTimer.nsecsElapsed();
                if (committed) {
                    result.rawTicksWritten = result.validRecords;
                } else {
                    store->rollbackRawTradeTickImport();
                    result.succeeded = false;
                    result.diagnostics.append({result.filePath, 0,
                        QStringLiteral("原始逐笔导入已回滚"), true});
                }
            } else {
                store->rollbackRawTradeTickImport();
                result.succeeded = false;
                if (!result.cancelled) {
                    result.diagnostics.append({result.filePath, 0,
                        QStringLiteral("原始逐笔导入已回滚"), true});
                }
            }
        }
        result.rawDatabaseElapsedMilliseconds = qRound64(
            static_cast<double>(rawDatabaseNanoseconds) / 1000000.0);
        result.parseElapsedMilliseconds = qRound64(
            static_cast<double>(qMax<qint64>(
                0, parseAndSinkNanoseconds - rawSinkNanoseconds)) / 1000000.0);
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, requestId, result]() {
            if (guard) emit guard->finished(requestId, result);
        }, Qt::QueuedConnection);
    });
    m_thread = thread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_thread == thread) m_thread = nullptr;
        thread->deleteLater();
    });
    thread->start();
    return requestId;
}

void TickDataCsvImportService::setDatabasePath(const QString &databasePath)
{
    if (!m_thread) m_databasePath = databasePath;
}

bool TickDataCsvImportService::running() const
{
    return m_thread != nullptr;
}

void TickDataCsvImportService::cancel()
{
    if (m_thread) m_thread->requestInterruption();
}

} // namespace pp
