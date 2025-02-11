/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2019 NVIDIA Inc.

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "egl_stream_backend.h"
#include "basiceglsurfacetexture_internal.h"
#include "composite.h"
#include "drm_backend.h"
#include "drm_output.h"
#include "drm_object_crtc.h"
#include "drm_object_plane.h"
#include "logging.h"
#include "options.h"
#include "renderloop_p.h"
#include "scene.h"
#include "screens.h"
#include "surfaceitem_wayland.h"
#include "wayland_server.h"
#include <kwinglplatform.h>
#include <kwingltexture.h>
#include "drm_gpu.h"
#include "dumb_swapchain.h"
#include "kwineglutils_p.h"
#include "shadowbuffer.h"
#include "drm_pipeline.h"

#include <QOpenGLContext>
#include <KWaylandServer/clientbuffer.h>
#include <KWaylandServer/display.h>
#include <KWaylandServer/eglstream_controller_interface.h>

namespace KWin
{

typedef EGLStreamKHR (*PFNEGLCREATESTREAMATTRIBNV)(EGLDisplay, EGLAttrib *);
typedef EGLBoolean (*PFNEGLGETOUTPUTLAYERSEXT)(EGLDisplay, EGLAttrib *, EGLOutputLayerEXT *, EGLint, EGLint *);
typedef EGLBoolean (*PFNEGLSTREAMCONSUMEROUTPUTEXT)(EGLDisplay, EGLStreamKHR, EGLOutputLayerEXT);
typedef EGLSurface (*PFNEGLCREATESTREAMPRODUCERSURFACEKHR)(EGLDisplay, EGLConfig, EGLStreamKHR, EGLint *);
typedef EGLBoolean (*PFNEGLDESTROYSTREAMKHR)(EGLDisplay, EGLStreamKHR);
typedef EGLBoolean (*PFNEGLSTREAMCONSUMERACQUIREATTRIBNV)(EGLDisplay, EGLStreamKHR, EGLAttrib *);
typedef EGLBoolean (*PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHR)(EGLDisplay, EGLStreamKHR);
typedef EGLBoolean (*PFNEGLQUERYSTREAMATTRIBNV)(EGLDisplay, EGLStreamKHR, EGLenum, EGLAttrib *);
typedef EGLBoolean (*PFNEGLSTREAMCONSUMERRELEASEKHR)(EGLDisplay, EGLStreamKHR);
typedef EGLBoolean (*PFNEGLQUERYWAYLANDBUFFERWL)(EGLDisplay, wl_resource *, EGLint, EGLint *);
PFNEGLCREATESTREAMATTRIBNV pEglCreateStreamAttribNV = nullptr;
PFNEGLGETOUTPUTLAYERSEXT pEglGetOutputLayersEXT = nullptr;
PFNEGLSTREAMCONSUMEROUTPUTEXT pEglStreamConsumerOutputEXT = nullptr;
PFNEGLCREATESTREAMPRODUCERSURFACEKHR pEglCreateStreamProducerSurfaceKHR = nullptr;
PFNEGLDESTROYSTREAMKHR pEglDestroyStreamKHR = nullptr;
PFNEGLSTREAMCONSUMERACQUIREATTRIBNV pEglStreamConsumerAcquireAttribNV = nullptr;
PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHR pEglStreamConsumerGLTextureExternalKHR = nullptr;
PFNEGLQUERYSTREAMATTRIBNV pEglQueryStreamAttribNV = nullptr;
PFNEGLSTREAMCONSUMERRELEASEKHR pEglStreamConsumerReleaseKHR = nullptr;
PFNEGLQUERYWAYLANDBUFFERWL pEglQueryWaylandBufferWL = nullptr;

#ifndef EGL_CONSUMER_AUTO_ACQUIRE_EXT
#define EGL_CONSUMER_AUTO_ACQUIRE_EXT 0x332B
#endif

#ifndef EGL_DRM_MASTER_FD_EXT
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif

#ifndef EGL_DRM_FLIP_EVENT_DATA_NV
#define EGL_DRM_FLIP_EVENT_DATA_NV 0x333E
#endif

#ifndef EGL_WAYLAND_EGLSTREAM_WL
#define EGL_WAYLAND_EGLSTREAM_WL 0x334B
#endif

#ifndef EGL_WAYLAND_Y_INVERTED_WL
#define EGL_WAYLAND_Y_INVERTED_WL 0x31DB
#endif

EglStreamBackend::EglStreamBackend(DrmBackend *drmBackend, DrmGpu *gpu)
    : AbstractEglDrmBackend(drmBackend, gpu)
{
}

EglStreamBackend::~EglStreamBackend()
{
    cleanup();
}

void EglStreamBackend::cleanupSurfaces()
{
    for (auto it = m_outputs.begin(); it != m_outputs.end(); ++it) {
        cleanupOutput(*it);
    }
    m_outputs.clear();
}

void EglStreamBackend::cleanupOutput(Output &o)
{
    if (o.eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(eglDisplay(), o.eglSurface);
    }
    if (o.eglStream != EGL_NO_STREAM_KHR) {
        pEglDestroyStreamKHR(eglDisplay(), o.eglStream);
    }
    o.shadowBuffer = nullptr;
}

bool EglStreamBackend::initializeEgl()
{
    initClientExtensions();
    EGLDisplay display = m_gpu->eglDisplay();
    if (display == EGL_NO_DISPLAY) {
        if (!hasClientExtension(QByteArrayLiteral("EGL_EXT_device_base")) &&
            !(hasClientExtension(QByteArrayLiteral("EGL_EXT_device_query")) &&
              hasClientExtension(QByteArrayLiteral("EGL_EXT_device_enumeration")))) {
            setFailed("Missing required EGL client extension: "
                      "EGL_EXT_device_base or "
                      "EGL_EXT_device_query and EGL_EXT_device_enumeration");
            return false;
        }

        // Try to find the EGLDevice corresponding to our DRM device file
        int numDevices;
        eglQueryDevicesEXT(0, nullptr, &numDevices);
        QVector<EGLDeviceEXT> devices(numDevices);
        eglQueryDevicesEXT(numDevices, devices.data(), &numDevices);
        for (EGLDeviceEXT device : devices) {
            const char *drmDeviceFile = eglQueryDeviceStringEXT(device, EGL_DRM_DEVICE_FILE_EXT);
            if (m_gpu->devNode().compare(drmDeviceFile)) {
                continue;
            }

            const char *deviceExtensionCString = eglQueryDeviceStringEXT(device, EGL_EXTENSIONS);
            QByteArray deviceExtensions = QByteArray::fromRawData(deviceExtensionCString,
                                                                  qstrlen(deviceExtensionCString));
            if (!deviceExtensions.split(' ').contains(QByteArrayLiteral("EGL_EXT_device_drm"))) {
                continue;
            }

            EGLint platformAttribs[] = {
                EGL_DRM_MASTER_FD_EXT, m_gpu->fd(),
                EGL_NONE
            };
            display = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, device, platformAttribs);
            break;
        }
        m_gpu->setEglDisplay(display);
    }

