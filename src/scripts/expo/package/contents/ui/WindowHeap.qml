/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.12
import QtQuick.Window 2.12
import org.kde.kwin 2.0 as KWinComponents
import org.kde.kwin.scripts.expo 1.0
import org.kde.plasma.components 3.0 as Plasma
import org.kde.plasma.core 2.0 as PlasmaCore

FocusScope {
    id: heap
    focus: true

    enum Direction {
        Left,
        Right,
        Up,
        Down
    }

    property alias model: windowsRepeater.model
    property int selectedIndex: -1

    ExpoLayout {
        id: expoLayout
        anchors.fill: parent
        mode: root.cfg_LayoutMode
        focus: true

        Repeater {
            id: windowsRepeater

            KWinComponents.ThumbnailItem {
                id: windowThumbnail
                wId: model.client.internalId
                state: {
                    if (container.organized) {
                        return "active";
                    }
                    return model.client.minimized ? "initial-minimized" : "initial";
                }
                z: model.client.stackingOrder

                readonly property bool selected: heap.selectedIndex == index

                ExpoCell {
                    id: cell
                    layout: expoLayout
                    naturalX: model.client.x - Screen.virtualX
                    naturalY: model.client.y - Screen.virtualY
                    naturalWidth: model.client.width
                    naturalHeight: model.client.height
                    persistentKey: model.client.internalId
                }

                states: [
                    State {
                        name: "initial"
                        PropertyChanges {
                            target: windowThumbnail
                            x: model.client.x - Screen.virtualX
                            y: model.client.y - Screen.virtualY
                            width: model.client.width
                            height: model.client.height
                        }
                    },
                    State {
                        name: "initial-minimized"
                        extend: "initial"
                        PropertyChanges {
                            target: windowThumbnail
                            opacity: 0
                        }
                    },
                    State {
                        name: "active"
                        PropertyChanges {
                            target: windowThumbnail
                            x: cell.x
                            y: cell.y
                            width: cell.width
                            height: cell.height
                        }
                    }
                ]

                Behavior on x {
                    NumberAnimation { duration: container.animationDuration; easing.type: Easing.InOutCubic; }
                }
                Behavior on y {
                    NumberAnimation { duration: container.animationDuration; easing.type: Easing.InOutCubic; }
                }
                Behavior on width {
                    NumberAnimation { duration: container.animationDuration; easing.type: Easing.InOutCubic; }
                }
                Behavior on height {
                    NumberAnimation { duration: container.animationDuration; easing.type: Easing.InOutCubic; }
                }
                Behavior on opacity {
                    NumberAnimation { duration: container.animationDuration; easing.type: Easing.InOutCubic; }
                }

                PlasmaCore.FrameSvgItem {
                    anchors.fill: parent
                    anchors.margins: -PlasmaCore.Units.smallSpacing
                    imagePath: "widgets/viewitem"
                    prefix: "hover"
                    z: -1
                    visible: mouseArea.containsMouse || selected
                }

                MouseArea {
                    id: mouseArea
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    anchors.fill: parent
                    hoverEnabled: true

                    onEntered: {
                        if (!selected) {
                            heap.selectedIndex = -1;
                        }
                    }
                    onExited: {
                        if (selected) {
                            heap.selectedIndex = -1;
                        }
                    }
                    onClicked: {
                        switch (mouse.button) {
                        case Qt.LeftButton:
                            workspace.activeClient = model.client;
                            root.deactivate();
                            break;
                        case Qt.MiddleButton:
                            model.client.closeWindow();
                            break;
                        }
                    }
                }

                Plasma.Button {
                    icon.name: "window-close"
                    anchors.right: windowThumbnail.right
                    anchors.rightMargin: PlasmaCore.Units.largeSpacing
                    anchors.top: windowThumbnail.top
                    anchors.topMargin: PlasmaCore.Units.largeSpacing
                    implicitWidth: PlasmaCore.Units.iconSizes.medium
                    implicitHeight: implicitWidth
                    visible: hovered || mouseArea.containsMouse
                    onClicked: model.client.closeWindow();
                }

                Component.onDestruction: {
                    if (selected) {
                        heap.selectedIndex = -1;
                    }
                }
            }
        }

        Keys.onPressed: {
            switch (event.key) {
            case Qt.Key_Escape:
                root.deactivate();
                break;
            case Qt.Key_Up:
                selectNextItem(WindowHeap.Direction.Up);
                break;
            case Qt.Key_Down:
                selectNextItem(WindowHeap.Direction.Down);
                break;
            case Qt.Key_Left:
                selectNextItem(WindowHeap.Direction.Left);
                break;
            case Qt.Key_Right:
                selectNextItem(WindowHeap.Direction.Right);
                break;
            case Qt.Key_Home:
                selectLastItem(WindowHeap.Direction.Left);
                break;
            case Qt.Key_End:
                selectLastItem(WindowHeap.Direction.Right);
                break;
            case Qt.Key_PageUp:
                selectLastItem(WindowHeap.Direction.Up);
                break;
            case Qt.Key_PageDown:
                selectLastItem(WindowHeap.Direction.Down);
                break;
            case Qt.Key_Return:
            case Qt.Key_Escape:
                if (heap.selectedIndex != -1) {
                    const selectedItem = windowsRepeater.itemAt(heap.selectedIndex);
                    workspace.activeClient = selectedItem.client;
                    root.deactivate();
                }
                break;
            default:
                return;
            }
            event.accepted = true;
        }

        onActiveFocusChanged: {
            heap.selectedIndex = -1;
        }
    }

    function findNextItem(selectedIndex, direction) {
        if (windowsRepeater.count == 0) {
            return -1;
        } else if (selectedIndex == -1) {
            return 0;
        }

        const selectedItem = windowsRepeater.itemAt(selectedIndex);
        let nextIndex = -1;

        switch (direction) {
        case WindowHeap.Direction.Left:
            for (let candidateIndex = 0; candidateIndex < windowsRepeater.count; ++candidateIndex) {
                const candidateItem = windowsRepeater.itemAt(candidateIndex);

                if (candidateItem.y + candidateItem.height <= selectedItem.y) {
                    continue;
                } else if (selectedItem.y + selectedItem.height <= candidateItem.y) {
                    continue;
                }

                if (candidateItem.x + candidateItem.width < selectedItem.x + selectedItem.width) {
                    if (nextIndex == -1) {
                        nextIndex = candidateIndex;
                    } else {
                        const nextItem = windowsRepeater.itemAt(nextIndex);
                        if (candidateItem.x + candidateItem.width > nextItem.x + nextItem.width) {
                            nextIndex = candidateIndex;
                        }
                    }
                }
            }
            break;
        case WindowHeap.Direction.Right:
            for (let candidateIndex = 0; candidateIndex < windowsRepeater.count; ++candidateIndex) {
                const candidateItem = windowsRepeater.itemAt(candidateIndex);

                if (candidateItem.y + candidateItem.height <= selectedItem.y) {
                    continue;
                } else if (selectedItem.y + selectedItem.height <= candidateItem.y) {
                    continue;
                }

                if (selectedItem.x < candidateItem.x) {
                    if (nextIndex == -1) {
                        nextIndex = candidateIndex;
                    } else {
                        const nextItem = windowsRepeater.itemAt(nextIndex);
                        console.log(candidateItem.x, nextItem.x)
                        if (nextIndex == -1 || candidateItem.x < nextItem.x) {
                            nextIndex = candidateIndex;
                        }
                    }
                }
            }
            break;
        case WindowHeap.Direction.Up:
            for (let candidateIndex = 0; candidateIndex < windowsRepeater.count; ++candidateIndex) {
                const candidateItem = windowsRepeater.itemAt(candidateIndex);

                if (candidateItem.x + candidateItem.width <= selectedItem.x) {
                    continue;
                } else if (selectedItem.x + selectedItem.width <= candidateItem.x) {
                    continue;
                }

                if (candidateItem.y + candidateItem.height < selectedItem.y + selectedItem.height) {
                    if (nextIndex == -1) {
                        nextIndex = candidateIndex;
                    } else {
                        const nextItem = windowsRepeater.itemAt(nextIndex);
                        if (nextItem.y + nextItem.height < candidateItem.y + candidateItem.height) {
                            nextIndex = candidateIndex;
                        }
                    }
                }
            }
            break;
        case WindowHeap.Direction.Down:
            for (let candidateIndex = 0; candidateIndex < windowsRepeater.count; ++candidateIndex) {
                const candidateItem = windowsRepeater.itemAt(candidateIndex);

                if (candidateItem.x + candidateItem.width <= selectedItem.x) {
                    continue;
                } else if (selectedItem.x + selectedItem.width <= candidateItem.x) {
                    continue;
                }

                if (selectedItem.y < candidateItem.y) {
                    if (nextIndex == -1) {
                        nextIndex = candidateIndex;
                    } else {
                        const nextItem = windowsRepeater.itemAt(nextIndex);
                        if (candidateItem.y < nextItem.y) {
                            nextIndex = candidateIndex;
                        }
                    }
                }
            }
            break;
        }

        return nextIndex;
    }

    function selectNextItem(direction) {
        const nextIndex = findNextItem(heap.selectedIndex, direction);
        if (nextIndex != -1) {
            heap.selectedIndex = nextIndex;
        }
    }

    function selectLastItem(direction) {
        let last = heap.selectedIndex;
        while (true) {
            const next = findNextItem(last, direction);
            if (next == -1) {
                break;
            } else {
                last = next;
            }
        }
        if (last != -1) {
            heap.selectedIndex = last;
        }
    }
}
