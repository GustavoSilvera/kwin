/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"
#include "x11client.h"
#include "cursor.h"
#include "input.h"
#include "internal_client.h"
#include "platform.h"
#include "screens.h"
#include "useractions.h"
#include "wayland_server.h"
#include "workspace.h"

#include <KWayland/Client/surface.h>
#include <KWaylandServer/keyboard_interface.h>
#include <KWaylandServer/seat_interface.h>

#include <KGlobalAccel>
#include <linux/input.h>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

using namespace KWin;
using namespace KWayland::Client;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_globalshortcuts-0");

class GlobalShortcutsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testNonLatinLayout_data();
    void testNonLatinLayout();
    void testConsumedShift();
    void testRepeatedTrigger();
    void testUserActionsMenu();
    void testMetaShiftW();
    void testComponseKey();
    void testX11ClientShortcut();
    void testWaylandClientShortcut();
    void testSetupWindowShortcut();
};

void GlobalShortcutsTest::initTestCase()
{
    qRegisterMetaType<KWin::AbstractClient *>();
    qRegisterMetaType<KWin::InternalClient *>();
    QSignalSpy applicationStartedSpy(kwinApp(), &Application::started);
    QVERIFY(applicationStartedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName));

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");
    qputenv("XKB_DEFAULT_RULES", "evdev");
    qputenv("XKB_DEFAULT_LAYOUT", "us,ru");

    kwinApp()->start();
    QVERIFY(applicationStartedSpy.wait());
    Test::initWaylandWorkspace();
}

void GlobalShortcutsTest::init()
{
    QVERIFY(Test::setupWaylandConnection());
    workspace()->setActiveOutput(QPoint(640, 512));
    KWin::Cursors::self()->mouse()->setPos(QPoint(640, 512));

    auto xkb = input()->keyboard()->xkb();
    xkb->switchToLayout(0);
}

void GlobalShortcutsTest::cleanup()
{
    Test::destroyWaylandConnection();
}

Q_DECLARE_METATYPE(Qt::Modifier)

void GlobalShortcutsTest::testNonLatinLayout_data()
{
    QTest::addColumn<int>("modifierKey");
    QTest::addColumn<Qt::Modifier>("qtModifier");
    QTest::addColumn<int>("key");
    QTest::addColumn<Qt::Key>("qtKey");

    for (const auto &modifier :
             QVector<QPair<int, Qt::Modifier>> {
                {KEY_LEFTCTRL, Qt::CTRL},
                {KEY_LEFTALT, Qt::ALT},
                {KEY_LEFTSHIFT, Qt::SHIFT},
                {KEY_LEFTMETA, Qt::META},
             } )
    {
        for (const auto &key :
             QVector<QPair<int, Qt::Key>> {

                 // Tab is example of a key usually the same on different layouts, check it first
                {KEY_TAB, Qt::Key_Tab},

                 // Then check a key with a Latin letter.
                 // The symbol will probably be differ on non-Latin layout.
                 // On Russian layout, "w" key has a cyrillic letter "ц"
                {KEY_W, Qt::Key_W},

             #if QT_VERSION_MAJOR > 5	// since Qt 5 LTS is frozen
                 // More common case with any Latin1 symbol keys, including punctuation, should work also.
                 // "`" key has a "ё" letter on Russian layout
                 // FIXME: QTBUG-90611
                {KEY_GRAVE, Qt::Key_QuoteLeft},
             #endif
             } )
        {
            QTest::newRow(QKeySequence(modifier.second + key.second).toString().toLatin1().constData())
                            << modifier.first << modifier.second << key.first << key.second;
        }
    }
}