    if (display == EGL_NO_DISPLAY) {
        setFailed("No suitable EGL device found");
        return false;
    }

    setEglDisplay(display);
    if (!initEglAPI()) {
        return false;
    }

    const QVector<QByteArray> requiredExtensions = {
        QByteArrayLiteral("EGL_EXT_output_base"),
        QByteArrayLiteral("EGL_EXT_output_drm"),
        QByteArrayLiteral("EGL_KHR_stream"),
        QByteArrayLiteral("EGL_KHR_stream_producer_eglsurface"),
        QByteArrayLiteral("EGL_EXT_stream_consumer_egloutput"),
        QByteArrayLiteral("EGL_NV_stream_attrib"),
        QByteArrayLiteral("EGL_EXT_stream_acquire_mode"),
        QByteArrayLiteral("EGL_KHR_stream_consumer_gltexture"),
        QByteArrayLiteral("EGL_WL_wayland_eglstream")
    };
    for (const QByteArray &ext : requiredExtensions) {
        if (!hasExtension(ext)) {
            setFailed(QStringLiteral("Missing required EGL extension: ") + ext);
            return false;
        }
    }

    pEglCreateStreamAttribNV = (PFNEGLCREATESTREAMATTRIBNV)eglGetProcAddress("eglCreateStreamAttribNV");
    pEglGetOutputLayersEXT = (PFNEGLGETOUTPUTLAYERSEXT)eglGetProcAddress("eglGetOutputLayersEXT");
    pEglStreamConsumerOutputEXT = (PFNEGLSTREAMCONSUMEROUTPUTEXT)eglGetProcAddress("eglStreamConsumerOutputEXT");
    pEglCreateStreamProducerSurfaceKHR = (PFNEGLCREATESTREAMPRODUCERSURFACEKHR)eglGetProcAddress("eglCreateStreamProducerSurfaceKHR");
    pEglDestroyStreamKHR = (PFNEGLDESTROYSTREAMKHR)eglGetProcAddress("eglDestroyStreamKHR");
    pEglStreamConsumerAcquireAttribNV = (PFNEGLSTREAMCONSUMERACQUIREATTRIBNV)eglGetProcAddress("eglStreamConsumerAcquireAttribNV");
    pEglStreamConsumerGLTextureExternalKHR = (PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHR)eglGetProcAddress("eglStreamConsumerGLTextureExternalKHR");
    pEglQueryStreamAttribNV = (PFNEGLQUERYSTREAMATTRIBNV)eglGetProcAddress("eglQueryStreamAttribNV");
    pEglStreamConsumerReleaseKHR = (PFNEGLSTREAMCONSUMERRELEASEKHR)eglGetProcAddress("eglStreamConsumerReleaseKHR");
    pEglQueryWaylandBufferWL = (PFNEGLQUERYWAYLANDBUFFERWL)eglGetProcAddress("eglQueryWaylandBufferWL");
    return true;
}

