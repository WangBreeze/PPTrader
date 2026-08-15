#include "app/WindowPlacement.h"

#include <QTest>

namespace pp {

class WindowPlacementTest : public QObject {
    Q_OBJECT

private slots:
    void emptyScreensReturnsDefaultSize();
    void restoresSavedGeometryOnSameScreen();
    void fallsBackToPrimaryWhenScreenMissing();
    void fallsBackToCenterWhenGeometryOffScreen();
    void clampsOversizedGeometryToAvailableArea();
    void clampsDefaultSizeToAvailableArea();
};

namespace {
ScreenInfo primary()
{
    return {QStringLiteral("P"), QRect(0, 0, 1920, 1040)};
}

ScreenInfo secondary()
{
    return {QStringLiteral("S"), QRect(1920, 0, 2560, 1400)};
}
} // namespace

void WindowPlacementTest::emptyScreensReturnsDefaultSize()
{
    const QRect result = WindowPlacement::resolve(
        WindowPlacementData{}, {}, QSize(1600, 1000));
    QCOMPARE(result, QRect(0, 0, 1600, 1000));
}

void WindowPlacementTest::restoresSavedGeometryOnSameScreen()
{
    WindowPlacementData data;
    data.screenName = QStringLiteral("S");
    data.geometry = QRect(2000, 100, 1600, 1000);
    data.hasGeometry = true;

    const QRect result = WindowPlacement::resolve(
        data, {primary(), secondary()}, QSize(1600, 1000));
    QCOMPARE(result, data.geometry); // 几何在 S 屏幕内，原样恢复。
}

void WindowPlacementTest::fallsBackToPrimaryWhenScreenMissing()
{
    WindowPlacementData data;
    data.screenName = QStringLiteral("REMOVED");
    data.geometry = QRect(3000, 200, 1600, 1000);
    data.hasGeometry = true;

    const QRect result = WindowPlacement::resolve(
        data, {primary(), secondary()}, QSize(1600, 1000));
    // 回退主屏幕 P 居中：P 可用 1920×1040，尺寸 1600×1000。
    QCOMPARE(result, QRect(160, 20, 1600, 1000));
}

void WindowPlacementTest::fallsBackToCenterWhenGeometryOffScreen()
{
    WindowPlacementData data;
    data.screenName = QStringLiteral("S");
    data.geometry = QRect(-3000, -3000, 1600, 1000);
    data.hasGeometry = true;

    const QRect result = WindowPlacement::resolve(
        data, {primary(), secondary()}, QSize(1600, 1000));
    // 保存几何与 S 无交叠，回退 S 居中：S 可用 2560×1400，尺寸 1600×1000。
    const QRect expected(1920 + (2560 - 1600) / 2, (1400 - 1000) / 2, 1600, 1000);
    QCOMPARE(result, expected);
}

void WindowPlacementTest::clampsOversizedGeometryToAvailableArea()
{
    WindowPlacementData data;
    data.screenName = QStringLiteral("P");
    data.geometry = QRect(0, 0, 5000, 5000);
    data.hasGeometry = true;

    const QRect result = WindowPlacement::resolve(
        data, {primary(), secondary()}, QSize(1600, 1000));
    QCOMPARE(result, QRect(0, 0, 1920, 1040)); // 钳制到 P 可用区域。
}

void WindowPlacementTest::clampsDefaultSizeToAvailableArea()
{
    WindowPlacementData data; // 无保存几何，尺寸用 fallbackSize。
    const QRect result = WindowPlacement::resolve(
        data, {primary()}, QSize(3000, 2000));
    QCOMPARE(result, QRect(0, 0, 1920, 1040)); // 夹紧到 P 可用区域。
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::WindowPlacementTest)

#include "tst_window_placement.moc"
