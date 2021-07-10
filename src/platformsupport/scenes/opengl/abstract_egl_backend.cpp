/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "abstract_egl_backend.h"
#include "egl_dmabuf.h"
#include "composite.h"
#include "egl_context_attribute_builder.h"
#include "options.h"
#include "platform.h"
#include "wayland_server.h"
#include "abstract_wayland_output.h"
#include <KWaylandServer/display.h>
// kwin libs
#include <logging.h>
#include <kwinglplatform.h>
#include <kwinglutils.h>
// Qt
#include <QOpenGLContext>

#include <memory>

namespace KWin
{

static EGLContext s_globalShareContext = EGL_NO_CONTEXT;

static bool isOpenGLES_helper()
{
    if (qstrcmp(qgetenv("KWIN_COMPOSE"), "O2ES") == 0) {
        return true;
    }
    return QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES;
}

static EGLContext ensureGlobalShareContext()
{
    const EGLDisplay eglDisplay = kwinApp()->platform()->sceneEglDisplay();
    const EGLConfig eglConfig = kwinApp()->platform()->sceneEglConfig();

    if (kwinApp()->platform()->sceneEglGlobalShareContext() != EGL_NO_CONTEXT) {
        return kwinApp()->platform()->sceneEglGlobalShareContext();
    }

    std::vector<int> attribs;
    if (isOpenGLES_helper()) {
        EglOpenGLESContextAttributeBuilder builder;
        builder.setVersion(2);
        attribs = builder.build();
    } else {
        EglContextAttributeBuilder builder;
        attribs = builder.build();
    }

    s_globalShareContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, attribs.data());
    if (s_globalShareContext == EGL_NO_CONTEXT) {
        qCWarning(KWIN_OPENGL, "Failed to create global share context: 0x%x", eglGetError());
    }

    kwinApp()->platform()->setSceneEglGlobalShareContext(s_globalShareContext);
    return s_globalShareContext;
}

static void destroyGlobalShareContext()
{
    const EGLDisplay eglDisplay = kwinApp()->platform()->sceneEglDisplay();
    if (eglDisplay == EGL_NO_DISPLAY || s_globalShareContext == EGL_NO_CONTEXT) {
        return;
    }
    eglDestroyContext(eglDisplay, s_globalShareContext);
    s_globalShareContext = EGL_NO_CONTEXT;
    kwinApp()->platform()->setSceneEglGlobalShareContext(EGL_NO_CONTEXT);
}

AbstractEglBackend *AbstractEglBackend::s_primaryBackend = nullptr;

AbstractEglBackend::AbstractEglBackend()
    : OpenGLBackend()
{
    if (s_primaryBackend == nullptr) {
        setPrimaryBackend(this);
    }
    connect(Compositor::self(), &Compositor::aboutToDestroy, this, &AbstractEglBackend::teardown);
}

AbstractEglBackend::~AbstractEglBackend()
{
    delete m_dmaBuf;
}

void AbstractEglBackend::teardown()
{
    if (m_functions.eglUnbindWaylandDisplayWL && m_display != EGL_NO_DISPLAY) {
        m_functions.eglUnbindWaylandDisplayWL(m_display, *(WaylandServer::self()->display()));
    }
    destroyGlobalShareContext();
}

void AbstractEglBackend::cleanup()
{
    cleanupSurfaces();
    if (isPrimary()) {
        cleanupGL();
        doneCurrent();
        eglDestroyContext(m_display, m_context);
        eglReleaseThread();
        kwinApp()->platform()->setSceneEglContext(EGL_NO_CONTEXT);
        kwinApp()->platform()->setSceneEglConfig(nullptr);
    }
}

void AbstractEglBackend::cleanupSurfaces()
{
    if (m_surface != EGL_NO_SURFACE) {
        eglDestroySurface(m_display, m_surface);
    }
}

bool AbstractEglBackend::initEglAPI()
{
    EGLint major, minor;
    if (eglInitialize(m_display, &major, &minor) == EGL_FALSE) {
        qCWarning(KWIN_OPENGL) << "eglInitialize failed";
        EGLint error = eglGetError();
        if (error != EGL_SUCCESS) {
            qCWarning(KWIN_OPENGL) << "Error during eglInitialize " << error;
        }
        return false;
    }
    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        qCWarning(KWIN_OPENGL) << "Error during eglInitialize " << error;
        return false;
    }
    qCDebug(KWIN_OPENGL) << "Egl Initialize succeeded";

    if (eglBindAPI(isOpenGLES() ? EGL_OPENGL_ES_API : EGL_OPENGL_API) == EGL_FALSE) {
        qCCritical(KWIN_OPENGL) << "bind OpenGL API failed";
        return false;
    }
    qCDebug(KWIN_OPENGL) << "EGL version: " << major << "." << minor;
    const QByteArray eglExtensions = eglQueryString(m_display, EGL_EXTENSIONS);
    setExtensions(eglExtensions.split(' '));
    setSupportsSurfacelessContext(hasExtension(QByteArrayLiteral("EGL_KHR_surfaceless_context")));
    setSupportsNativeFence(hasExtension(QByteArrayLiteral("EGL_ANDROID_native_fence_sync")));
    return true;
}

