/*
 * Copyright (C) 1998, 1999 Torben Weis <weis@kde.org>
 *                     1999-2001 Lars Knoll <knoll@kde.org>
 *                     1999-2001 Antti Koivisto <koivisto@kde.org>
 *                     2000-2001 Simon Hausmann <hausmann@kde.org>
 *                     2000-2001 Dirk Mueller <mueller@kde.org>
 *                     2000 Stefan Schimanski <1Stein@gmx.de>
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2008 Eric Seidel <eric@webkit.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef Frame_h
#define Frame_h

#include "core/page/FrameTree.h"
#include "platform/heap/Handle.h"
#include "wtf/Forward.h"
#include "wtf/RefCounted.h"

namespace blink {
class WebLayer;
}

namespace blink {

class ChromeClient;
class FrameClient;
class FrameHost;
class FrameOwner;
class HTMLFrameOwnerElement;
class LocalDOMWindow;
class Page;
class RenderPart;
class Settings;

class Frame : public RefCounted<Frame> {
public:
    virtual bool isLocalFrame() const { return false; }
    virtual bool isRemoteFrame() const { return false; }

    virtual ~Frame();

    virtual void detach() = 0;
    void detachChildren();

    FrameClient* client() const;
    void clearClient();

    // NOTE: Page is moving out of Blink up into the browser process as
    // part of the site-isolation (out of process iframes) work.
    // FrameHost should be used instead where possible.
    Page* page() const;
    FrameHost* host() const; // Null when the frame is detached.

    bool isMainFrame() const;
    bool isLocalRoot() const;

    virtual void disconnectOwnerElement();

    FrameOwner* owner() const;
    HTMLFrameOwnerElement* deprecatedLocalOwner() const;

    // FIXME: LocalDOMWindow and Document should both be moved to LocalFrame
    // after RemoteFrame is complete enough to exist without them.
    virtual void setDOMWindow(PassRefPtrWillBeRawPtr<LocalDOMWindow>);
    LocalDOMWindow* domWindow() const;

    FrameTree& tree() const;
    ChromeClient& chromeClient() const;

    RenderPart* ownerRenderer() const; // Renderer for the element that contains this frame.

    // FIXME: These should move to RemoteFrame when that is instantiated.
    void setRemotePlatformLayer(blink::WebLayer*);
    blink::WebLayer* remotePlatformLayer() const { return m_remotePlatformLayer; }

    Settings* settings() const; // can be null

    // FIXME: This method identifies a LocalFrame that is acting as a RemoteFrame.
    // It is necessary only until we can instantiate a RemoteFrame, at which point
    // it can be removed and its callers can be converted to use the isRemoteFrame()
    // method.
    bool isRemoteFrameTemporary() const { return m_remotePlatformLayer; }

    void setNodeJS(bool node) { m_nodejs = node; }
    bool isNodeJS() const;
    bool isNwCheckXFrame() const;
    bool isNwDisabledChildFrame() const;
    bool isNwFakeTop() const;
    void setDevtoolsJail(Frame* iframe);
    Frame* getDevtoolsJail() { return m_devtoolsJail; }

protected:
    Frame(FrameClient*, FrameHost*, FrameOwner*);

    mutable FrameTree m_treeNode;

    FrameHost* m_host;
    FrameOwner* m_owner;

    RefPtrWillBePersistent<LocalDOMWindow> m_domWindow;

private:
    FrameClient* m_client;
    blink::WebLayer* m_remotePlatformLayer;

    bool m_nodejs;
    Frame* m_devtoolsJail;
    Frame* m_devJailOwner;
};

inline FrameClient* Frame::client() const
{
    return m_client;
}

inline void Frame::clearClient()
{
    m_client = 0;
}

inline LocalDOMWindow* Frame::domWindow() const
{
    return m_domWindow.get();
}

inline FrameOwner* Frame::owner() const
{
    return m_owner;
}

inline FrameTree& Frame::tree() const
{
    return m_treeNode;
}

// Allow equality comparisons of Frames by reference or pointer, interchangeably.
DEFINE_COMPARISON_OPERATORS_WITH_REFERENCES_REFCOUNTED(Frame)

} // namespace blink

#endif // Frame_h
