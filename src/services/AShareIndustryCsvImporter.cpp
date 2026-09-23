#include "services/AShareIndustryCsvImporter.h"

#include "services/AShareUniverseLoader.h"
#include "services/SqliteLocalDataStore.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>

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

void fail(AShareIndustryImportResult *result, qint64 line,
          const QString &reason)
{
    result->succeeded = false;
    result->diagnostics.append({line, reason});
}

bool validIdentityToken(const QString &value)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9_.:-]{1,80}$"));
    return expression.match(value).hasMatch();
}

} // namespace

AShareIndustryImportResult AShareIndustryCsvImporter::parse(
    const AShareIndustryImportRequest &request,
    const CancellationCheck &isCancelled)
{
    AShareIndustryImportResult result;
    result.filePath = QFileInfo(request.filePath).absoluteFilePath();
    result.dataset.classification = request.classification.trimmed();
    result.dataset.source = request.source.trimmed();
    result.dataset.sourceVersion = request.sourceVersion.trimmed();
    if (result.filePath.isEmpty() || !QFileInfo::exists(result.filePath)) {
        fail(&result, 0, QStringLiteral("请选择存在的本地行业分类 CSV"));
        return result;
    }
    if (!validIdentityToken(result.dataset.classification)
        || !validIdentityToken(result.dataset.source)
        || !validIdentityToken(result.dataset.sourceVersion)) {
        fail(&result, 0, QStringLiteral(
            "分类体系、来源和来源版本只能使用 1–80 位字母、数字、点、冒号、下划线或连字符"));
        return result;
    }

    QFile file(result.filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(&result, 0, QStringLiteral("无法打开显式选择的行业分类 CSV"));
        return result;
    }
    QCryptographicHash fileHash(QCryptographicHash::Sha256);
    QSet<QString> identities;
    bool headerRead = false;
    while (!file.atEnd()) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            result.dataset.memberships.clear();
            return result;
        }
        constexpr qint64 kMaximumLineBytes = 1024 * 1024;
        const QByteArray rawLine = file.readLine(kMaximumLineBytes + 2);
        fileHash.addData(rawLine);
        ++result.linesRead;
        if (rawLine.size() > kMaximumLineBytes) {
            fail(&result, result.linesRead, QStringLiteral("单行超过 1 MiB 限制"));
            result.dataset.memberships.clear();
            return result;
        }
        QStringDecoder decoder(QStringDecoder::Utf8);
        QString line = decoder.decode(rawLine);
        if (decoder.hasError()) {
            fail(&result, result.linesRead, QStringLiteral("文件不是有效 UTF-8"));
            result.dataset.memberships.clear();
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
            result.dataset.memberships.clear();
            return result;
        }
        if (!headerRead) {
            const QStringList expected{
                QStringLiteral("symbol"), QStringLiteral("name"),
                QStringLiteral("venue"), QStringLiteral("industry_code"),
                QStringLiteral("industry_name"),
                QStringLiteral("effective_from"),
                QStringLiteral("effective_to")};
            QStringList normalized;
            for (const QString &field : fields)
                normalized.append(field.toLower());
            if (normalized != expected) {
                fail(&result, result.linesRead, QStringLiteral(
                    "表头必须为 symbol,name,venue,industry_code,industry_name,effective_from,effective_to"));
                return result;
            }
            headerRead = true;
            continue;
        }
        if (fields.size() != 7) {
            fail(&result, result.linesRead, QStringLiteral("每行必须包含 7 列"));
            result.dataset.memberships.clear();
            return result;
        }
        AShareIndustryMembership membership;
        membership.instrument.symbol = fields.at(0);
        membership.instrument.name = fields.at(1).isEmpty()
            ? fields.at(0) : fields.at(1);
        membership.instrument.market = Market::AShare;
        membership.instrument.venue = AShareUniverseLoader::venueForCode(
            fields.at(0), fields.at(2));
        membership.industryCode = fields.at(3);
        membership.industryName = fields.at(4);
        membership.effectiveFrom = QDate::fromString(fields.at(5), Qt::ISODate);
        if (!fields.at(6).isEmpty()) {
            membership.effectiveTo = QDate::fromString(
                fields.at(6), Qt::ISODate);
        }
        if ((membership.instrument.venue != QStringLiteral("上交所")
             && membership.instrument.venue != QStringLiteral("深交所"))
            || !membership.isValid()) {
            fail(&result, result.linesRead, QStringLiteral(
                "代码、venue、行业字段或有效期无效；当前只接受沪深 A 股"));
            result.dataset.memberships.clear();
            return result;
        }
        const QString identity = membership.instrument.symbol
            + QLatin1Char('|') + membership.effectiveFrom.toString(Qt::ISODate);
        if (identities.contains(identity)) {
            fail(&result, result.linesRead,
                 QStringLiteral("同一文件包含重复的代码+生效日期"));
            result.dataset.memberships.clear();
            return result;
        }
        identities.insert(identity);
        result.dataset.memberships.append(membership);
    }
    result.fileContentSha256 = QStringLiteral("sha256:%1")
        .arg(QString::fromLatin1(fileHash.result().toHex()));
    result.validRecords = result.dataset.memberships.size();
    result.dataset.fileName = QFileInfo(result.filePath).fileName();
    result.dataset.linesRead = result.linesRead;
    result.dataset.recordCount = result.validRecords;
    if (!headerRead || result.dataset.memberships.isEmpty()) {
        fail(&result, 0, QStringLiteral("CSV 没有可导入的行业成员"));
        result.dataset.memberships.clear();
        return result;
    }
    result.succeeded = true;
    return result;
}