EglStreamBackend::StreamTexture *EglStreamBackend::lookupStreamTexture(KWaylandServer::SurfaceInterface *surface)
{
    auto it = m_streamTextures.find(surface);
    return it != m_streamTextures.end() ?
           &it.value() :
           nullptr;
}

void EglStreamBackend::destroyStreamTexture(KWaylandServer::SurfaceInterface *surface)
{
    const StreamTexture &st = m_streamTextures.take(surface);
    pEglDestroyStreamKHR(eglDisplay(), st.stream);
    glDeleteTextures(1, &st.texture);
}

void EglStreamBackend::attachStreamConsumer(KWaylandServer::SurfaceInterface *surface,
                                            void *eglStream,
                                            wl_array *attribs)
{
    makeCurrent();
    QVector<EGLAttrib> streamAttribs;
    streamAttribs << EGL_WAYLAND_EGLSTREAM_WL << (EGLAttrib)eglStream;
    EGLAttrib *attribArray = (EGLAttrib *)attribs->data;
    for (unsigned int i = 0; i < attribs->size; ++i) {
        streamAttribs << attribArray[i];
    }
    streamAttribs << EGL_NONE;

    EGLStreamKHR stream = pEglCreateStreamAttribNV(eglDisplay(), streamAttribs.data());
    if (stream == EGL_NO_STREAM_KHR) {
        qCWarning(KWIN_DRM) << "Failed to create EGL stream:" << getEglErrorString();
        return;
    }

    GLuint texture;
    StreamTexture *st = lookupStreamTexture(surface);
    if (st != nullptr) {
        pEglDestroyStreamKHR(eglDisplay(), st->stream);
        st->stream = stream;
        texture = st->texture;
    } else {
        StreamTexture newSt = { stream, 0 };
        glGenTextures(1, &newSt.texture);
        m_streamTextures.insert(surface, newSt);
        texture = newSt.texture;

        connect(surface, &KWaylandServer::SurfaceInterface::destroyed, this,
            [surface, this]() {
                makeCurrent();
                destroyStreamTexture(surface);
            });
    }

    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
    if (!pEglStreamConsumerGLTextureExternalKHR(eglDisplay(), stream)) {
        qCWarning(KWIN_DRM) << "Failed to bind EGL stream to texture:" << getEglErrorString();
    }
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
}

void EglStreamBackend::init()
{
    if (!m_gpu->atomicModeSetting()) {
        setFailed("EGLStream backend requires atomic modesetting");
        return;
    }

    if (isPrimary()) {
        if (!initializeEgl()) {
            setFailed("Failed to initialize EGL api");
            return;
        }
        if (!initRenderingContext()) {
            setFailed("Failed to initialize rendering context");
            return;
        }

        initKWinGL();
        setSupportsBufferAge(false);
        initWayland();

        using namespace KWaylandServer;
        m_eglStreamControllerInterface = new EglStreamControllerInterface(waylandServer()->display());
        connect(m_eglStreamControllerInterface, &EglStreamControllerInterface::streamConsumerAttached, this,
                &EglStreamBackend::attachStreamConsumer);
    } else {
        // secondary NVidia GPUs only import dumb buffers
        const auto outputs = m_gpu->outputs();
        for (DrmAbstractOutput *drmOutput : outputs) {
            addOutput(drmOutput);
        }
    }
}