void GlobalShortcutsTest::testNonLatinLayout()
{
    // Shortcuts on non-Latin layouts should still work, see BUG 375518
    auto xkb = input()->keyboard()->xkb();
    xkb->switchToLayout(1);
    QCOMPARE(xkb->layoutName(), QStringLiteral("Russian"));

    QFETCH(int, modifierKey);
    QFETCH(Qt::Modifier, qtModifier);
    QFETCH(int, key);
    QFETCH(Qt::Key, qtKey);

    const QKeySequence seq(qtModifier + qtKey);

    QScopedPointer<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName("globalshortcuts-test-non-latin-layout");

    QSignalSpy triggeredSpy(action.data(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());

    KGlobalAccel::self()->stealShortcutSystemwide(seq);
    KGlobalAccel::self()->setShortcut(action.data(), {seq}, KGlobalAccel::NoAutoloading);
    input()->registerShortcut(seq, action.data());

    quint32 timestamp = 0;
    kwinApp()->platform()->keyboardKeyPressed(modifierKey, timestamp++);
    QCOMPARE(input()->keyboardModifiers(), qtModifier);
    kwinApp()->platform()->keyboardKeyPressed(key, timestamp++);

    kwinApp()->platform()->keyboardKeyReleased(key, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(modifierKey, timestamp++);

    QTRY_COMPARE_WITH_TIMEOUT(triggeredSpy.count(), 1, 100);
}

void GlobalShortcutsTest::testConsumedShift()
{
    // this test verifies that a shortcut with a consumed shift modifier triggers
    // create the action
    QScopedPointer<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(QStringLiteral("globalshortcuts-test-consumed-shift"));
    QSignalSpy triggeredSpy(action.data(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());
    KGlobalAccel::self()->setShortcut(action.data(), QList<QKeySequence>{Qt::Key_Percent}, KGlobalAccel::NoAutoloading);
    input()->registerShortcut(Qt::Key_Percent, action.data());

    // press shift+5
    quint32 timestamp = 0;
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTSHIFT, timestamp++);
    QCOMPARE(input()->keyboardModifiers(), Qt::ShiftModifier);
    kwinApp()->platform()->keyboardKeyPressed(KEY_5, timestamp++);
    QTRY_COMPARE(triggeredSpy.count(), 1);
    kwinApp()->platform()->keyboardKeyReleased(KEY_5, timestamp++);

    // release shift
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTSHIFT, timestamp++);
}

void GlobalShortcutsTest::testRepeatedTrigger()
{
    // this test verifies that holding a key, triggers repeated global shortcut
    // in addition pressing another key should stop triggering the shortcut

    QScopedPointer<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(QStringLiteral("globalshortcuts-test-consumed-shift"));
    QSignalSpy triggeredSpy(action.data(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());
    KGlobalAccel::self()->setShortcut(action.data(), QList<QKeySequence>{Qt::Key_Percent}, KGlobalAccel::NoAutoloading);
    input()->registerShortcut(Qt::Key_Percent, action.data());

    // we need to configure the key repeat first. It is only enabled on libinput
    waylandServer()->seat()->keyboard()->setRepeatInfo(25, 300);

    // press shift+5
    quint32 timestamp = 0;
    kwinApp()->platform()->keyboardKeyPressed(KEY_WAKEUP, timestamp++);
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTSHIFT, timestamp++);
    QCOMPARE(input()->keyboardModifiers(), Qt::ShiftModifier);
    kwinApp()->platform()->keyboardKeyPressed(KEY_5, timestamp++);
    QTRY_COMPARE(triggeredSpy.count(), 1);
    // and should repeat
    QVERIFY(triggeredSpy.wait());
    QVERIFY(triggeredSpy.wait());
    // now release the key
    kwinApp()->platform()->keyboardKeyReleased(KEY_5, timestamp++);
    QVERIFY(!triggeredSpy.wait(50));

    kwinApp()->platform()->keyboardKeyReleased(KEY_WAKEUP, timestamp++);
    QVERIFY(!triggeredSpy.wait(50));

    // release shift
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTSHIFT, timestamp++);
}

void GlobalShortcutsTest::testUserActionsMenu()
{
    // this test tries to trigger the user actions menu with Alt+F3
    // the problem here is that pressing F3 consumes modifiers as it's part of the
    // Ctrl+alt+F3 keysym for vt switching. xkbcommon considers all modifiers as consumed
    // which a transformation to any keysym would cause
    // for more information see:
    // https://bugs.freedesktop.org/show_bug.cgi?id=92818
    // https://github.com/xkbcommon/libxkbcommon/issues/17

    // first create a window
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.data()));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->isActive());

    quint32 timestamp = 0;
    QVERIFY(!workspace()->userActionsMenu()->isShown());
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTALT, timestamp++);
    kwinApp()->platform()->keyboardKeyPressed(KEY_F3, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_F3, timestamp++);
    QTRY_VERIFY(workspace()->userActionsMenu()->isShown());
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTALT, timestamp++);
}

