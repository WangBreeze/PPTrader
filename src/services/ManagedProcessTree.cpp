#include "services/ManagedProcessTree.h"

#include <QProcess>
#include <QSet>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <tlhelp32.h>
#endif

namespace pp {

QList<qint64> ManagedProcessTree::descendantProcessIds(qint64 rootProcessId)
{
    QList<qint64> result;
#ifdef Q_OS_WIN
    if (rootProcessId <= 0) return result;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof(entry);
    QList<QPair<DWORD, DWORD>> processes;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            processes.append({entry.th32ProcessID, entry.th32ParentProcessID});
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    QSet<DWORD> parents {static_cast<DWORD>(rootProcessId)};
    bool added = true;
    while (added) {
        added = false;
        for (const auto &[processId, parentProcessId] : std::as_const(processes)) {
            if (processId == static_cast<DWORD>(rootProcessId)
                || parents.contains(processId) || !parents.contains(parentProcessId)) {
                continue;
            }
            parents.insert(processId);
            result.append(static_cast<qint64>(processId));
            added = true;
        }
    }
#else
    Q_UNUSED(rootProcessId)
#endif
    return result;
}

bool ManagedProcessTree::isProcessRunning(qint64 processId)
{
#ifdef Q_OS_WIN
    if (processId <= 0) return false;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                       FALSE, static_cast<DWORD>(processId));
    if (!process) return false;
    DWORD exitCode = 0;
    const bool running = GetExitCodeProcess(process, &exitCode)
        && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return running;
#else
    Q_UNUSED(processId)
    return false;
#endif
}

void ManagedProcessTree::stop(QProcess *process, int terminateTimeoutMilliseconds,
                              int killTimeoutMilliseconds)
{
    if (!process || process->state() == QProcess::NotRunning) return;
#ifdef Q_OS_WIN
    const QList<qint64> descendants = descendantProcessIds(process->processId());
#endif
    process->terminate();
    process->waitForFinished(terminateTimeoutMilliseconds);
#ifdef Q_OS_WIN
    for (auto iterator = descendants.crbegin(); iterator != descendants.crend(); ++iterator) {
        if (!isProcessRunning(*iterator)) continue;
        const HANDLE child = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE,
                                         FALSE, static_cast<DWORD>(*iterator));
        if (!child) continue;
        TerminateProcess(child, 1);
        WaitForSingleObject(child, static_cast<DWORD>(
            std::max(0, killTimeoutMilliseconds)));
        CloseHandle(child);
    }
#endif
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(killTimeoutMilliseconds);
    }
}

} // namespace pp
