#pragma once

#include "services/PaperStrategyRun.h"
#include "domain/CustomStrategyTypes.h"
#include "domain/RealtimeMinuteSeriesTypes.h"

namespace pp {

// 固定策略版本与品种的模拟实验簿。行情由宿主投递，图表选择不参与身份。
class PaperStrategyBook final : public QObject {
    Q_OBJECT
public:
    enum class InputMode { Live, HistoricalReplay };
    explicit PaperStrategyBook(QObject *parent = nullptr, InputMode mode = InputMode::Live);
    ~PaperStrategyBook() override;
    void bindAccount(TradingAccountService &account, SimulatedBrokerGateway &broker);
    bool hasRun() const;
    QString createRun(const CustomStrategyDefinition &definition, const QVector<Instrument> &instruments,
                      const PaperStrategyConfig &config, const QDateTime &startedAt,
                      QString *error = nullptr);
    QString createRun(const CustomStrategyDefinition &definition, const Instrument &instrument,
                      const PaperStrategyConfig &config, const QDateTime &startedAt,
                      QString *error = nullptr);
    quint64 beginMinuteRequest(const QString &symbol);
    bool acceptMinutes(quint64 requestId, const RealtimeMinuteSeriesResult &result);
    void acceptQuote(const Tick &quote, const QDateTime &receivedAt);
    bool acceptReplayBar(const Candle &bar, const QDateTime &sourceClock, bool warmup);
    void acceptReplayQuote(const Tick &quote);
    QStringList subscribedSymbols() const;
    QVariantList snapshots() const;
    bool stopRun(const QString &runId);
    void stopAll();
signals:
    void changed();
private:
    bool validateReplayClock(const QDateTime &sourceClock);
    struct State;
    std::unique_ptr<State> m_state;
    const InputMode m_mode;
};

} // namespace pp
