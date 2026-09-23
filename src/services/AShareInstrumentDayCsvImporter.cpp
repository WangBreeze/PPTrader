#include "services/AShareInstrumentDayCsvImporter.h"

#include "services/AShareTradingCalendar.h"
#include "services/AShareUniverseLoader.h"
#include "services/SqliteLocalDataStore.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QStringConverter>
#include <QUuid>

namespace pp {

namespace {

bool parseCsvRow(const QString &line, QStringList *fields, QString *error)
{
    fields->clear();
    QString field;
    bool quoted = false;
    bool quoteClosed = false;
    for (int index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (quoted) {
            if (character == QLatin1Char('"')) {
                if (index + 1 < line.size()
                    && line.at(index + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++index;
                } else {
                    quoted = false;
                    quoteClosed = true;
                }
            } else {
                field += character;
            }
        } else if (quoteClosed) {
            if (character == QLatin1Char(',')) {
                fields->append(field.trimmed());
                field.clear();
                quoteClosed = false;
            } else if (!character.isSpace()) {
                if (error) *error = QStringLiteral("CSV 闭合引号后存在非法字符");
                return false;
            }
        } else if (character == QLatin1Char(',')) {
            fields->append(field.trimmed());
            field.clear();
        } else if (character == QLatin1Char('"') && field.trimmed().isEmpty()) {
            field.clear();
            quoted = true;
        } else if (character == QLatin1Char('"')) {
            if (error) *error = QStringLiteral("CSV 非引号字段中出现引号");
            return false;
        } else {
            field += character;
        }
    }
    if (quoted) {
        if (error) *error = QStringLiteral("CSV 引号未闭合");
        return false;
    }
    fields->append(field.trimmed());
    return true;
}

void fail(AShareInstrumentDayImportResult *result, qint64 line,
          const QString &reason)
{
    result->diagnostics.append({line, reason});
    result->succeeded = false;
}

AShareInstrumentDayState parseState(const QString &value)
{
    if (value.compare(QStringLiteral("suspended"), Qt::CaseInsensitive) == 0
        || value == QStringLiteral("停牌")) {
        return AShareInstrumentDayState::Suspended;
    }
    if (value.compare(QStringLiteral("tradable"), Qt::CaseInsensitive) == 0
        || value == QStringLiteral("可交易")) {
        return AShareInstrumentDayState::Tradable;
    }
    return AShareInstrumentDayState::Unknown;
}

} // namespace

AShareInstrumentDayImportResult AShareInstrumentDayCsvImporter::parse(
    const AShareInstrumentDayImportRequest &request,
    const CancellationCheck &isCancelled)
{
    AShareInstrumentDayImportResult result;
    result.filePath = QFileInfo(request.filePath).absoluteFilePath();
    result.source = request.source.trimmed().toUpper();
    result.sourceVersion = request.sourceVersion.trimmed();
    if (result.filePath.isEmpty() || !QFileInfo::exists(result.filePath)) {
        fail(&result, 0, QStringLiteral("请选择存在的本地 CSV 文件"));
        return result;
    }
    if (result.source != QStringLiteral("SSE")
        && result.source != QStringLiteral("SZSE")) {
        fail(&result, 0, QStringLiteral("来源只允许 SSE 或 SZSE"));
        return result;
    }
    if (result.sourceVersion.isEmpty()) {
        fail(&result, 0, QStringLiteral("必须填写官方公告号、导出日期或数据版本"));
        return result;
    }

    QFile file(result.filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(&result, 0, QStringLiteral("无法打开显式选择的 CSV 文件"));
        return result;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QSet<QString> identities;
    bool headerRead = false;
    while (!file.atEnd()) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            result.records.clear();
            return result;
        }
        constexpr qint64 kMaximumLineBytes = 1024 * 1024;
        QByteArray rawLine = file.readLine(kMaximumLineBytes + 2);
        hash.addData(rawLine);
        ++result.linesRead;
        if (rawLine.size() > kMaximumLineBytes) {
            fail(&result, result.linesRead, QStringLiteral("单行超过 1 MiB 限制"));
            result.records.clear();
            return result;
        }
        QStringDecoder decoder(QStringDecoder::Utf8);
        QString line = decoder.decode(rawLine);
        if (decoder.hasError()) {
            fail(&result, result.linesRead, QStringLiteral("文件不是有效 UTF-8"));
            result.records.clear();
            return result;
        }
        line = line.trimmed();
        if (result.linesRead == 1 && !line.isEmpty()
            && line.front() == QChar(0xfeff)) line.remove(0, 1);
        if (line.isEmpty()) continue;
        QStringList fields;
        QString parseError;
        if (!parseCsvRow(line, &fields, &parseError)) {
            fail(&result, result.linesRead, parseError);
            result.records.clear();
            return result;
        }
        if (!headerRead) {
            const QStringList expected{QStringLiteral("symbol"),
                QStringLiteral("name"), QStringLiteral("venue"),
                QStringLiteral("trading_date"), QStringLiteral("state"),
                QStringLiteral("reason")};
            QStringList normalized;
            for (const QString &field : fields) normalized.append(field.toLower());
            if (normalized != expected) {
                fail(&result, result.linesRead,
                     QStringLiteral("表头必须为 symbol,name,venue,trading_date,state,reason"));
                return result;
            }
            headerRead = true;
            continue;
        }
        if (fields.size() != 6) {
            fail(&result, result.linesRead, QStringLiteral("每行必须包含 6 列"));
            result.records.clear();
            return result;
        }
        AShareInstrumentDayRecord record;
        record.instrument.symbol = fields.at(0);
        record.instrument.name = fields.at(1).isEmpty() ? fields.at(0)
                                                        : fields.at(1);
        record.instrument.market = Market::AShare;
        record.instrument.venue = AShareUniverseLoader::venueForCode(
            fields.at(0), fields.at(2));
        const QString expectedVenue = AShareUniverseLoader::venueForCode(
            fields.at(0), result.source);
        record.tradingDate = QDate::fromString(fields.at(3), Qt::ISODate);
        record.state = parseState(fields.at(4));
        record.reason = fields.at(5);
        record.source = result.source;
        record.sourceVersion = result.sourceVersion;
        if (record.instrument.venue.isEmpty() || expectedVenue.isEmpty()
            || record.instrument.venue != expectedVenue) {
            fail(&result, result.linesRead,
                 QStringLiteral("代码、venue 与声明来源不一致"));
            result.records.clear();
            return result;
        }
        if (!AShareTradingCalendar::status(record.tradingDate).tradingDay) {
            fail(&result, result.linesRead,
                 QStringLiteral("标的日事实必须落在市场交易日"));
            result.records.clear();
            return result;
        }
        if (!record.isValid()) {
            fail(&result, result.linesRead,
                 QStringLiteral("代码、日期或状态无效；状态只允许 tradable/suspended"));
            result.records.clear();
            return result;
        }
        const QString identity = record.instrument.symbol + QLatin1Char('|')
            + record.tradingDate.toString(Qt::ISODate);
        if (identities.contains(identity)) {
            fail(&result, result.linesRead,
                 QStringLiteral("同一文件包含重复的代码+交易日"));
            result.records.clear();
            return result;
        }
        identities.insert(identity);
        result.records.append(record);
        if (!result.firstDate.isValid() || record.tradingDate < result.firstDate)
            result.firstDate = record.tradingDate;
        if (!result.lastDate.isValid() || record.tradingDate > result.lastDate)
            result.lastDate = record.tradingDate;
    }
    result.contentSha256 = QStringLiteral("sha256:")
        + QString::fromLatin1(hash.result().toHex());
    result.validRecords = result.records.size();
    if (!headerRead || result.records.isEmpty()) {
        fail(&result, 0, QStringLiteral("CSV 没有可导入的事实记录"));
        result.records.clear();
        return result;
    }
    result.succeeded = true;
    return result;
}

AShareInstrumentDayImportService::AShareInstrumentDayImportService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<AShareInstrumentDayImportResult>();
}

