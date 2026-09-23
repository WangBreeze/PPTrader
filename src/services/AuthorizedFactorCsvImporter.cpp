#include "services/AuthorizedFactorCsvImporter.h"
#include "services/SqliteLocalDataStore.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QMetaObject>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QStringConverter>

#include <cmath>

namespace pp {

namespace {

void fail(AuthorizedFactorImportResult *result, qint64 line,
          const QString &reason)
{
    result->succeeded = false;
    result->diagnostics.append({line, reason});
    result->observations.clear();
}

bool isAShareSymbol(const QString &symbol)
{
    static const QRegularExpression expression(QStringLiteral(
        "^(?:60[0135689]\\d{3}|68[089]\\d{3}|00[0123]\\d{3}|30[01]\\d{3})$"));
    return expression.match(symbol).hasMatch();
}

bool hasExplicitTimezone(const QString &value)
{
    static const QRegularExpression expression(
        QStringLiteral("(?:[zZ]|[+-]\\d{2}:\\d{2})$"));
    return expression.match(value).hasMatch();
}

} // namespace

AuthorizedFactorImportResult AuthorizedFactorCsvImporter::parse(
    const AuthorizedFactorImportRequest &request,
    const CancellationCheck &isCancelled)
{
    AuthorizedFactorImportResult result;
    result.previewOnly = request.previewOnly;
    result.filePath = QFileInfo(request.filePath).absoluteFilePath();
    if (!QFileInfo::exists(result.filePath)) {
        fail(&result, 0, QStringLiteral("请选择存在的本地授权因子 CSV"));
        return result;
    }
    if (request.factorKey.trimmed().isEmpty()
        || !request.factorDefinitionSha256.startsWith(QStringLiteral("sha256:"))
        || request.source != QStringLiteral("authorized_csv")
        || request.sourceVersion.trimmed().isEmpty()
        || request.sourceReference.trimmed().isEmpty()) {
        fail(&result, 0, QStringLiteral("授权因子身份、来源版本或来源引用不完整"));
        return result;
    }

    QFile file(result.filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(&result, 0, QStringLiteral("无法打开显式选择的授权因子 CSV"));
        return result;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QHash<QString, double> valuesByIdentity;
    QSet<QString> symbols;
    QSet<QString> timestamps;
    bool headerRead = false;
    while (!file.atEnd()) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            result.observations.clear();
            return result;
        }
        constexpr qint64 kMaximumLineBytes = 1024 * 1024;
        const QByteArray rawLine = file.readLine(kMaximumLineBytes + 2);
        hash.addData(rawLine);
        ++result.linesRead;
        if (rawLine.size() > kMaximumLineBytes) {
            fail(&result, result.linesRead, QStringLiteral("单行超过 1 MiB 限制"));
            return result;
        }
        QStringDecoder decoder(QStringDecoder::Utf8);
        QString line = decoder.decode(rawLine);
        if (decoder.hasError()) {
            fail(&result, result.linesRead, QStringLiteral("文件不是有效 UTF-8"));
            return result;
        }
        line = line.trimmed();
        if (result.linesRead == 1 && !line.isEmpty()
            && line.front() == QChar(0xfeff)) line.remove(0, 1);
        if (line.isEmpty()) continue;
        const QStringList fields = line.split(QLatin1Char(','));
        if (!headerRead) {
            QStringList normalized;
            for (const QString &field : fields)
                normalized.append(field.trimmed().toLower());
            if (normalized != QStringList{QStringLiteral("timestamp"),
                                         QStringLiteral("symbol"),
                                         QStringLiteral("value")}) {
                fail(&result, result.linesRead, QStringLiteral(
                    "表头必须为 timestamp,symbol,value"));
                return result;
            }
            headerRead = true;
            continue;
        }
        if (fields.size() != 3) {
            fail(&result, result.linesRead, QStringLiteral("每行必须包含 3 列"));
            return result;
        }
        const QString timestampText = fields.at(0).trimmed();
        ExternalFactorObservation observation;
        observation.timestamp = QDateTime::fromString(timestampText, Qt::ISODate);
        observation.symbol = fields.at(1).trimmed();
        bool valueOk = false;
        observation.value = fields.at(2).trimmed().toDouble(&valueOk);
        if (!hasExplicitTimezone(timestampText) || !observation.timestamp.isValid()) {
            fail(&result, result.linesRead, QStringLiteral(
                "timestamp 必须是带 Z 或时区偏移的 ISO 8601 时间"));
            return result;
        }
        if (!isAShareSymbol(observation.symbol)) {
            fail(&result, result.linesRead, QStringLiteral(
                "symbol 当前只接受沪深六位 A 股代码"));
            return result;
        }
        if (!valueOk || !std::isfinite(observation.value)) {
            fail(&result, result.linesRead, QStringLiteral("value 必须是有限数值"));
            return result;
        }
        observation.timestamp = observation.timestamp.toUTC();
        const QString identity = observation.timestamp.toString(Qt::ISODateWithMs)
            + QLatin1Char('|') + observation.symbol;
        const auto existing = valuesByIdentity.constFind(identity);
        if (existing != valuesByIdentity.constEnd()) {
            if (*existing != observation.value) {
                fail(&result, result.linesRead, QStringLiteral(
                    "同一 timestamp+symbol 存在冲突数值"));
                return result;
            }
            ++result.duplicateRecords;
            continue;
        }
        valuesByIdentity.insert(identity, observation.value);
        symbols.insert(observation.symbol);
        timestamps.insert(observation.timestamp.toString(Qt::ISODateWithMs));
        result.observations.append(observation);
    }
    result.contentSha256 = QStringLiteral("sha256:")
        + QString::fromLatin1(hash.result().toHex());
    result.validRecords = result.observations.size();
    if (!headerRead || result.observations.isEmpty()) {
        fail(&result, 0, QStringLiteral("CSV 没有可导入的因子观测"));
        return result;
    }
    result.dataset.factorKey = request.factorKey.trimmed();
    result.dataset.factorDefinitionSha256 = request.factorDefinitionSha256;
    result.dataset.source = request.source;
    result.dataset.sourceVersion = request.sourceVersion.trimmed();
    result.dataset.sourceReference = request.sourceReference.trimmed();
    result.dataset.contentSha256 = result.contentSha256;
    result.dataset.recordCount = result.validRecords;
    result.dataset.symbolCount = symbols.size();
    result.dataset.timestampCount = timestamps.size();
    result.dataset.firstTimestamp = result.observations.first().timestamp;
    result.dataset.lastTimestamp = result.observations.first().timestamp;
    for (const ExternalFactorObservation &observation : result.observations) {
        if (observation.timestamp < result.dataset.firstTimestamp)
            result.dataset.firstTimestamp = observation.timestamp;
        if (observation.timestamp > result.dataset.lastTimestamp)
            result.dataset.lastTimestamp = observation.timestamp;
    }
    result.succeeded = true;
    return result;
}

