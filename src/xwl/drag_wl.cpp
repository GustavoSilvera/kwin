/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drag_wl.h"

#include "databridge.h"
#include "dnd.h"
#include "xwayland.h"

#include "atoms.h"
#include "x11client.h"
#include "wayland_server.h"
#include "workspace.h"

#include <KWaylandServer/datasource_interface.h>
#include <KWaylandServer/datadevice_interface.h>
#include <KWaylandServer/seat_interface.h>
#include <KWaylandServer/surface_interface.h>

#include <QMouseEvent>
#include <QTimer>

namespace KWin
{
namespace Xwl
{

using DnDAction = KWaylandServer::DataDeviceManagerInterface::DnDAction;
using DnDActions = KWaylandServer::DataDeviceManagerInterface::DnDActions;

static DnDAction atomToClientAction(xcb_atom_t atom)
{
    if (atom == atoms->xdnd_action_copy) {
        return DnDAction::Copy;
    } else if (atom == atoms->xdnd_action_move) {
        return DnDAction::Move;
    } else if (atom == atoms->xdnd_action_ask) {
        // we currently do not support it - need some test client first
        return DnDAction::None;
//        return DnDAction::Ask;
    }
    return DnDAction::None;
}

static xcb_atom_t clientActionToAtom(DnDAction action)
{
    if (action == DnDAction::Copy) {
        return atoms->xdnd_action_copy;
    } else if (action == DnDAction::Move) {
        return atoms->xdnd_action_move;
    } else if (action == DnDAction::Ask) {
        // we currently do not support it - need some test client first
        return XCB_ATOM_NONE;
//        return atoms->xdnd_action_ask;
    }
    return XCB_ATOM_NONE;
}

WlToXDrag::WlToXDrag()
{
    m_dsi = waylandServer()->seat()->dragSource()->dragSource();
}

DragEventReply WlToXDrag::moveFilter(Toplevel *target, const QPoint &pos)
{
    AbstractClient *ac = qobject_cast<AbstractClient *>(target);
    auto *seat = waylandServer()->seat();
    if (m_visit && m_visit->target() == ac) {
        // no target change
        return DragEventReply::Take;
    }
    // leave current target
    if (m_visit) {
        seat->setDragTarget(nullptr);
        m_visit->leave();
        delete m_visit;
        m_visit = nullptr;
    }
    if (!qobject_cast<X11Client *>(ac)) {
        // no target or wayland native target,
        // handled by input code directly
        return DragEventReply::Wayland;
    }
    // new target
    seat->setDragTarget(DataBridge::self()->dnd()->surfaceIface(), pos, ac->inputTransformation());
    m_visit = new Xvisit(this, ac);
    return DragEventReply::Take;
}

bool WlToXDrag::handleClientMessage(xcb_client_message_event_t *event)
{
    if (m_visit && m_visit->handleClientMessage(event)) {
        return true;
    }
    return false;
}

bool WlToXDrag::end()
{
    if (!m_visit || m_visit->finished()) {
        delete m_visit;
        m_visit = nullptr;
        return true;
    }
    connect(m_visit, &Xvisit::finish, this, [this](Xvisit *visit) {
        Q_ASSERT(m_visit == visit);
        delete visit;
        m_visit = nullptr;
        // we direclty allow to delete previous visits
        Q_EMIT finish(this);
    });
    m_visit->leave();
    return false;
}

KWaylandServer::DataSourceInterface *WlToXDrag::dataSourceIface() const
{
    return m_dsi.data();
}

Xvisit::Xvisit(WlToXDrag *drag, AbstractClient *target)
    : QObject(drag),
      m_drag(drag),
      m_target(target)
{
    // first check supported DND version
    xcb_connection_t *xcbConn = kwinApp()->x11Connection();
    xcb_get_property_cookie_t cookie = xcb_get_property(xcbConn,
                                                        0,
                                                        m_target->window(),
                                                        atoms->xdnd_aware,
                                                        XCB_GET_PROPERTY_TYPE_ANY,
                                                        0, 1);
    auto *reply = xcb_get_property_reply(xcbConn, cookie, nullptr);
    if (!reply) {
        doFinish();
        return;
    }
    if (reply->type != XCB_ATOM_ATOM) {
        doFinish();
        free(reply);
        return;
    }
    xcb_atom_t *value = static_cast<xcb_atom_t *>(xcb_get_property_value(reply));
    m_version = qMin(*value, Dnd::version());
    if (m_version < 1) {
        // minimal version we accept is 1
        doFinish();
        free(reply);
        return;
    }
    free(reply);

    m_dropConnection = connect(waylandServer()->seat(), &KWaylandServer::SeatInterface::dragDropped, this, &Xvisit::drop);
    receiveOffer();
}

bool Xvisit::handleClientMessage(xcb_client_message_event_t *event)
{
    if (event->type == atoms->xdnd_status) {
        return handleStatus(event);
    } else if (event->type == atoms->xdnd_finished) {
        return handleFinished(event);
    }
    return false;
}

bool Xvisit::handleStatus(xcb_client_message_event_t *event)
{
    xcb_client_message_data_t *data = &event->data;
    if (data->data32[0] != m_target->window()) {
        // wrong target window
        return false;
    }

    m_accepts = data->data32[1] & 1;
    xcb_atom_t actionAtom = data->data32[4];

    auto dataSource = m_drag->dataSourceIface();
    if (dataSource && !dataSource->mimeTypes().isEmpty()) {
        dataSource->accept(m_accepts ? dataSource->mimeTypes().constFirst() : QString());
    }
    // TODO: we could optimize via rectangle in data32[2] and data32[3]

    // position round trip finished
    m_pos.pending = false;

    if (!m_state.dropped) {
        // as long as the drop is not yet done determine requested action
        m_preferredAction = atomToClientAction(actionAtom);
        determineProposedAction();
        requestDragAndDropAction();
    }

    if (m_pos.cached) {
        // send cached position
        m_pos.cached = false;
        sendPosition(m_pos.cache);
    } else if (m_state.dropped) {
        // drop was done in between, now close it out
        drop();
    }
    return true;
}

bool Xvisit::handleFinished(xcb_client_message_event_t *event)
{
    xcb_client_message_data_t *data = &event->data;

    if (data->data32[0] != m_target->window()) {
        // different target window
        return false;
    }

    if (!m_state.dropped) {
        // drop was never done
        doFinish();
        return true;
    }

    const bool success = m_version > 4 ? data->data32[1] & 1 : true;
    const xcb_atom_t usedActionAtom = m_version > 4 ? data->data32[2] :
                                                      static_cast<uint32_t>(XCB_ATOM_NONE);
    Q_UNUSED(success);
    Q_UNUSED(usedActionAtom);

    if (auto dataSource = m_drag->dataSourceIface())  {
        dataSource->dndFinished();
    }
    doFinish();
    return true;
}

void Xvisit::sendPosition(const QPointF &globalPos)
{
    const int16_t x = globalPos.x();
    const int16_t y = globalPos.y();

    if (m_pos.pending) {
        m_pos.cache = QPoint(x, y);
        m_pos.cached = true;
        return;
    }
    m_pos.pending = true;

    xcb_client_message_data_t data = {};
    data.data32[0] = DataBridge::self()->dnd()->window();
    data.data32[2] = (x << 16) | y;
    data.data32[3] = XCB_CURRENT_TIME;
    data.data32[4] = clientActionToAtom(m_proposedAction);

    Drag::sendClientMessage(m_target->window(), atoms->xdnd_position, &data);
}

void Xvisit::leave()
{
    if (m_state.dropped) {
        // dropped, but not yet finished, it'll be cleaned up when the drag finishes
        return;
    }
    if (m_state.finished) {
        // was already finished
        return;
    }
    // we only need to leave, when we entered before
    if (m_state.entered) {
        sendLeave();
    }
    doFinish();
}

void Xvisit::receiveOffer()
{
    retrieveSupportedActions();
    auto dragSource = m_drag->dataSourceIface();
    connect(dragSource, &KWaylandServer::AbstractDataSource::supportedDragAndDropActionsChanged,
                          this, &Xvisit::retrieveSupportedActions);
    enter();
}

void Xvisit::enter()
{
    m_state.entered = true;
    // send enter event and current position to X client
    sendEnter();
    sendPosition(waylandServer()->seat()->pointerPos());

    // proxy future pointer position changes
    m_motionConnection = connect(waylandServer()->seat(),
                          &KWaylandServer::SeatInterface::pointerPosChanged,
                          this, &Xvisit::sendPosition);
}

void Xvisit::sendEnter()
{
    auto dataSource = m_drag->dataSourceIface();
    if (!dataSource) {
        return;
    }

    xcb_client_message_data_t data = {};
    data.data32[0] = DataBridge::self()->dnd()->window();
    data.data32[1] = m_version << 24;

    const auto mimeTypesNames = dataSource->mimeTypes();
    const int mimesCount = mimeTypesNames.size();
    size_t cnt = 0;
    size_t totalCnt = 0;
    for (const auto &mimeName : mimeTypesNames) {
        // 3 mimes and less can be sent directly in the XdndEnter message
        if (totalCnt == 3) {
            break;
        }
        const auto atom = Selection::mimeTypeToAtom(mimeName);

        if (atom != XCB_ATOM_NONE) {
            data.data32[cnt + 2] = atom;
            cnt++;
        }
        totalCnt++;
    }
    for (int i = cnt; i < 4; i++) {
        data.data32[i + 2] = XCB_ATOM_NONE;
    }

    if (mimesCount > 3) {
        // need to first transfer all available mime types
        data.data32[1] |= 1;

        QVector<xcb_atom_t> targets;
        targets.resize(mimesCount);

        size_t cnt = 0;
        for (const auto &mimeName : mimeTypesNames) {
            const auto atom = Selection::mimeTypeToAtom(mimeName);
            if (atom != XCB_ATOM_NONE) {
                targets[cnt] = atom;
                cnt++;
            }
        }

        xcb_change_property(kwinApp()->x11Connection(),
                            XCB_PROP_MODE_REPLACE,
                            DataBridge::self()->dnd()->window(),
                            atoms->xdnd_type_list,
                            XCB_ATOM_ATOM,
                            32, cnt, targets.data());
    }
    Drag::sendClientMessage(m_target->window(), atoms->xdnd_enter, &data);
}

void Xvisit::sendDrop(uint32_t time)
{
    xcb_client_message_data_t data = {};
    data.data32[0] = DataBridge::self()->dnd()->window();
    data.data32[2] = time;

    Drag::sendClientMessage(m_target->window(), atoms->xdnd_drop, &data);

    if (m_version < 2) {
        doFinish();
    }
}

void Xvisit::sendLeave()
{
    xcb_client_message_data_t data = {};
    data.data32[0] = DataBridge::self()->dnd()->window();
    Drag::sendClientMessage(m_target->window(), atoms->xdnd_leave, &data);
}

void Xvisit::retrieveSupportedActions()
{
    m_supportedActions = m_drag->dataSourceIface()->supportedDragAndDropActions();
    determineProposedAction();
    requestDragAndDropAction();
}

void Xvisit::determineProposedAction()
{
    DnDAction oldProposedAction = m_proposedAction;
    if (m_supportedActions.testFlag(m_preferredAction)) {
        m_proposedAction = m_preferredAction;
    } else if (m_supportedActions.testFlag(DnDAction::Copy)) {
        m_proposedAction = DnDAction::Copy;
    } else {
        m_proposedAction = DnDAction::None;
    }
    // send updated action to X target
    if (oldProposedAction != m_proposedAction) {
        sendPosition(waylandServer()->seat()->pointerPos());
    }
}

void Xvisit::requestDragAndDropAction()
{
    DnDAction action = m_preferredAction != DnDAction::None ? m_preferredAction:
                                                           DnDAction::Copy;
    // we assume the X client supports Move, but this might be wrong - then
    // the drag just cancels, if the user tries to force it.

    // As we skip the client data device, we do action negotiation directly then tell the source.
    if (m_supportedActions.testFlag(action)) {
        // everything is supported, no changes are needed
    } else if (m_supportedActions.testFlag(DnDAction::Copy)) {
        action = DnDAction::Copy;
    } else if (m_supportedActions.testFlag(DnDAction::Move)) {
        action = DnDAction::Move;
    }
    if (auto dataSource = m_drag->dataSourceIface()) {
        dataSource->dndAction(action);
    }
}

void Xvisit::drop()
{
    Q_ASSERT(!m_state.finished);
    m_state.dropped = true;
    // stop further updates
    // TODO: revisit when we allow ask action
    stopConnections();
    if (!m_state.entered) {
        // wait for enter (init + offers)
        return;
    }
    if (m_pos.pending) {
        // wait for pending position roundtrip
        return;
    }
    if (!m_accepts) {
        // target does not accept current action/offer
        sendLeave();
        doFinish();
        return;
    }
    // dnd session ended successfully
    sendDrop(XCB_CURRENT_TIME);
}

void Xvisit::doFinish()
{
    m_state.finished = true;
    m_pos.cached = false;
    stopConnections();
    Q_EMIT finish(this);
}

void Xvisit::stopConnections()
{
    // final outcome has been determined from Wayland side
    // no more updates needed
    disconnect(m_enterConnection);
    m_enterConnection = QMetaObject::Connection();
    disconnect(m_dropConnection);
    m_dropConnection = QMetaObject::Connection();

    disconnect(m_motionConnection);
    m_motionConnection = QMetaObject::Connection();
    disconnect(m_actionConnection);
    m_actionConnection = QMetaObject::Connection();
}

} // namespace Xwl
} // namespace KWin
