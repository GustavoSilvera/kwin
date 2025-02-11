/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WAYLAND_TEST_H
#define KWIN_WAYLAND_TEST_H

#include "main.h"

// Qt
#include <QtTest>

#include "qwayland-idle-inhibit-unstable-v1.h"
#include "qwayland-wlr-layer-shell-unstable-v1.h"
#include "qwayland-text-input-unstable-v3.h"
#include "qwayland-xdg-decoration-unstable-v1.h"
#include "qwayland-xdg-shell.h"

namespace KWayland
{
namespace Client
{
class AppMenuManager;
class ConnectionThread;
class Compositor;
class Output;
class PlasmaShell;
class PlasmaWindowManagement;
class PointerConstraints;
class Seat;
class ServerSideDecorationManager;
class ShadowManager;
class ShmPool;
class SubCompositor;
class SubSurface;
class Surface;
class OutputManagement;
class TextInputManager;
class OutputDevice;
}
}

namespace QtWayland
{
class zwp_input_panel_surface_v1;
class zwp_text_input_v3;
class zwp_text_input_manager_v3;
}

namespace KWin
{
namespace Xwl
{
class Xwayland;
}

class AbstractClient;

class WaylandTestApplication : public ApplicationWaylandAbstract
{
    Q_OBJECT
public:
    WaylandTestApplication(OperationMode mode, int &argc, char **argv);
    ~WaylandTestApplication() override;

    void setInputMethodServerToStart(const QString &inputMethodServer) {
        m_inputMethodServerToStart = inputMethodServer;
    }
protected:
    void performStartup() override;

private:
    void continueStartupWithScreens();
    void continueStartupWithScene();
    void finalizeStartup();

    Xwl::Xwayland *m_xwayland = nullptr;
    QString m_inputMethodServerToStart;
};

namespace Test
{

class MockInputMethod;

class TextInputManagerV3 : public QtWayland::zwp_text_input_manager_v3
{
public:
    ~TextInputManagerV3() override { destroy(); }
};

class TextInputV3 : public QtWayland::zwp_text_input_v3
{
    ~TextInputV3() override { destroy(); }
};

class LayerShellV1 : public QtWayland::zwlr_layer_shell_v1
{
public:
    ~LayerShellV1() override;
};

class LayerSurfaceV1 : public QObject, public QtWayland::zwlr_layer_surface_v1
{
    Q_OBJECT

public:
    ~LayerSurfaceV1() override;

protected:
    void zwlr_layer_surface_v1_configure(uint32_t serial, uint32_t width, uint32_t height) override;
    void zwlr_layer_surface_v1_closed() override;

Q_SIGNALS:
    void closeRequested();
    void configureRequested(quint32 serial, const QSize &size);
};

/**
 * The XdgShell class represents the @c xdg_wm_base global.
 */
class XdgShell : public QtWayland::xdg_wm_base
{
public:
    ~XdgShell() override;
    void xdg_wm_base_ping(uint32_t serial) override
    {
        pong(serial);
    }
};

/**
 * The XdgSurface class represents an xdg_surface object.
 */
class XdgSurface : public QObject, public QtWayland::xdg_surface
{
    Q_OBJECT

public:
    explicit XdgSurface(XdgShell *shell, KWayland::Client::Surface *surface, QObject *parent = nullptr);
    ~XdgSurface() override;

    KWayland::Client::Surface *surface() const;

Q_SIGNALS:
    void configureRequested(quint32 serial);

protected:
    void xdg_surface_configure(uint32_t serial) override;

private:
    KWayland::Client::Surface *m_surface;
};

/**
 * The XdgToplevel class represents an xdg_toplevel surface. Note that the XdgToplevel surface
 * takes the ownership of the underlying XdgSurface object.
 */
class XdgToplevel : public QObject, public QtWayland::xdg_toplevel
{
    Q_OBJECT

public:
    enum class State {
        Maximized  = 1 << 0,
        Fullscreen = 1 << 1,
        Resizing   = 1 << 2,
        Activated  = 1 << 3
    };
    Q_DECLARE_FLAGS(States, State)

    explicit XdgToplevel(XdgSurface *surface, QObject *parent = nullptr);
    ~XdgToplevel() override;

    XdgSurface *xdgSurface() const;

Q_SIGNALS:
    void configureRequested(const QSize &size, KWin::Test::XdgToplevel::States states);
    void closeRequested();

protected:
    void xdg_toplevel_configure(int32_t width, int32_t height, wl_array *states) override;
    void xdg_toplevel_close() override;

private:
    QScopedPointer<XdgSurface> m_xdgSurface;
};

/**
 * The XdgPositioner class represents an xdg_positioner object.
 */
class XdgPositioner : public QtWayland::xdg_positioner
{
public:
    explicit XdgPositioner(XdgShell *shell);
    ~XdgPositioner() override;
};

/**
 * The XdgPopup class represents an xdg_popup surface. Note that the XdgPopup surface takes
 * the ownership of the underlying XdgSurface object.
 */
class XdgPopup : public QObject, public QtWayland::xdg_popup
{
    Q_OBJECT

public:
    XdgPopup(XdgSurface *surface, XdgSurface *parentSurface, XdgPositioner *positioner, QObject *parent = nullptr);
    ~XdgPopup() override;