AuthorizedFactorImportService::AuthorizedFactorImportService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<AuthorizedFactorImportResult>();
}

AuthorizedFactorImportService::~AuthorizedFactorImportService()
{
    if (!m_thread) return;
    disconnect(m_thread, nullptr, this, nullptr);
    m_thread->requestInterruption();
    m_thread->wait();
    delete m_thread;
}

void AuthorizedFactorImportService::setDatabasePath(
    const QString &databasePath)
{
    if (!m_thread) m_databasePath = databasePath;
}

quint64 AuthorizedFactorImportService::start(
    const AuthorizedFactorImportRequest &request)
{
    if (m_thread || m_databasePath.isEmpty()) return 0;
    const quint64 requestId = ++m_nextRequestId;
    const QString databasePath = m_databasePath;
    QPointer<AuthorizedFactorImportService> guard(this);
    QThread *thread = QThread::create(
        [guard, request, requestId, databasePath]() {
        QElapsedTimer timer;
        timer.start();
        AuthorizedFactorImportResult result = AuthorizedFactorCsvImporter::parse(
            request, []() {
            QThread *thread = QThread::currentThread();
            return thread && thread->isInterruptionRequested();
        });
        if (result.succeeded && !result.cancelled) {
            SqliteLocalDataStore store;
            if (!store.initialize(databasePath)) {
                fail(&result, 0, QStringLiteral(
                    "无法打开 SQLite；已有授权因子数据集保持不变"));
            } else if (request.previewOnly) {
                result.previewReady = true;
                for (const ExternalFactorDataset &item :
                     store.loadExternalFactorDatasets(10000)) {
                    if (item.factorKey == result.dataset.factorKey
                        && item.factorDefinitionSha256
                            == result.dataset.factorDefinitionSha256
                        && item.source == result.dataset.source
                        && item.sourceVersion == result.dataset.sourceVersion
                        && item.contentSha256 == result.dataset.contentSha256) {
                        result.reused = true;
                        result.dataset = item;
                        break;
                    }
                }
            } else if (request.expectedContentSha256.isEmpty()) {
                fail(&result, 0, QStringLiteral(
                    "确认导入缺少预览文件哈希；请先预览"));
            } else if (request.expectedContentSha256 != result.contentSha256) {
                fail(&result, 0, QStringLiteral(
                    "CSV 内容已在预览后改变；请重新预览再确认导入"));
            } else {
                result.dataset.importedAt = QDateTime::currentDateTimeUtc();
                QString saveError;
                if (!store.saveExternalFactorDataset(
                        result.dataset, result.observations, &result.reused,
                        &saveError)) {
                    fail(&result, 0, saveError.isEmpty()
                        ? QStringLiteral("SQLite 事务导入失败；已有数据集保持不变")
                        : saveError);
                } else {
                    result.writtenRecords = result.reused
                        ? 0 : result.validRecords;
                }
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

quint64 AuthorizedFactorImportService::preview(
    const AuthorizedFactorImportRequest &request)
{
    AuthorizedFactorImportRequest previewRequest = request;
    previewRequest.previewOnly = true;
    previewRequest.expectedContentSha256.clear();
    return start(previewRequest);
}

bool AuthorizedFactorImportService::running() const
{
    return m_thread != nullptr;
}

void AuthorizedFactorImportService::cancel()
{
    if (m_thread) m_thread->requestInterruption();
}

} // namespace pp