bool EglStreamBackend::initRenderingContext()
{
    initBufferConfigs();

    if (!createContext()) {
        return false;
    }

    const auto outputs = m_gpu->outputs();
    for (DrmAbstractOutput *drmOutput : outputs) {
        addOutput(drmOutput);
    }
    return !m_outputs.isEmpty() && makeContextCurrent(m_outputs.first());
}

bool EglStreamBackend::resetOutput(Output &o)
{
    const auto &drmOutput = o.output;
    QSize sourceSize = drmOutput->sourceSize();

    if (isPrimary()) {
        // dumb buffer used for modesetting
        o.buffer = QSharedPointer<DrmDumbBuffer>::create(m_gpu, sourceSize);

        EGLAttrib streamAttribs[] = {
            EGL_STREAM_FIFO_LENGTH_KHR, 0, // mailbox mode
            EGL_CONSUMER_AUTO_ACQUIRE_EXT, EGL_FALSE,
            EGL_NONE
        };
        EGLStreamKHR stream = pEglCreateStreamAttribNV(eglDisplay(), streamAttribs);
        if (stream == EGL_NO_STREAM_KHR) {
            qCCritical(KWIN_DRM) << "Failed to create EGL stream for output:" << getEglErrorString();
            return false;
        }

        EGLAttrib outputAttribs[3];
        if (drmOutput->pipeline()->primaryPlane()) {
            outputAttribs[0] = EGL_DRM_PLANE_EXT;
            outputAttribs[1] = drmOutput->pipeline()->primaryPlane()->id();
        } else {
            outputAttribs[0] = EGL_DRM_CRTC_EXT;
            outputAttribs[1] = drmOutput->pipeline()->crtc()->id();
        }
        outputAttribs[2] = EGL_NONE;
        EGLint numLayers;
        EGLOutputLayerEXT outputLayer;
        pEglGetOutputLayersEXT(eglDisplay(), outputAttribs, &outputLayer, 1, &numLayers);
        if (numLayers == 0) {
            qCCritical(KWIN_DRM) << "No EGL output layers found";
            return false;
        }

        pEglStreamConsumerOutputEXT(eglDisplay(), stream, outputLayer);
        EGLint streamProducerAttribs[] = {
            EGL_WIDTH, sourceSize.width(),
            EGL_HEIGHT, sourceSize.height(),
            EGL_NONE
        };
        EGLSurface eglSurface = pEglCreateStreamProducerSurfaceKHR(eglDisplay(), config(), stream,
                                                                streamProducerAttribs);
        if (eglSurface == EGL_NO_SURFACE) {
            qCCritical(KWIN_DRM) << "Failed to create EGL surface for output:" << getEglErrorString();
            return false;
        }

        if (o.eglSurface != EGL_NO_SURFACE) {
            if (surface() == o.eglSurface) {
                setSurface(eglSurface);
            }
            eglDestroySurface(eglDisplay(), o.eglSurface);
        }

        if (o.eglStream != EGL_NO_STREAM_KHR) {
            pEglDestroyStreamKHR(eglDisplay(), o.eglStream);
        }

        o.eglStream = stream;
        o.eglSurface = eglSurface;

        if (sourceSize != drmOutput->pixelSize()) {
            makeContextCurrent(o);
            o.shadowBuffer = QSharedPointer<ShadowBuffer>::create(o.output->pixelSize());
            if (!o.shadowBuffer->isComplete()) {
                cleanupOutput(o);
                return false;
            }
        }
    } else {
        o.dumbSwapchain = QSharedPointer<DumbSwapchain>::create(m_gpu, sourceSize);
        if (o.dumbSwapchain->isEmpty()) {
            return false;
        }
    }
    return true;
}

bool EglStreamBackend::addOutput(DrmAbstractOutput *output)
{
    Q_ASSERT(output->gpu() == m_gpu);
    DrmOutput *drmOutput = qobject_cast<DrmOutput *>(output);
    if (drmOutput) {
        Output o;
        o.output = drmOutput;
        if (!resetOutput(o)) {
            return false;
        }
        if (!isPrimary() && !renderingBackend()->addOutput(drmOutput)) {
            return false;
        }

        connect(drmOutput, &DrmOutput::modeChanged, this,
            [drmOutput, this] {
                resetOutput(m_outputs[drmOutput]);
            }
        );
        m_outputs.insert(output, o);
        return true;
    } else {
        return false;
    }
}

