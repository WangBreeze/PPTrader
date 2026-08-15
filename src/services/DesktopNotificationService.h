#pragma once

#include "services/NotificationService.h"

class QSystemTrayIcon;

namespace pp {

// 桌面气泡通知：通过系统托盘 QSystemTrayIcon::showMessage 投递。
class DesktopNotificationService final : public NotificationService {
    Q_OBJECT

public:
    explicit DesktopNotificationService(QObject *parent = nullptr);
    ~DesktopNotificationService() override;

    QString channelName() const override { return QStringLiteral("desktop"); }
    bool send(const NotificationMessage &message) override;

private:
    QSystemTrayIcon *m_tray = nullptr;
};

} // namespace pp
