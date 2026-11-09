// 回归：Qt 6.11 在“项已持有 active focus”时拒绝把 activeFocusOnTab 置为 false
// （focusPolicy 走同一处检查），并打印
//   QQuickItem: Cannot set activeFocusOnTab to false once item is the active focus item.
// 赋值会被静默丢弃，按钮会一直留在 Tab 链里。PPTrader 的 PpButton 等组件改用
// “可用性 || activeFocus” 表达该属性：持有焦点时保持可聚焦，焦点离开后才退出 Tab 链。
// 本测试用真实 PpButton 锁住这个行为，避免以后又被改回直接翻转。
//
// 说明：告警通过 QMessageHandler 捕获，因此不受发行版 Qt 把日志送进 systemd journal
// 的影响，也不需要 QT_FORCE_STDERR_LOGGING。

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <memory>

namespace {

QStringList g_activeFocusWarnings;
QtMessageHandler g_previousHandler = nullptr;

void captureHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.contains(QStringLiteral("Cannot set activeFocusOnTab")))
        g_activeFocusWarnings.append(message);
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

} // namespace

class FocusAvailabilityTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        g_previousHandler = qInstallMessageHandler(captureHandler);
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previousHandler);
    }

    void disabledButtonLeavesTabChainOnlyAfterFocusMovesAway()
    {
        QQmlEngine engine;
        const QString componentsDir = QStringLiteral(PPTRADER_SOURCE_DIR "/qml/components");
        engine.addImportPath(componentsDir);

        QQmlComponent themeComponent(
            &engine, QUrl::fromLocalFile(componentsDir + QStringLiteral("/PPTheme.qml")));
        std::unique_ptr<QObject> theme(themeComponent.create());
        QVERIFY2(theme, qPrintable(themeComponent.errorString()));

        QQmlComponent buttonComponent(
            &engine, QUrl::fromLocalFile(componentsDir + QStringLiteral("/PpButton.qml")));
        std::unique_ptr<QObject> button(buttonComponent.create());
        QVERIFY2(button, qPrintable(buttonComponent.errorString()));
        button->setProperty("theme", QVariant::fromValue(theme.get()));

        auto *item = qobject_cast<QQuickItem *>(button.get());
        QVERIFY(item);
        item->setWidth(120);
        item->setHeight(36);

        QQuickWindow window;
        window.resize(400, 300);
        item->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        item->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY2(item->hasActiveFocus(),
                 "测试前提不成立：必须先让按钮真正持有 active focus，否则本回归没有意义");
        QVERIFY(item->property("activeFocusOnTab").toBool());

        // 1) 持有焦点时变为不可用：不得翻转属性，也不得打印 Qt 告警
        g_activeFocusWarnings.clear();
        button->setProperty("available", false);
        QVERIFY2(item->property("activeFocusOnTab").toBool(),
                 "持有焦点时 activeFocusOnTab 被翻成 false；Qt 会拒绝该赋值并告警");
        QVERIFY2(item->property("focusPolicy").toInt() != int(Qt::NoFocus),
                 "持有焦点时 focusPolicy 被翻成 NoFocus；Qt 会拒绝该赋值并告警");
        QCOMPARE(g_activeFocusWarnings.size(), 0);

        // 2) 焦点移走后应正常退出 Tab 链，且仍然不告警
        QQuickItem other;
        other.setParentItem(window.contentItem());
        other.setWidth(10);
        other.setHeight(10);
        other.setActiveFocusOnTab(true);
        other.forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(!item->hasActiveFocus());
        QVERIFY2(!item->property("activeFocusOnTab").toBool(),
                 "焦点已离开且按钮不可用，应退出 Tab 焦点链");
        QCOMPARE(item->property("focusPolicy").toInt(), int(Qt::NoFocus));
        QCOMPARE(g_activeFocusWarnings.size(), 0);

        // 3) 恢复可用后重新进入 Tab 链
        button->setProperty("available", true);
        QVERIFY(item->property("activeFocusOnTab").toBool());
        QCOMPARE(item->property("focusPolicy").toInt(), int(Qt::TabFocus));
        QCOMPARE(g_activeFocusWarnings.size(), 0);
    }
};

QTEST_MAIN(FocusAvailabilityTest)
#include "tst_focus_availability_qml.moc"
