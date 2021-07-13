/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.12
import QtQuick.Window 2.12
import org.kde.kwin 2.0 as KWinComponents
import org.kde.plasma.core 2.0 as PlasmaCore

Window {
    id: container

    readonly property int animationDuration: PlasmaCore.Units.longDuration
    readonly property bool kwinSkipOpenAnimation: true
    readonly property bool kwinSkipCloseAnimation: true

    property bool organized: false

    flags: Qt.BypassWindowManagerHint | Qt.FramelessWindowHint
    x: Screen.virtualX
    y: Screen.virtualY
    width: Screen.width
    height: Screen.height
    color: "transparent"
    visible: true

    function stop() {
        container.organized = false;
    }

    Repeater {
        model: KWinComponents.ClientFilterModel {
            screenName: container.screen.name
            clientModel: stackModel
            include: KWinComponents.ClientFilterModel.WindowDesktop
        }

        KWinComponents.ThumbnailItem {
            id: windowThumbnail
            wId: model.client.internalId
            x: model.client.x - Screen.virtualX
            y: model.client.y - Screen.virtualY
            width: model.client.width
            height: model.client.height
        }
    }

    WindowHeap {
        anchors.fill: parent
        model: KWinComponents.ClientFilterModel {
            screenName: container.screen.name
            clientModel: stackModel
            exclude: KWinComponents.ClientFilterModel.WindowDock |
                    KWinComponents.ClientFilterModel.WindowDesktop |
                    KWinComponents.ClientFilterModel.WindowNotification;
        }
    }

    Repeater {
        model: KWinComponents.ClientFilterModel {
            screenName: container.screen.name
            clientModel: stackModel
            include: KWinComponents.ClientFilterModel.WindowDock
        }

        KWinComponents.ThumbnailItem {
            id: windowThumbnail
            wId: model.client.internalId
            x: model.client.x - Screen.virtualX
            y: model.client.y - Screen.virtualY
            width: model.client.width
            height: model.client.height
        }
    }

    KWinComponents.ClientModel {
        id: stackModel
        exclusions: KWinComponents.ClientModel.OtherActivitiesExclusion
    }

    Component.onCompleted: {
        container.organized = true;
        KWin.registerWindow(container);
    }
}
