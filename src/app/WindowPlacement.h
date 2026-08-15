#pragma once

#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace pp {

// 屏幕信息（抽象自 QScreen，便于无 GUI 单测）。
struct ScreenInfo {
    QString name;
    QRect availableGeometry; // 可用区域（不含任务栏/停靠栏）。
};

// 持久化的窗口放置数据：上次关闭时所在屏幕 + 窗口几何。
struct WindowPlacementData {
    QString screenName;
    QRect geometry;
    bool hasGeometry = false;
};

// 窗口放置持久化：QSettings 读写 + 多屏恢复几何计算。
class WindowPlacement {
public:
    static WindowPlacementData load();
    static void save(const QString &screenName, const QRect &geometry);

    // 计算恢复几何：优先恢复到保存的屏幕；屏幕缺失或保存几何几乎不可见时，
    // 回退到主屏幕（列表首项）居中，尺寸夹紧到可用区域。
    static QRect resolve(const WindowPlacementData &data,
                         const QVector<ScreenInfo> &screens,
                         const QSize &fallbackSize);
};

} // namespace pp