void EglStreamBackend::removeOutput(DrmAbstractOutput *drmOutput)
{
    Q_ASSERT(drmOutput->gpu() == m_gpu);
    auto it = std::find_if(m_outputs.begin(), m_outputs.end(),
        [drmOutput] (const Output &o) {
            return o.output == drmOutput;
        }
    );
    if (it == m_outputs.end()) {
        return;
    }
    cleanupOutput(*it);
    m_outputs.erase(it);
    if (!isPrimary()) {
        renderingBackend()->removeOutput(drmOutput);
    }
}

bool EglStreamBackend::makeContextCurrent(const Output &output)
{
    const EGLSurface surface = output.eglSurface;
    if (surface == EGL_NO_SURFACE) {
        return false;
    }

    if (eglMakeCurrent(eglDisplay(), surface, surface, context()) == EGL_FALSE) {
        qCCritical(KWIN_DRM) << "Failed to make EGL context current:" << getEglErrorString();
        return false;
    }

    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        qCWarning(KWIN_DRM) << "Error occurred while making EGL context current:" << getEglErrorString(error);
        return false;
    }

    const QSize size = output.output->pixelSize();
    glViewport(0, 0, size.width(), size.height());
    return true;
}

bool EglStreamBackend::initBufferConfigs()
{
    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE,         EGL_STREAM_BIT_KHR,
        EGL_RED_SIZE,             1,
        EGL_GREEN_SIZE,           1,
        EGL_BLUE_SIZE,            1,
        EGL_ALPHA_SIZE,           0,
        EGL_RENDERABLE_TYPE,      isOpenGLES() ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT,
        EGL_CONFIG_CAVEAT,        EGL_NONE,
        EGL_NONE,
    };
    EGLint count;
    EGLConfig config;
    if (!eglChooseConfig(eglDisplay(), configAttribs, &config, 1, &count)) {
        qCCritical(KWIN_DRM) << "Failed to query available EGL configs:" << getEglErrorString();
        return false;
    }
    if (count == 0) {
        qCCritical(KWIN_DRM) << "No suitable EGL config found";
        return false;
    }

    setConfig(config);
    return true;
}

PlatformSurfaceTexture *EglStreamBackend::createPlatformSurfaceTextureInternal(SurfacePixmapInternal *pixmap)
{
    return new BasicEGLSurfaceTextureInternal(this, pixmap);
}

PlatformSurfaceTexture *EglStreamBackend::createPlatformSurfaceTextureWayland(SurfacePixmapWayland *pixmap)
{
    return new EglStreamSurfaceTextureWayland(this, pixmap);
}

QRegion EglStreamBackend::beginFrame(AbstractOutput *drmOutput)
{
    Q_ASSERT(m_outputs.contains(drmOutput));
    const Output &o = m_outputs[drmOutput];
    if (isPrimary()) {
        makeContextCurrent(o);
        if (o.shadowBuffer) {
            o.shadowBuffer->bind();
        }
        return o.output->geometry();
    } else {
        return renderingBackend()->beginFrameForSecondaryGpu(o.output);
    }
}

void EglStreamBackend::endFrame(AbstractOutput *output, const QRegion &renderedRegion, const QRegion &damagedRegion)
{
    Q_ASSERT(m_outputs.contains(output));
    Q_UNUSED(renderedRegion);

    Output &renderOutput = m_outputs[output];
    bool frameFailed = false;

    QSharedPointer<DrmDumbBuffer> buffer;
    if (isPrimary()) {
        buffer = renderOutput.buffer;
        if (renderOutput.shadowBuffer) {
            renderOutput.shadowBuffer->render(renderOutput.output);
        }
        if (!eglSwapBuffers(eglDisplay(), renderOutput.eglSurface)) {
            qCCritical(KWIN_DRM) << "eglSwapBuffers() failed:" << getEglErrorString();
            frameFailed = true;
        }
    } else {
        if (!renderingBackend()->swapBuffers(static_cast<DrmOutput*>(output), damagedRegion.intersected(output->geometry()))) {
            qCCritical(KWIN_DRM) << "swapping buffers on render backend for" << output << "failed!";
            frameFailed = true;
        }
        buffer = renderOutput.dumbSwapchain->acquireBuffer();
        if (!frameFailed && !renderingBackend()->exportFramebuffer(static_cast<DrmOutput*>(output), buffer->data(), buffer->size(), buffer->stride())) {
            qCCritical(KWIN_DRM) << "importing framebuffer from render backend for" << output << "failed!";
            frameFailed = true;
        }
    }
    if (!frameFailed && !renderOutput.output->present(buffer, damagedRegion)) {
        frameFailed = true;
    }

    if (frameFailed) {
        RenderLoopPrivate *renderLoopPrivate = RenderLoopPrivate::get(output->renderLoop());
        renderLoopPrivate->notifyFrameFailed();
    } else if (isPrimary()) {
        EGLAttrib acquireAttribs[] = {
            EGL_DRM_FLIP_EVENT_DATA_NV, (EGLAttrib)output,
            EGL_NONE,
        };
        if (!pEglStreamConsumerAcquireAttribNV(eglDisplay(), renderOutput.eglStream, acquireAttribs)) {
            qCWarning(KWIN_DRM) << "Failed to acquire output EGL stream frame:" << getEglErrorString();
        }
    }
}

