#include "app/WindowPlacement.h"

#include <QSettings>

namespace pp {

namespace {
const QString kGroup = QStringLiteral("windowPlacement");
const QString kScreenKey = QStringLiteral("screenName");
const QString kGeometryKey = QStringLiteral("geometry");
const int kMinVisibleEdge = 96; // 恢复几何至少需与目标屏幕有 96px 交叠才视为可见。
} // namespace

WindowPlacementData WindowPlacement::load()
{
    WindowPlacementData data;
    QSettings settings;
    settings.beginGroup(kGroup);
    data.screenName = settings.value(kScreenKey).toString();
    data.geometry = settings.value(kGeometryKey).toRect();
    data.hasGeometry = settings.contains(kGeometryKey) && data.geometry.isValid()
        && data.geometry.width() > 0 && data.geometry.height() > 0;
    settings.endGroup();
    return data;
}

void WindowPlacement::save(const QString &screenName, const QRect &geometry)
{
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.setValue(kScreenKey, screenName);
    settings.setValue(kGeometryKey, geometry);
    settings.endGroup();
    settings.sync();
}

QRect WindowPlacement::resolve(const WindowPlacementData &data,
                               const QVector<ScreenInfo> &screens,
                               const QSize &fallbackSize)
{
    const QSize desiredSize = data.hasGeometry ? data.geometry.size() : fallbackSize;
    if (screens.isEmpty()) {
        return QRect(QPoint(0, 0), desiredSize);
    }

    // 定位目标屏幕：优先同名，其次主屏幕（首项）。
    const ScreenInfo *target = &screens.first();
    for (const ScreenInfo &screen : screens) {
        if (screen.name == data.screenName) {
            target = &screen;
            break;
        }
    }

    const QRect available = target->availableGeometry;
    if (data.hasGeometry) {
        const QRect intersection = data.geometry.intersected(available);
        if (intersection.width() >= kMinVisibleEdge && intersection.height() >= kMinVisibleEdge) {
            // 保存几何基本可见：保留其尺寸，钳制到目标屏幕可用区域内。
            QRect result = data.geometry;
            if (result.width() > available.width()) result.setWidth(available.width());
            if (result.height() > available.height()) result.setHeight(available.height());
            if (result.left() < available.left()) result.moveLeft(available.left());
            if (result.top() < available.top()) result.moveTop(available.top());
            if (result.right() > available.right()) result.moveRight(available.right());
            if (result.bottom() > available.bottom()) result.moveBottom(available.bottom());
            return result;
        }
    }

    // 回退：目标屏幕居中，尺寸夹紧到可用区域。
    QSize size = desiredSize;
    if (size.width() > available.width()) size.setWidth(available.width());
    if (size.height() > available.height()) size.setHeight(available.height());
    const QPoint topLeft = available.topLeft()
        + QPoint((available.width() - size.width()) / 2,
                 (available.height() - size.height()) / 2);
    return QRect(topLeft, size);
}

} // namespace pp
