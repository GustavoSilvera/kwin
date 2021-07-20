/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "item.h"

namespace KWin
{

class SurfacePixmap;

/**
 * The SurfaceItem class represents a surface with some contents.
 */
class KWIN_EXPORT SurfaceItem : public Item
{
    Q_OBJECT

public:
    virtual QPointF mapToBuffer(const QPointF &point) const = 0;

    virtual QRegion shape() const;
    virtual QRegion opaque() const;

    void addDamage(const QRegion &region);
    void resetDamage();
    QRegion damage() const;

    void discardPixmap();
    void updatePixmap();

    SurfacePixmap *pixmap() const;
    SurfacePixmap *previousPixmap() const;

    void referencePreviousPixmap();
    void unreferencePreviousPixmap();

protected:
    explicit SurfaceItem(Scene::Window *window, Item *parent = nullptr);

    virtual SurfacePixmap *createPixmap() = 0;
    void preprocess() override;
    WindowQuadList buildQuads() const override;

    QRegion m_damage;
    QScopedPointer<SurfacePixmap> m_pixmap;
    QScopedPointer<SurfacePixmap> m_previousPixmap;
    int m_referencePixmapCounter = 0;

    friend class Scene::Window;
};

class KWIN_EXPORT PlatformSurfaceTexture
{
public:
    virtual ~PlatformSurfaceTexture();

    virtual bool isValid() const = 0;

    virtual bool create() = 0;
    virtual void update(const QRegion &region) = 0;
};

class KWIN_EXPORT SurfacePixmap : public QObject
{
    Q_OBJECT

public:
    explicit SurfacePixmap(PlatformSurfaceTexture *platformTexture, QObject *parent = nullptr);

    PlatformSurfaceTexture *platformTexture() const;

    bool hasAlphaChannel() const;
    QSize size() const;

    virtual void create() = 0;
    virtual void update();

    virtual bool isValid() const = 0;

protected:
    QSize m_size;
    bool m_hasAlphaChannel = false;

private:
    QScopedPointer<PlatformSurfaceTexture> m_platformTexture;
};

} // namespace KWin
