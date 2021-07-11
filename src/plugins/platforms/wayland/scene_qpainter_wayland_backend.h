/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2013, 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_SCENE_QPAINTER_WAYLAND_BACKEND_H
#define KWIN_SCENE_QPAINTER_WAYLAND_BACKEND_H

#include "qpainterbackend.h"
#include "qpainterframeprofiler.h"

#include <QObject>
#include <QImage>
#include <QWeakPointer>

namespace KWayland
{
namespace Client
{
class ShmPool;
class Buffer;
}
}

namespace KWin
{
class AbstractOutput;
namespace Wayland
{
class WaylandBackend;
class WaylandOutput;
class WaylandQPainterBackend;

class WaylandQPainterOutput : public QObject
{
    Q_OBJECT
public:
    WaylandQPainterOutput(WaylandOutput *output, QObject *parent = nullptr);
    ~WaylandQPainterOutput() override;

    bool init(KWayland::Client::ShmPool *pool);
    void updateSize(const QSize &size);
    void remapBuffer();

    void prepareRenderingFrame();
    void present(const QRegion &damage);

    bool needsFullRepaint() const;
    void setNeedsFullRepaint(bool set);

    WaylandOutput *platformOutput() const;
    QRegion mapToLocal(const QRegion &region) const;
    QPainterFrameProfiler *profiler() const;

private:
    WaylandOutput *m_waylandOutput;
    KWayland::Client::ShmPool *m_pool;

    QWeakPointer<KWayland::Client::Buffer> m_buffer;
    QImage m_backBuffer;
    QScopedPointer<QPainterFrameProfiler> m_profiler;
    bool m_needsFullRepaint = true;

    friend class WaylandQPainterBackend;
};

class WaylandQPainterBackend : public QPainterBackend
{
    Q_OBJECT
public:
    explicit WaylandQPainterBackend(WaylandBackend *b);
    ~WaylandQPainterBackend() override;

    QImage *bufferForScreen(int screenId) override;

    void endFrame(int screenId, int mask, const QRegion& damage) override;
    void beginFrame(int screenId) override;

    bool needsFullRepaint(int screenId) const override;
    std::chrono::nanoseconds renderTime(AbstractOutput *output) override;

private:
    void createOutput(AbstractOutput *waylandOutput);
    void frameRendered();

    WaylandBackend *m_backend;
    QVector<WaylandQPainterOutput*> m_outputs;
};

}
}

#endif