QSharedPointer<DrmBuffer> EglStreamBackend::renderTestFrame(DrmAbstractOutput *drmOutput)
{
    Q_ASSERT(m_outputs.contains(drmOutput));
    auto &output = m_outputs[drmOutput];
    auto buffer = output.dumbSwapchain ? output.dumbSwapchain->currentBuffer() : output.buffer;
    auto size = drmOutput->sourceSize();
    if (buffer->size() == size) {
        return buffer;
    } else {
        return QSharedPointer<DrmDumbBuffer>::create(m_gpu, size);
    }
}

bool EglStreamBackend::hasOutput(AbstractOutput *output) const
{
    return m_outputs.contains(output);
}

/************************************************
 * EglTexture
 ************************************************/

EglStreamSurfaceTextureWayland::EglStreamSurfaceTextureWayland(EglStreamBackend *backend,
                                                               SurfacePixmapWayland *pixmap)
    : BasicEGLSurfaceTextureWayland(backend, pixmap)
    , m_backend(backend)
{
}

EglStreamSurfaceTextureWayland::~EglStreamSurfaceTextureWayland()
{
    glDeleteRenderbuffers(1, &m_rbo);
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_textureId);
}

bool EglStreamSurfaceTextureWayland::acquireStreamFrame(EGLStreamKHR stream)
{
    EGLAttrib streamState;
    if (!pEglQueryStreamAttribNV(m_backend->eglDisplay(), stream,
                                 EGL_STREAM_STATE_KHR, &streamState)) {
        qCWarning(KWIN_DRM) << "Failed to query EGL stream state:" << getEglErrorString();
        return false;
    }

    if (streamState == EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR) {
        if (pEglStreamConsumerAcquireAttribNV(m_backend->eglDisplay(), stream, nullptr)) {
            return true;
        } else {
            qCWarning(KWIN_DRM) << "Failed to acquire EGL stream frame:" << getEglErrorString();
        }
    }

    // Re-use previous texture contents if no new frame is available
    // or if acquisition fails for some reason
    return false;
}