void GlobalShortcutsTest::testMetaShiftW()
{
    // BUG 370341
    QScopedPointer<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(QStringLiteral("globalshortcuts-test-meta-shift-w"));
    QSignalSpy triggeredSpy(action.data(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());
    KGlobalAccel::self()->setShortcut(action.data(), QList<QKeySequence>{Qt::META + Qt::SHIFT + Qt::Key_W}, KGlobalAccel::NoAutoloading);
    input()->registerShortcut(Qt::META + Qt::SHIFT + Qt::Key_W, action.data());

    // press meta+shift+w
    quint32 timestamp = 0;
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTMETA, timestamp++);
    QCOMPARE(input()->keyboardModifiers(), Qt::MetaModifier);
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTSHIFT, timestamp++);
    QCOMPARE(input()->keyboardModifiers(), Qt::ShiftModifier | Qt::MetaModifier);
    kwinApp()->platform()->keyboardKeyPressed(KEY_W, timestamp++);
    QTRY_COMPARE(triggeredSpy.count(), 1);
    kwinApp()->platform()->keyboardKeyReleased(KEY_W, timestamp++);

    // release meta+shift
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTSHIFT, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTMETA, timestamp++);
}

void GlobalShortcutsTest::testComponseKey()
{
    // BUG 390110
    QScopedPointer<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(QStringLiteral("globalshortcuts-accent"));
    QSignalSpy triggeredSpy(action.data(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());
    KGlobalAccel::self()->setShortcut(action.data(), QList<QKeySequence>{Qt::UNICODE_ACCEL}, KGlobalAccel::NoAutoloading);
    input()->registerShortcut(Qt::UNICODE_ACCEL, action.data());

    // press & release `
    quint32 timestamp = 0;
    kwinApp()->platform()->keyboardKeyPressed(KEY_RESERVED, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_RESERVED, timestamp++);

    QTRY_COMPARE(triggeredSpy.count(), 0);
}

struct XcbConnectionDeleter
{
    static inline void cleanup(xcb_connection_t *pointer)
    {
        xcb_disconnect(pointer);
    }
};

void GlobalShortcutsTest::testX11ClientShortcut()
{
#ifdef NO_XWAYLAND
    QSKIP("x11 test, unnecessary without xwayland");
#endif
    // create an X11 window
    QScopedPointer<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
    QVERIFY(!xcb_connection_has_error(c.data()));
    xcb_window_t w = xcb_generate_id(c.data());
    const QRect windowGeometry = QRect(0, 0, 10, 20);
    const uint32_t values[] = {
        XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_LEAVE_WINDOW
    };
    xcb_create_window(c.data(), XCB_COPY_FROM_PARENT, w, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, values);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.data(), w, &hints);
    NETWinInfo info(c.data(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Normal);
    xcb_map_window(c.data(), w);
    xcb_flush(c.data());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    X11Client *client = windowCreatedSpy.last().first().value<X11Client *>();
    QVERIFY(client);

    QCOMPARE(workspace()->activeClient(), client);
    QVERIFY(client->isActive());
    QCOMPARE(client->shortcut(), QKeySequence());
    const QKeySequence seq(Qt::META + Qt::SHIFT + Qt::Key_Y);
    QVERIFY(workspace()->shortcutAvailable(seq));
    client->setShortcut(seq.toString());
    QCOMPARE(client->shortcut(), seq);
    QVERIFY(!workspace()->shortcutAvailable(seq));
    QCOMPARE(client->caption(), QStringLiteral(" {Meta+Shift+Y}"));

    // it's delayed
    QCoreApplication::processEvents();

    workspace()->activateClient(nullptr);
    QVERIFY(!workspace()->activeClient());
    QVERIFY(!client->isActive());

    // now let's trigger the shortcut
    quint32 timestamp = 0;
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTMETA, timestamp++);
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTSHIFT, timestamp++);
    kwinApp()->platform()->keyboardKeyPressed(KEY_Y, timestamp++);
    QTRY_COMPARE(workspace()->activeClient(), client);
    kwinApp()->platform()->keyboardKeyReleased(KEY_Y, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTSHIFT, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTMETA, timestamp++);

    // destroy window again
    QSignalSpy windowClosedSpy(client, &X11Client::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.data(), w);
    xcb_destroy_window(c.data(), w);
    xcb_flush(c.data());
    QVERIFY(windowClosedSpy.wait());
}

