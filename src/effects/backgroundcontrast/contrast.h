/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2014 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CONTRAST_H
#define CONTRAST_H

#include <kwineffects.h>
#include <kwinglplatform.h>
#include <kwinglutils.h>

#include <QVector>
#include <QVector2D>

#include <KWaylandServer/contrast_interface.h>
#include <KWaylandServer/utils.h>

namespace KWin
{

class ContrastShader;

class ContrastEffect : public KWin::Effect
{
    Q_OBJECT
public:
    ContrastEffect();
    ~ContrastEffect() override;

    static bool supported();
    static bool enabledByDefault();

    static QMatrix4x4 colorMatrix(qreal contrast, qreal intensity, qreal saturation);
    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void drawWindow(EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data) override;
    void paintEffectFrame(EffectFrame *frame, const QRegion &region, double opacity, double frameOpacity) override;

    bool provides(Feature feature) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override {
        return 76;
    }

    bool eventFilter(QObject *watched, QEvent *event) override;

    bool blocksDirectScanout() const override;

public Q_SLOTS:
    void slotWindowAdded(KWin::EffectWindow *w);
    void slotWindowDeleted(KWin::EffectWindow *w);
    void slotPropertyNotify(KWin::EffectWindow *w, long atom);
    void slotScreenGeometryChanged();

private:
    QRegion contrastRegion(const EffectWindow *w) const;
    bool shouldContrast(const EffectWindow *w, int mask, const WindowPaintData &data) const;
    std::optional<QRegion> updateContrastRegionWayland(EffectWindow *w);
    std::optional<QRegion> updateContrastRegionX11(EffectWindow *w);
    std::optional<QRegion> updateContrastRegionInternal(EffectWindow *w);
    void updateContrastRegion(EffectWindow *w);
    void doContrast(EffectWindow *w, const QRegion &shape, const QRect &screen, const float opacity, const QMatrix4x4 &screenProjection);
    void uploadRegion(QVector2D *&map, const QRegion &region);
    void uploadGeometry(GLVertexBuffer *vbo, const QRegion &region);
    void prepareAtoms();

private:
    ContrastShader *shader;
    long net_wm_contrast_region;
    long net_wm_frost_region;
    QRegion m_paintedArea; // actually painted area which is greater than m_damagedArea
    QRegion m_currentContrast; // keeps track of the currently contrasted area of non-caching windows(from bottom to top)
    QHash< const EffectWindow*, std::variant<QMatrix4x4,QColor>> m_contrastData;
    QHash< const EffectWindow*, QMetaObject::Connection > m_contrastChangedConnections; // used only in Wayland to keep track of effect changed
    KWaylandServer::ScopedGlobalPointer<KWaylandServer::ContrastManagerInterface> m_contrastManager;
};

inline
bool ContrastEffect::provides(Effect::Feature feature)
{
    if (feature == Contrast) {
        return true;
    }
    return KWin::Effect::provides(feature);
}


} // namespace KWin

#endif

