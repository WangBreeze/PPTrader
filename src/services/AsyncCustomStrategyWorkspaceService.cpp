#include "services/AsyncCustomStrategyWorkspaceService.h"

#include "services/CustomStrategyPreviewService.h"
#include "services/SqliteLocalDataStore.h"
#include "strategy/CustomStrategyValidator.h"

#include <QMetaObject>
#include <QPointer>

namespace pp {

namespace {

QVariantMap previewMap(
    const CustomStrategyPreviewService::PreviewResult &preview,
    const QString &symbol)
{
    QVariantMap result{{QStringLiteral("symbol"), symbol},
                       {QStringLiteral("source"), QStringLiteral("raw_tick_cache_v1")},
                       {QStringLiteral("barCount"), preview.bars.size()}};
    QVariantList aliases;
    QVariantList labels;
    for (int index = 0; index < preview.seriesAliases.size(); ++index) {
        aliases.append(preview.seriesAliases.at(index));
        labels.append(preview.seriesLabels.at(index));
    }
    result.insert(QStringLiteral("aliases"), aliases);
    result.insert(QStringLiteral("labels"), labels);

    QVariantList bars;
    for (const auto &bar : preview.bars) {
        QVariantList values;
        for (const auto &value : bar.seriesValues)
            values.append(value ? QVariant(*value) : QVariant());
        bars.append(QVariantMap{{QStringLiteral("t"), bar.time.toString(Qt::ISODateWithMs)},
                               {QStringLiteral("o"), bar.open},
                               {QStringLiteral("h"), bar.high},
                               {QStringLiteral("l"), bar.low},
                               {QStringLiteral("c"), bar.close},
                               {QStringLiteral("v"), bar.volume},
                               {QStringLiteral("s"), values}});
    }
    result.insert(QStringLiteral("bars"), bars);

    QVariantList signalRows;
    for (const auto &signal : preview.signalEvents) {
        signalRows.append(QVariantMap{{QStringLiteral("index"), signal.barIndex},
                                      {QStringLiteral("side"), signal.side},
                                      {QStringLiteral("price"), signal.price},
                                      {QStringLiteral("reason"), signal.reason}});
    }
    result.insert(QStringLiteral("signals"), signalRows);
    return result;
}

} // namespace

quint64 AsyncOperationRequestGate::begin()
{
    ++m_currentRequestId;
    if (m_currentRequestId == 0) ++m_currentRequestId;
    m_loading = true;
    return m_currentRequestId;
}

bool AsyncOperationRequestGate::finish(quint64 requestId)
{
    if (requestId == 0 || requestId != m_currentRequestId) return false;
    m_resolvedRequestId = requestId;
    m_loading = false;
    return true;
}

void AsyncOperationRequestGate::cancel()
{
    ++m_currentRequestId;
    if (m_currentRequestId == 0) ++m_currentRequestId;
    m_loading = false;
}

quint64 AsyncOperationRequestGate::currentRequestId() const { return m_currentRequestId; }
quint64 AsyncOperationRequestGate::resolvedRequestId() const { return m_resolvedRequestId; }
bool AsyncOperationRequestGate::loading() const { return m_loading; }

AsyncCustomStrategyWorkspaceService::AsyncCustomStrategyWorkspaceService(QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderCustomStrategyWorkspace"));
    m_thread.start();
}

AsyncCustomStrategyWorkspaceService::~AsyncCustomStrategyWorkspaceService()
{
    stop();
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool AsyncCustomStrategyWorkspaceService::start(const QString &databasePath)
{
    if (m_running || databasePath.isEmpty() || !m_worker) return m_running;
    bool initialized = false;
    QMetaObject::invokeMethod(m_worker, [this, databasePath, &initialized] {
        m_workerStore = std::make_unique<SqliteLocalDataStore>();
        initialized = m_workerStore->initialize(databasePath);
        if (!initialized) m_workerStore.reset();
    }, Qt::BlockingQueuedConnection);
    m_running = initialized;
    return initialized;
}

void AsyncCustomStrategyWorkspaceService::stop()
{
    if (!m_thread.isRunning()) return;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this] { m_workerStore.reset(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_running = false;
}

bool AsyncCustomStrategyWorkspaceService::isRunning() const { return m_running; }

bool AsyncCustomStrategyWorkspaceService::requestPreview(
    quint64 requestId, const QString &json, const Instrument &instrument,
    int periodSeconds, int barCount)
{
    if (!m_running || !m_worker || requestId == 0) return false;
    QPointer<AsyncCustomStrategyWorkspaceService> guard(this);
    return QMetaObject::invokeMethod(m_worker,
        [this, guard, requestId, json, instrument, periodSeconds, barCount] {
            QVariantMap result;
            CustomStrategyDefinition definition;
            QString error;
            CustomStrategyValidator validator;
            if (!validator.parseAndValidate(json, &definition, &error)) {
                result.insert(QStringLiteral("error"), error);
            } else if (!m_workerStore) {
                result.insert(QStringLiteral("error"),
                              QStringLiteral("自定义策略预览数据库不可用"));
            } else {
                const auto preview = CustomStrategyPreviewService::buildLocalTickPreview(
                    definition, {}, *m_workerStore, instrument,
                    periodSeconds, barCount, &error);
                result = error.isEmpty() ? previewMap(preview, instrument.symbol)
                                         : QVariantMap{{QStringLiteral("error"), error}};
            }
            if (!guard) return;
            QMetaObject::invokeMethod(guard, [guard, requestId, result] {
                if (guard) emit guard->previewReady(requestId, result);
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

bool AsyncCustomStrategyWorkspaceService::requestSave(
    quint64 requestId, const QString &json)
{
    if (!m_running || !m_worker || requestId == 0) return false;
    QPointer<AsyncCustomStrategyWorkspaceService> guard(this);
    return QMetaObject::invokeMethod(m_worker, [this, guard, requestId, json] {
        QVariantMap result;
        CustomStrategyDefinition definition;
        QString error;
        CustomStrategyValidator validator;
        if (!validator.parseAndValidate(json, &definition, &error)) {
            result.insert(QStringLiteral("error"), error);
        } else if (!m_workerStore) {
            result.insert(QStringLiteral("error"),
                          QStringLiteral("本地数据库不可用，无法保存自定义策略"));
        } else if (!m_workerStore->insertCustomStrategy(definition)) {
            result.insert(QStringLiteral("error"),
                          QStringLiteral("保存失败：同版本内容冲突或数据库写入错误"));
        } else {
            result = {{QStringLiteral("success"), true},
                      {QStringLiteral("strategyId"), definition.strategyId},
                      {QStringLiteral("version"), definition.version},
                      {QStringLiteral("name"), definition.name}};
        }
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, requestId, result] {
            if (guard) emit guard->saveFinished(requestId, result);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

} // namespace pp
