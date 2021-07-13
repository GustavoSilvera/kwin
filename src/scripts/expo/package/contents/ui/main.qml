/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.12
import QtQuick.Window 2.12
import org.kde.kwin 2.0 as KWinComponents
import org.kde.plasma.core 2.0 as PlasmaCore

Item {
    id: root

    property int cfg_LayoutMode: 2

    property bool activated: false

    function toggleActivated() {
        if (!root.activated) {
            activate();
        } else {
            deactivate();
        }
    }

    function activate() {
        root.activated = true;
        expoLoader.active = true;
    }

    function deactivate() {
        root.activated = false;
        if (expoLoader.item) {
            shutdownTimer.start();
            expoLoader.item.stop();
        }
    }

    function loadConfig() {
        cfg_LayoutMode = KWin.readConfig("LayoutMode", 2);
    }

    Loader {
        id: expoLoader
        active: false
        sourceComponent: Expo {}
    }

    Timer {
        id: shutdownTimer
        interval: PlasmaCore.Units.longDuration
        running: false
        repeat: false
        onTriggered: expoLoader.active = false
    }

    KWinComponents.ScreenEdgeItem {
        edge: KWinComponents.ScreenEdgeItem.TopLeftEdge
        mode: KWinComponents.ScreenEdgeItem.Pointer
        onActivated: toggleActivated()
    }

    KWinComponents.ScreenEdgeItem {
        edge: KWinComponents.ScreenEdgeItem.TopLeftEdge
        mode: KWinComponents.ScreenEdgeItem.Touch
        onActivated: toggleActivated()
    }

    Connections {
        target: options
        function onConfigChanged() { root.loadConfig(); }
    }

    Component.onCompleted: {
        loadConfig();
        KWin.registerShortcut("Expo", "Toggle Expo", "Ctrl+Meta+D", () => toggleActivated());
    }
}
