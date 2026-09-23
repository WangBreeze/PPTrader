#pragma once

#include <QList>

class QProcess;

namespace pp {

class ManagedProcessTree final {
public:
    static QList<qint64> descendantProcessIds(qint64 rootProcessId);
    static bool isProcessRunning(qint64 processId);
    static void stop(QProcess *process, int terminateTimeoutMilliseconds = 3000,
                     int killTimeoutMilliseconds = 3000);
};

} // namespace pp