typedef void (*eglFuncPtr)();
static eglFuncPtr getProcAddress(const char* name)
{
    return eglGetProcAddress(name);
}

void AbstractEglBackend::initKWinGL()
{
    GLPlatform *glPlatform = GLPlatform::instance();
    glPlatform->detect(EglPlatformInterface);
    options->setGlPreferBufferSwap(options->glPreferBufferSwap()); // resolve autosetting
    if (options->glPreferBufferSwap() == Options::AutoSwapStrategy)
        options->setGlPreferBufferSwap('e'); // for unknown drivers - should not happen
    glPlatform->printResults();
    initGL(&getProcAddress);
}

void AbstractEglBackend::initBufferAge()
{
    setSupportsBufferAge(false);

    if (hasExtension(QByteArrayLiteral("EGL_EXT_buffer_age"))) {
        const QByteArray useBufferAge = qgetenv("KWIN_USE_BUFFER_AGE");

        if (useBufferAge != "0") {
            setSupportsBufferAge(true);
        }
    }

    if (hasExtension(QByteArrayLiteral("EGL_KHR_partial_update"))) {
        const QByteArray usePartialUpdate = qgetenv("KWIN_USE_PARTIAL_UPDATE");
        if (usePartialUpdate != "0") {
            setSupportsPartialUpdate(true);
        }
    }
    setSupportsSwapBuffersWithDamage(hasExtension(QByteArrayLiteral("EGL_EXT_swap_buffers_with_damage")));
}

void AbstractEglBackend::initWayland()
{
    if (!WaylandServer::self()) {
        return;
    }
    if (hasExtension(QByteArrayLiteral("EGL_WL_bind_wayland_display"))) {
        m_functions.eglBindWaylandDisplayWL = (eglBindWaylandDisplayWL_func)eglGetProcAddress("eglBindWaylandDisplayWL");
        m_functions.eglUnbindWaylandDisplayWL = (eglUnbindWaylandDisplayWL_func)eglGetProcAddress("eglUnbindWaylandDisplayWL");
        m_functions.eglQueryWaylandBufferWL = (eglQueryWaylandBufferWL_func)eglGetProcAddress("eglQueryWaylandBufferWL");
        // only bind if not already done
        if (waylandServer()->display()->eglDisplay() != eglDisplay()) {
            if (!m_functions.eglBindWaylandDisplayWL(eglDisplay(), *(WaylandServer::self()->display()))) {
                m_functions.eglUnbindWaylandDisplayWL = nullptr;
                m_functions.eglQueryWaylandBufferWL = nullptr;
            } else {
                waylandServer()->display()->setEglDisplay(eglDisplay());
            }
        }
    }

    Q_ASSERT(!m_dmaBuf);
    m_dmaBuf = EglDmabuf::factory(this);
}

void AbstractEglBackend::initClientExtensions()
{
    // Get the list of client extensions
    const char* clientExtensionsCString = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    const QByteArray clientExtensionsString = QByteArray::fromRawData(clientExtensionsCString, qstrlen(clientExtensionsCString));
    if (clientExtensionsString.isEmpty()) {
        // If eglQueryString() returned NULL, the implementation doesn't support
        // EGL_EXT_client_extensions. Expect an EGL_BAD_DISPLAY error.
        (void) eglGetError();
    }

    m_clientExtensions = clientExtensionsString.split(' ');
}

bool AbstractEglBackend::hasClientExtension(const QByteArray &ext) const
{
    return m_clientExtensions.contains(ext);
}

bool AbstractEglBackend::makeCurrent()
{
    if (QOpenGLContext *context = QOpenGLContext::currentContext()) {
        // Workaround to tell Qt that no QOpenGLContext is current
        context->doneCurrent();
    }
    const bool current = eglMakeCurrent(m_display, m_surface, m_surface, m_context);
    return current;
}

