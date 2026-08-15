#include "services/DesktopNotificationService.h"

#include <QSystemTrayIcon>

namespace pp {

DesktopNotificationService::DesktopNotificationService(QObject *parent)
    : NotificationService(parent)
    , m_tray(new QSystemTrayIcon(this))
{
}

DesktopNotificationService::~DesktopNotificationService() = default;

bool DesktopNotificationService::send(const NotificationMessage &message)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable() || !m_tray) {
        emit notificationFailed(channelName(),
                                QStringLiteral("系统托盘不可用，无法发送桌面通知"));
        return false;
    }
    const QSystemTrayIcon::MessageIcon icon =
        message.severity == NotificationSeverity::Critical
        ? QSystemTrayIcon::Critical
        : (message.severity == NotificationSeverity::Warning
               ? QSystemTrayIcon::Warning
               : QSystemTrayIcon::Information);
    m_tray->showMessage(message.title, message.body, icon, 5000);
    emit notificationSent(channelName(), message.title);
    return true;
}

} // namespace pp