void GlobalShortcutsTest::testWaylandClientShortcut()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.data()));
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);

    QCOMPARE(workspace()->activeClient(), client);
    QVERIFY(client->isActive());
    QCOMPARE(client->shortcut(), QKeySequence());
    const QKeySequence seq(Qt::META + Qt::SHIFT + Qt::Key_Y);
    QVERIFY(workspace()->shortcutAvailable(seq));
    client->setShortcut(seq.toString());
    QCOMPARE(client->shortcut(), seq);
    QVERIFY(!workspace()->shortcutAvailable(seq));
    QCOMPARE(client->caption(), QStringLiteral(" {Meta+Shift+Y}"));

    workspace()->activateClient(nullptr);
    QVERIFY(!workspace()->activeClient());
    QVERIFY(!client->isActive());

    // now let's trigger the shortcut
    quint32 timestamp = 0;
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTMETA, timestamp++);
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTSHIFT, timestamp++);
    kwinApp()->platform()->keyboardKeyPressed(KEY_Y, timestamp++);
    QTRY_COMPARE(workspace()->activeClient(), client);
    kwinApp()->platform()->keyboardKeyReleased(KEY_Y, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTSHIFT, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTMETA, timestamp++);

    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
    QTRY_VERIFY_WITH_TIMEOUT(workspace()->shortcutAvailable(seq), 500); //we need the try since KGlobalAccelPrivate::unregister is async
}

void GlobalShortcutsTest::testSetupWindowShortcut()
{
    // QTBUG-62102

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.data()));
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);

    QCOMPARE(workspace()->activeClient(), client);
    QVERIFY(client->isActive());
    QCOMPARE(client->shortcut(), QKeySequence());

    QSignalSpy shortcutDialogAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(shortcutDialogAddedSpy.isValid());
    workspace()->slotSetupWindowShortcut();
    QTRY_COMPARE(shortcutDialogAddedSpy.count(), 1);
    auto dialog = shortcutDialogAddedSpy.first().first().value<InternalClient *>();
    QVERIFY(dialog);
    QVERIFY(dialog->isInternal());
    auto sequenceEdit = workspace()->shortcutDialog()->findChild<QKeySequenceEdit*>();
    QVERIFY(sequenceEdit);

    // the QKeySequenceEdit field does not get focus, we need to pass it focus manually
    QEXPECT_FAIL("", "Edit does not have focus", Continue);
    QVERIFY(sequenceEdit->hasFocus());
    sequenceEdit->setFocus();
    QTRY_VERIFY(sequenceEdit->hasFocus());

    quint32 timestamp = 0;
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTMETA, timestamp++);
    kwinApp()->platform()->keyboardKeyPressed(KEY_LEFTSHIFT, timestamp++);
    kwinApp()->platform()->keyboardKeyPressed(KEY_Y, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_Y, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTSHIFT, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_LEFTMETA, timestamp++);

    // the sequence gets accepted after one second, so wait a bit longer
    QTest::qWait(2000);
    // now send in enter
    kwinApp()->platform()->keyboardKeyPressed(KEY_ENTER, timestamp++);
    kwinApp()->platform()->keyboardKeyReleased(KEY_ENTER, timestamp++);
    QTRY_COMPARE(client->shortcut(), QKeySequence(Qt::META + Qt::SHIFT + Qt::Key_Y));
}

WAYLANDTEST_MAIN(GlobalShortcutsTest)
#include "globalshortcuts_test.moc"