    XdgSurface *xdgSurface() const;

Q_SIGNALS:
    void configureRequested(const QRect &rect);
    void doneReceived();

protected:
    void xdg_popup_configure(int32_t x, int32_t y, int32_t width, int32_t height) override;
    void xdg_popup_popup_done() override;

private:
    QScopedPointer<XdgSurface> m_xdgSurface;
};

class XdgDecorationManagerV1 : public QtWayland::zxdg_decoration_manager_v1
{
public:
    ~XdgDecorationManagerV1() override;
};

class XdgToplevelDecorationV1 : public QObject, public QtWayland::zxdg_toplevel_decoration_v1
{
    Q_OBJECT

public:
    XdgToplevelDecorationV1(XdgDecorationManagerV1 *manager, XdgToplevel *toplevel, QObject *parent = nullptr);
    ~XdgToplevelDecorationV1() override;

Q_SIGNALS:
    void configureRequested(QtWayland::zxdg_toplevel_decoration_v1::mode mode);

protected:
    void zxdg_toplevel_decoration_v1_configure(uint32_t mode) override;
};

class IdleInhibitManagerV1 : public QtWayland::zwp_idle_inhibit_manager_v1
{
public:
    ~IdleInhibitManagerV1() override;
};

class IdleInhibitorV1 : public QtWayland::zwp_idle_inhibitor_v1
{
public:
    IdleInhibitorV1(IdleInhibitManagerV1 *manager, KWayland::Client::Surface *surface);
    ~IdleInhibitorV1() override;
};

enum class AdditionalWaylandInterface {
    Seat = 1 << 0,
    Decoration = 1 << 1,
    PlasmaShell = 1 << 2,
    WindowManagement = 1 << 3,
    PointerConstraints = 1 << 4,
    IdleInhibitV1 = 1 << 5,
    AppMenu = 1 << 6,
    ShadowManager = 1 << 7,
    XdgDecorationV1 = 1 << 8,
    OutputManagement = 1 << 9,
    TextInputManagerV2 = 1 << 10,
    InputMethodV1 = 1 << 11,
    LayerShellV1 = 1 << 12,
    TextInputManagerV3 = 1 << 13,
    OutputDevice = 1 << 14,
};
Q_DECLARE_FLAGS(AdditionalWaylandInterfaces, AdditionalWaylandInterface)
/**
 * Creates a Wayland Connection in a dedicated thread and creates various
 * client side objects which can be used to create windows.
 * @returns @c true if created successfully, @c false if there was an error
 * @see destroyWaylandConnection
 */
bool setupWaylandConnection(AdditionalWaylandInterfaces flags = AdditionalWaylandInterfaces());

/**
 * Destroys the Wayland Connection created with @link{setupWaylandConnection}.
 * This can be called from cleanup in order to ensure that no Wayland Connection
 * leaks into the next test method.
 * @see setupWaylandConnection
 */
void destroyWaylandConnection();

KWayland::Client::ConnectionThread *waylandConnection();
KWayland::Client::Compositor *waylandCompositor();
KWayland::Client::SubCompositor *waylandSubCompositor();
KWayland::Client::ShadowManager *waylandShadowManager();
KWayland::Client::ShmPool *waylandShmPool();
KWayland::Client::Seat *waylandSeat();
KWayland::Client::ServerSideDecorationManager *waylandServerSideDecoration();
KWayland::Client::PlasmaShell *waylandPlasmaShell();
KWayland::Client::PlasmaWindowManagement *waylandWindowManagement();
KWayland::Client::PointerConstraints *waylandPointerConstraints();
KWayland::Client::AppMenuManager *waylandAppMenuManager();
KWayland::Client::OutputManagement *waylandOutputManagement();
KWayland::Client::TextInputManager *waylandTextInputManager();
QVector<KWayland::Client::Output *> waylandOutputs();
QVector<KWayland::Client::OutputDevice *> waylandOutputDevices();

bool waitForWaylandSurface(AbstractClient *client);

bool waitForWaylandPointer();
bool waitForWaylandTouch();
bool waitForWaylandKeyboard();

void flushWaylandConnection();

KWayland::Client::Surface *createSurface(QObject *parent = nullptr);
KWayland::Client::SubSurface *createSubSurface(KWayland::Client::Surface *surface,
                                               KWayland::Client::Surface *parentSurface, QObject *parent = nullptr);

LayerSurfaceV1 *createLayerSurfaceV1(KWayland::Client::Surface *surface,
                                     const QString &scope,
                                     KWayland::Client::Output *output = nullptr,
                                     LayerShellV1::layer layer = LayerShellV1::layer_top);

TextInputManagerV3 *waylandTextInputManagerV3();

enum class CreationSetup {
    CreateOnly,
    CreateAndConfigure, /// commit and wait for the configure event, making this surface ready to commit buffers
};

QtWayland::zwp_input_panel_surface_v1 *createInputPanelSurfaceV1(KWayland::Client::Surface *surface,
                                                                 KWayland::Client::Output *output);

XdgToplevel *createXdgToplevelSurface(KWayland::Client::Surface *surface, QObject *parent = nullptr,
                                      CreationSetup configureMode = CreationSetup::CreateAndConfigure);

XdgPositioner *createXdgPositioner();

XdgPopup *createXdgPopupSurface(KWayland::Client::Surface *surface, XdgSurface *parentSurface,
                                XdgPositioner *positioner, QObject *parent = nullptr,
                                CreationSetup configureMode = CreationSetup::CreateAndConfigure);

XdgToplevelDecorationV1 *createXdgToplevelDecorationV1(XdgToplevel *toplevel, QObject *parent = nullptr);
IdleInhibitorV1 *createIdleInhibitorV1(KWayland::Client::Surface *surface);


/**
 * Creates a shared memory buffer of @p size in @p color and attaches it to the @p surface.
 * The @p surface gets damaged and committed, thus it's rendered.
 */
void render(KWayland::Client::Surface *surface, const QSize &size, const QColor &color, const QImage::Format &format = QImage::Format_ARGB32_Premultiplied);

/**
 * Creates a shared memory buffer using the supplied image @p img and attaches it to the @p surface
 */
void render(KWayland::Client::Surface *surface, const QImage &img);

/**
 * Waits till a new AbstractClient is shown and returns the created AbstractClient.
 * If no AbstractClient gets shown during @p timeout @c null is returned.
 */
AbstractClient *waitForWaylandWindowShown(int timeout = 5000);

/**
 * Combination of @link{render} and @link{waitForWaylandWindowShown}.
 */
AbstractClient *renderAndWaitForShown(KWayland::Client::Surface *surface, const QSize &size, const QColor &color, const QImage::Format &format = QImage::Format_ARGB32, int timeout = 5000);

/**
 * Waits for the @p client to be destroyed.
 */
bool waitForWindowDestroyed(AbstractClient *client);

/**
 * Locks the screen and waits till the screen is locked.
 * @returns @c true if the screen could be locked, @c false otherwise
 */
bool lockScreen();

/**
 * Unlocks the screen and waits till the screen is unlocked.
 * @returns @c true if the screen could be unlocked, @c false otherwise
 */
bool unlockScreen();

void initWaylandWorkspace();

AbstractClient *inputPanelClient();
KWayland::Client::Surface *inputPanelSurface();

}

}

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::Test::AdditionalWaylandInterfaces)
Q_DECLARE_METATYPE(KWin::Test::XdgToplevel::States)
Q_DECLARE_METATYPE(QtWayland::zxdg_toplevel_decoration_v1::mode)