void AbstractEglBackend::doneCurrent()
{
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

bool AbstractEglBackend::isOpenGLES() const
{
    return isOpenGLES_helper();
}

bool AbstractEglBackend::createContext()
{
    EGLContext globalShareContext = ensureGlobalShareContext();
    if (globalShareContext == EGL_NO_CONTEXT) {
        return false;
    }

    const bool haveRobustness = hasExtension(QByteArrayLiteral("EGL_EXT_create_context_robustness"));
    const bool haveCreateContext = hasExtension(QByteArrayLiteral("EGL_KHR_create_context"));
    const bool haveContextPriority = hasExtension(QByteArrayLiteral("EGL_IMG_context_priority"));

    std::vector<std::unique_ptr<AbstractOpenGLContextAttributeBuilder>> candidates;
    if (isOpenGLES()) {
        if (haveCreateContext && haveRobustness && haveContextPriority) {
            auto glesRobustPriority = std::make_unique<EglOpenGLESContextAttributeBuilder>();
            glesRobustPriority->setVersion(2);
            glesRobustPriority->setRobust(true);
            glesRobustPriority->setHighPriority(true);
            candidates.push_back(std::move(glesRobustPriority));
        }
        if (haveCreateContext && haveRobustness) {
            auto glesRobust = std::make_unique<EglOpenGLESContextAttributeBuilder>();
            glesRobust->setVersion(2);
            glesRobust->setRobust(true);
            candidates.push_back(std::move(glesRobust));
        }
        if (haveContextPriority) {
            auto glesPriority = std::make_unique<EglOpenGLESContextAttributeBuilder>();
            glesPriority->setVersion(2);
            glesPriority->setHighPriority(true);
            candidates.push_back(std::move(glesPriority));
        }
        auto gles = std::make_unique<EglOpenGLESContextAttributeBuilder>();
        gles->setVersion(2);
        candidates.push_back(std::move(gles));
    } else {
        if (options->glCoreProfile() && haveCreateContext) {
            if (haveRobustness && haveContextPriority) {
                auto robustCorePriority = std::make_unique<EglContextAttributeBuilder>();
                robustCorePriority->setVersion(3, 1);
                robustCorePriority->setRobust(true);
                robustCorePriority->setHighPriority(true);
                candidates.push_back(std::move(robustCorePriority));
            }
            if (haveRobustness) {
                auto robustCore = std::make_unique<EglContextAttributeBuilder>();
                robustCore->setVersion(3, 1);
                robustCore->setRobust(true);
                candidates.push_back(std::move(robustCore));
            }
            if (haveContextPriority) {
                auto corePriority = std::make_unique<EglContextAttributeBuilder>();
                corePriority->setVersion(3, 1);
                corePriority->setHighPriority(true);
                candidates.push_back(std::move(corePriority));
            }
            auto core = std::make_unique<EglContextAttributeBuilder>();
            core->setVersion(3, 1);
            candidates.push_back(std::move(core));
        }
        if (haveRobustness && haveCreateContext && haveContextPriority) {
            auto robustPriority = std::make_unique<EglContextAttributeBuilder>();
            robustPriority->setRobust(true);
            robustPriority->setHighPriority(true);
            candidates.push_back(std::move(robustPriority));
        }
        if (haveRobustness && haveCreateContext) {
            auto robust = std::make_unique<EglContextAttributeBuilder>();
            robust->setRobust(true);
            candidates.push_back(std::move(robust));
        }
        candidates.emplace_back(new EglContextAttributeBuilder);
    }

    EGLContext ctx = EGL_NO_CONTEXT;
    for (auto it = candidates.begin(); it != candidates.end(); it++) {
        const auto attribs = (*it)->build();
        ctx = eglCreateContext(m_display, config(), globalShareContext, attribs.data());
        if (ctx != EGL_NO_CONTEXT) {
            qCDebug(KWIN_OPENGL) << "Created EGL context with attributes:" << (*it).get();
            break;
        }
    }

    if (ctx == EGL_NO_CONTEXT) {
        qCCritical(KWIN_OPENGL) << "Create Context failed";
        return false;
    }
    m_context = ctx;
    if (isPrimary()) {
        kwinApp()->platform()->setSceneEglContext(m_context);
    }
    return true;
}

void AbstractEglBackend::setEglDisplay(const EGLDisplay &display) {
    m_display = display;
    if (isPrimary()) {
        kwinApp()->platform()->setSceneEglDisplay(display);
    }
}

void AbstractEglBackend::setConfig(const EGLConfig &config)
{
    m_config = config;
    if (isPrimary()) {
        kwinApp()->platform()->setSceneEglConfig(config);
    }
}

void AbstractEglBackend::setSurface(const EGLSurface &surface)
{
    m_surface = surface;
}

QSharedPointer<GLTexture> AbstractEglBackend::textureForOutput(AbstractOutput *requestedOutput) const
{
    QSharedPointer<GLTexture> texture(new GLTexture(GL_RGBA8, requestedOutput->pixelSize()));
    GLRenderTarget renderTarget(*texture);

    const QRect geo = requestedOutput->geometry();
    QRect invGeo(geo.left(), geo.bottom(), geo.width(), -geo.height());
    renderTarget.blitFromFramebuffer(invGeo);
    return texture;
}

}
