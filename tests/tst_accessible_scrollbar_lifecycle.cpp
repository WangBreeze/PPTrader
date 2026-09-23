#include <QAccessible>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <memory>

class AccessibleScrollBarLifecycleTest : public QObject {
    Q_OBJECT
private slots:
    void repeatedActivationDoesNotMultiplyAnAction_data()
    {
        QTest::addColumn<int>("activationCount");
        QTest::newRow("native") << 0;
        QTest::newRow("repeated") << 10;
        QTest::newRow("stress") << 1000;
    }
    void repeatedActivationDoesNotMultiplyAnAction()
    {
#ifndef Q_OS_WIN
        QSKIP("本回归验证 Windows 原生无障碍激活路径，其他平台需要独立入口");
#endif
        QFETCH(int, activationCount);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            ScrollBar {
                width: 20; height: 200
                orientation: Qt.Vertical
                size: 0.2; position: 0.2; stepSize: 0.05
                Accessible.role: Accessible.ScrollBar
                Accessible.name: "退出回归滚动条"
            }
        )", QUrl());
        std::unique_ptr<QObject> bar(component.create());
        QVERIFY2(bar, qPrintable(component.errorString()));
#ifdef Q_OS_WIN
        QQuickWindow window;
        // 仅访问测试自建窗口，沿 Windows 的原生活动通知激活平台无障碍。
        SendMessageW(reinterpret_cast<HWND>(window.winId()), WM_GETOBJECT, 0, -25);
#endif
        QVERIFY(QAccessible::isActive());
        for (int activation = 0; activation < activationCount; ++activation)
            QAccessible::setActive(true);
        auto *accessible = QAccessible::queryAccessibleInterface(bar.get());
        QVERIFY(accessible);
        auto *actions = accessible->actionInterface();
        QVERIFY(actions);
        actions->doAction(QAccessibleActionInterface::increaseAction());
        constexpr double expectedIncreasePosition = 0.25;
        // 一次辅助技术动作只能滚动一个步长，重复激活不能改变用户操作语义。
        QCOMPARE(bar->property("position").toDouble(),
                 expectedIncreasePosition);
        actions->doAction(QAccessibleActionInterface::decreaseAction());
        QCOMPARE(bar->property("position").toDouble(), 0.2);
        QAccessible::setActive(false);
        QAccessible::setActive(true);
        actions->doAction(QAccessibleActionInterface::increaseAction());
        QCOMPARE(bar->property("position").toDouble(),
                 expectedIncreasePosition);
    }
};

QTEST_MAIN(AccessibleScrollBarLifecycleTest)
#include "tst_accessible_scrollbar_lifecycle.moc"