#define WAYLANDTEST_MAIN_HELPER(TestObject, DPI, OperationMode) \
int main(int argc, char *argv[]) \
{ \
    setenv("QT_QPA_PLATFORM", "wayland-org.kde.kwin.qpa", true); \
    setenv("QT_QPA_PLATFORM_PLUGIN_PATH", QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath().toLocal8Bit().constData(), true); \
    setenv("KWIN_FORCE_OWN_QPA", "1", true); \
    qunsetenv("KDE_FULL_SESSION"); \
    qunsetenv("KDE_SESSION_VERSION"); \
    qunsetenv("XDG_SESSION_DESKTOP"); \
    qunsetenv("XDG_CURRENT_DESKTOP"); \
    DPI; \
    KWin::WaylandTestApplication app(OperationMode, argc, argv); \
    app.setAttribute(Qt::AA_Use96Dpi, true); \
    TestObject tc; \
    return QTest::qExec(&tc, argc, argv); \
}

#ifdef NO_XWAYLAND
#define WAYLANDTEST_MAIN(TestObject) WAYLANDTEST_MAIN_HELPER(TestObject, QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps), KWin::Application::OperationModeWaylandOnly)
#else
#define WAYLANDTEST_MAIN(TestObject) WAYLANDTEST_MAIN_HELPER(TestObject, QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps), KWin::Application::OperationModeXwayland)
#endif

#endif
