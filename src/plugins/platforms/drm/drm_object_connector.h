/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QPoint>
#include <QSize>

#include <QSize>

#include "drm_object.h"
#include "edid.h"
#include "drm_pointer.h"
#include "abstract_wayland_output.h"

namespace KWin
{

class DrmConnector : public DrmObject
{
public:
    DrmConnector(DrmGpu *gpu, uint32_t connectorId);
    ~DrmConnector() override;

    bool init() override;

    enum class PropertyIndex : uint32_t {
        CrtcId = 0,
        NonDesktop = 1,
        Dpms = 2,
        Edid = 3,
        Overscan = 4,
        VrrCapable = 5,
        Underscan = 6,
        Underscan_vborder = 7,
        Underscan_hborder = 8,
        Tile = 9,
        Count
    };

    enum class UnderscanOptions : uint32_t {
        Off = 0,
        On = 1,
        Auto = 2,
    };

    QVector<uint32_t> encoders() {
        return m_encoders;
    }

    bool isConnected();

    bool isNonDesktop() const {
        auto prop = m_props.at(static_cast<uint32_t>(PropertyIndex::NonDesktop));
        if (!prop) {
            return false;
        }
        return prop->pending();
    }

    Property *dpms() const {
        return m_props[static_cast<uint32_t>(PropertyIndex::Dpms)];
    }

    const Edid *edid() const {
        return &m_edid;
    }

    QString connectorName() const;
    QString modelName() const;

    bool isInternal() const;
    QSize physicalSize() const;

    struct Mode {
        drmModeModeInfo mode;
        QSize size;
        uint32_t refreshRate;
    };
    const Mode &currentMode() const;
    int modeIndex() const;
    const QVector<Mode> &modes();
    void setModeIndex(int index);
    void findCurrentMode(drmModeModeInfo currentMode);

    AbstractWaylandOutput::SubPixel subpixel() const;

    bool hasOverscan() const;
    uint32_t overscan() const;
    void setOverscan(uint32_t overscan, const QSize &modeSize);

    bool vrrCapable() const;

    bool needsModeset() const override;

    struct TilingInfo {
        int group_id = -1;
        int flags = 0;
        int num_tiles_x = 1;
        int num_tiles_y = 1;
        int loc_x = 0;
        int loc_y = 0;
        int tile_width = 1;
        int tile_height = 1;
    };
    const TilingInfo &tilingInfo() const;
    bool isTiled() const;

    QPoint tilePos() const;
    // includes all other tiles
    QSize totalModeSize(int modeIndex) const;

    void commitPending() override;
    void rollbackPending() override;

private:
    DrmScopedPointer<drmModeConnector> m_conn;
    QVector<uint32_t> m_encoders;
    Edid m_edid;
    QSize m_physicalSize = QSize(-1, -1);
    QVector<Mode> m_modes;
    int m_pendingModeIndex = 0;
    int m_modeIndex = 0;
    TilingInfo m_tilingInfo;
};

}