AShareInstrumentDayImportService::~AShareInstrumentDayImportService()
{
    if (m_thread) {
        disconnect(m_thread, nullptr, this, nullptr);
        m_thread->requestInterruption();
        m_thread->wait();
        delete m_thread;
    }
}

void AShareInstrumentDayImportService::setDatabasePath(const QString &databasePath)
{
    if (!m_thread) m_databasePath = databasePath;
}

quint64 AShareInstrumentDayImportService::start(
    const AShareInstrumentDayImportRequest &request)
{
    if (m_thread || m_databasePath.isEmpty()) return 0;
    const quint64 requestId = ++m_nextRequestId;
    const QString databasePath = m_databasePath;
    QPointer<AShareInstrumentDayImportService> guard(this);
    QThread *thread = QThread::create([guard, request, requestId, databasePath]() {
        QElapsedTimer timer;
        timer.start();
        AShareInstrumentDayImportResult result =
            AShareInstrumentDayCsvImporter::parse(request, []() {
                QThread *thread = QThread::currentThread();
                return thread && thread->isInterruptionRequested();
            });
        if (result.succeeded && !result.cancelled) {
            SqliteLocalDataStore store;
            if (!store.initialize(databasePath)) {
                result.succeeded = false;
                result.diagnostics.append(
                    {0, QStringLiteral("无法打开 SQLite；已有事实保持不变")});
            } else {
                const auto changes = store.analyzeAShareInstrumentDayImport(
                    result.records);
                result.newRecords = changes.newRecords;
                result.unchangedRecords = changes.unchangedRecords;
                result.correctedRecords = changes.correctedRecords;
                result.requiresRevisionReason = changes.correctedRecords > 0;
                result.previewReady = changes.valid;
                if (!changes.valid) {
                    result.succeeded = false;
                    result.diagnostics.append({0, changes.error});
                }
            }
        }
        if (result.succeeded && !result.cancelled && !request.previewOnly) {
            if (!request.expectedContentSha256.isEmpty()
                && request.expectedContentSha256 != result.contentSha256) {
                result.succeeded = false;
                result.diagnostics.append({0, QStringLiteral(
                    "CSV 内容已在预览后改变；请重新预览再确认导入")});
            }
        }
        if (result.succeeded && !result.cancelled && !request.previewOnly) {
            SqliteLocalDataStore store;
            AShareInstrumentDayImportRun run;
            run.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            run.importedAt = QDateTime::currentDateTimeUtc();
            run.fileName = QFileInfo(result.filePath).fileName();
            run.source = result.source;
            run.sourceVersion = result.sourceVersion;
            run.contentSha256 = result.contentSha256;
            run.linesRead = result.linesRead;
            run.recordCount = result.validRecords;
            run.firstDate = result.firstDate;
            run.lastDate = result.lastDate;
            run.newRecords = result.newRecords;
            run.unchangedRecords = result.unchangedRecords;
            run.correctedRecords = result.correctedRecords;
            run.revisionReason = request.revisionReason.trimmed();
            bool reused = false;
            QString importError;
            if (!store.initialize(databasePath)
                || !store.importAShareInstrumentDayRecords(
                    run, result.records, &reused, &importError)) {
                result.succeeded = false;
                result.diagnostics.append(
                    {0, importError.isEmpty()
                            ? QStringLiteral("SQLite 事务导入失败；已有事实保持不变")
                            : importError});
            } else {
                result.reused = reused;
                result.runId = run.runId;
                result.newRecords = run.newRecords;
                result.unchangedRecords = run.unchangedRecords;
                result.correctedRecords = run.correctedRecords;
                result.writtenRecords = reused ? 0 : result.validRecords;
            }
        }
        result.previewOnly = request.previewOnly;
        result.elapsedMilliseconds = timer.elapsed();
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

quint64 AShareInstrumentDayImportService::preview(
    const AShareInstrumentDayImportRequest &request)
{
    AShareInstrumentDayImportRequest previewRequest = request;
    previewRequest.previewOnly = true;
    previewRequest.revisionReason.clear();
    previewRequest.expectedContentSha256.clear();
    return start(previewRequest);
}

bool AShareInstrumentDayImportService::running() const
{
    return m_thread != nullptr;
}

void AShareInstrumentDayImportService::cancel()
{
    if (m_thread) m_thread->requestInterruption();
}

} // namespace pp