AShareIndustryImportService::AShareIndustryImportService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<AShareIndustryImportResult>();
}

AShareIndustryImportService::~AShareIndustryImportService()
{
    if (!m_thread) return;
    disconnect(m_thread, nullptr, this, nullptr);
    m_thread->requestInterruption();
    m_thread->wait();
    delete m_thread;
}

void AShareIndustryImportService::setDatabasePath(const QString &databasePath)
{
    if (!m_thread) m_databasePath = databasePath;
}

quint64 AShareIndustryImportService::start(
    const AShareIndustryImportRequest &request)
{
    if (m_thread || m_databasePath.isEmpty()) return 0;
    const quint64 requestId = ++m_nextRequestId;
    const QString databasePath = m_databasePath;
    QPointer<AShareIndustryImportService> guard(this);
    QThread *thread = QThread::create([guard, request, requestId, databasePath]() {
        QElapsedTimer timer;
        timer.start();
        AShareIndustryImportResult result = AShareIndustryCsvImporter::parse(
            request, []() {
            QThread *thread = QThread::currentThread();
            return thread && thread->isInterruptionRequested();
        });
        if (result.succeeded && !result.cancelled) {
            SqliteLocalDataStore store;
            if (!store.initialize(databasePath)) {
                fail(&result, 0, QStringLiteral(
                    "无法打开 SQLite；已有行业数据集保持不变"));
            } else {
                const auto analysis = store.analyzeAShareIndustryDataset(
                    result.dataset);
                result.previewReady = analysis.valid;
                result.reused = analysis.reused;
                result.requiresRevisionReason =
                    analysis.requiresRevisionReason;
                result.dataset.previousDatasetId = analysis.previousDatasetId;
                if (!analysis.valid) fail(&result, 0, analysis.error);
            }
        }
        if (result.succeeded && !result.cancelled && !request.previewOnly) {
            if (request.expectedFileContentSha256.isEmpty()) {
                fail(&result, 0, QStringLiteral(
                    "确认导入缺少预览文件哈希；请先预览"));
            } else if (request.expectedFileContentSha256
                       != result.fileContentSha256) {
                fail(&result, 0, QStringLiteral(
                    "CSV 内容已在预览后改变；请重新预览再确认导入"));
            }
        }
        if (result.succeeded && !result.cancelled && !request.previewOnly) {
            result.dataset.revisionReason = request.revisionReason.trimmed();
            bool reused = false;
            QString saveError;
            SqliteLocalDataStore store;
            if (!store.initialize(databasePath)
                || !store.saveAShareIndustryDataset(
                    result.dataset, &reused, &saveError)) {
                fail(&result, 0, saveError.isEmpty()
                    ? QStringLiteral("SQLite 事务导入失败；已有数据集保持不变")
                    : saveError);
            } else {
                result.reused = reused;
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

quint64 AShareIndustryImportService::preview(
    const AShareIndustryImportRequest &request)
{
    AShareIndustryImportRequest previewRequest = request;
    previewRequest.previewOnly = true;
    previewRequest.revisionReason.clear();
    previewRequest.expectedFileContentSha256.clear();
    return start(previewRequest);
}

bool AShareIndustryImportService::running() const { return m_thread != nullptr; }

void AShareIndustryImportService::cancel()
{
    if (m_thread) m_thread->requestInterruption();
}

} // namespace pp