void EglStreamSurfaceTextureWayland::createFbo()
{
    glDeleteRenderbuffers(1, &m_rbo);
    glDeleteFramebuffers(1, &m_fbo);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glGenRenderbuffers(1, &m_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, m_format, m_texture->width(), m_texture->height());
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Renders the contents of the given EXTERNAL_OES texture
// to the scratch framebuffer, then copies this to m_texture
void EglStreamSurfaceTextureWayland::copyExternalTexture(GLuint tex)
{
    GLint oldViewport[4], oldProgram;
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glViewport(0, 0, m_texture->width(), m_texture->height());
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
    glEnable(GL_TEXTURE_EXTERNAL_OES);

    GLfloat yTop = texture()->isYInverted() ? 0 : 1;
    glBegin(GL_QUADS);
    glTexCoord2f(0, yTop);
    glVertex2f(-1, 1);
    glTexCoord2f(0, 1 - yTop);
    glVertex2f(-1, -1);
    glTexCoord2f(1, 1 - yTop);
    glVertex2f(1, -1);
    glTexCoord2f(1, yTop);
    glVertex2f(1, 1);
    glEnd();

    texture()->bind();
    glCopyTexImage2D(m_texture->target(), 0, m_format, 0, 0, m_texture->width(), m_texture->height(), 0);
    texture()->unbind();

    glDisable(GL_TEXTURE_EXTERNAL_OES);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(oldProgram);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
}

bool EglStreamSurfaceTextureWayland::attachBuffer(KWaylandServer::ClientBuffer *buffer)
{
    GLenum oldFormat = m_format;
    m_format = buffer->hasAlphaChannel() ? GL_RGBA : GL_RGB;

    EGLint yInverted, wasYInverted = texture()->isYInverted();
    if (!pEglQueryWaylandBufferWL(m_backend->eglDisplay(), buffer->resource(), EGL_WAYLAND_Y_INVERTED_WL, &yInverted)) {
        yInverted = EGL_TRUE;
    }
    texture()->setYInverted(yInverted);

    return oldFormat != m_format || wasYInverted != texture()->isYInverted();
}

bool EglStreamSurfaceTextureWayland::checkBuffer(KWaylandServer::SurfaceInterface *surface,
                                                 KWaylandServer::ClientBuffer *buffer)
{
    EGLAttrib attribs[] = {
        EGL_WAYLAND_EGLSTREAM_WL, (EGLAttrib)buffer->resource(),
        EGL_NONE
    };
    EGLStreamKHR stream = pEglCreateStreamAttribNV(m_backend->eglDisplay(), attribs);
    if (stream == EGL_NO_STREAM_KHR) {
        // eglCreateStreamAttribNV generates EGL_BAD_ACCESS if the
        // provided buffer is not a wl_eglstream. In that case, clean up
        // the old stream and fall back to the dmabuf or shm attach
        // paths.
        EGLint err = eglGetError();
        if (err == EGL_BAD_ACCESS) {
            m_backend->destroyStreamTexture(surface);
            return false;
        }
        // Otherwise it should have generated EGL_BAD_STREAM_KHR since
        // we've already created an EGLStream for it.
        Q_ASSERT(err == EGL_BAD_STREAM_KHR);
    } else {
        // If eglCreateStreamAttribNV *didn't* fail, that means the
        // buffer is a wl_eglstream but it hasn't been attached to a
        // consumer for some reason. Not much we can do here.
        qCCritical(KWIN_DRM) << "Untracked wl_eglstream attached to surface";
        pEglDestroyStreamKHR(m_backend->eglDisplay(), stream);
    }
    return true;
}

bool EglStreamSurfaceTextureWayland::create()
{
    using namespace KWaylandServer;
    SurfaceInterface *surface = m_pixmap->surface();
    const EglStreamBackend::StreamTexture *st = m_backend->lookupStreamTexture(surface);
    if (m_pixmap->buffer() && st != nullptr && checkBuffer(surface, m_pixmap->buffer())) {

        glGenTextures(1, &m_textureId);
        m_texture.reset(new GLTexture(m_textureId, 0, m_pixmap->buffer()->size()));
        m_texture->setWrapMode(GL_CLAMP_TO_EDGE);
        m_texture->setFilter(GL_LINEAR);

        attachBuffer(surface->buffer());
        createFbo();

        if (acquireStreamFrame(st->stream)) {
            copyExternalTexture(st->texture);
            if (!pEglStreamConsumerReleaseKHR(m_backend->eglDisplay(), st->stream)) {
                qCWarning(KWIN_DRM) << "Failed to release EGL stream:" << getEglErrorString();
            }
        }
        return true;
    } else {
        // Not an EGLStream surface
        return BasicEGLSurfaceTextureWayland::create();
    }
}

void EglStreamSurfaceTextureWayland::update(const QRegion &region)
{
    using namespace KWaylandServer;
    SurfaceInterface *surface = m_pixmap->surface();
    const EglStreamBackend::StreamTexture *st = m_backend->lookupStreamTexture(surface);
    if (m_pixmap->buffer() && st != nullptr && checkBuffer(surface, m_pixmap->buffer())) {

        if (attachBuffer(surface->buffer())) {
            createFbo();
        }

        if (acquireStreamFrame(st->stream)) {
            copyExternalTexture(st->texture);
            if (!pEglStreamConsumerReleaseKHR(m_backend->eglDisplay(), st->stream)) {
                qCWarning(KWIN_DRM) << "Failed to release EGL stream:" << getEglErrorString();
            }
        }
    } else {
        // Not an EGLStream surface
        BasicEGLSurfaceTextureWayland::update(region);
    }
}

} // namespace
