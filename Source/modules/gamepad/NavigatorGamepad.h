/*
 * Copyright (C) 2011, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#ifndef NavigatorGamepad_h
#define NavigatorGamepad_h

#include "core/frame/DOMWindowLifecycleObserver.h"
#include "core/frame/DOMWindowProperty.h"
#include "core/frame/PlatformEventController.h"
#include "platform/AsyncMethodRunner.h"
#include "platform/Supplementable.h"
#include "platform/heap/Handle.h"
#include "public/platform/WebGamepads.h"

namespace blink {

class Document;
class Gamepad;
class GamepadList;
class Navigator;

class NavigatorGamepad final : public NoBaseWillBeGarbageCollectedFinalized<NavigatorGamepad>, public WillBeHeapSupplement<Navigator>, public DOMWindowProperty, public PlatformEventController, public DOMWindowLifecycleObserver {
    WILL_BE_USING_GARBAGE_COLLECTED_MIXIN(NavigatorGamepad);
public:
    static NavigatorGamepad* from(Document&);
    static NavigatorGamepad& from(Navigator&);
    virtual ~NavigatorGamepad();

    static GamepadList* getGamepads(Navigator&);
    GamepadList* gamepads();

    virtual void trace(Visitor*);

    void didConnectOrDisconnectGamepad(unsigned index, const WebGamepad&, bool connected);

private:
    explicit NavigatorGamepad(LocalFrame*);

    static const char* supplementName();

    void dispatchOneEvent();
    void didRemoveGamepadEventListeners();

    // DOMWindowProperty
    virtual void willDestroyGlobalObjectInFrame() override;
    virtual void willDetachGlobalObjectFromFrame() override;

    // PlatformEventController
    virtual void registerWithDispatcher() override;
    virtual void unregisterWithDispatcher() override;
    virtual bool hasLastData() override;
    virtual void didUpdateData() override;
    virtual void pageVisibilityChanged() override;

    // DOMWindowLifecycleObserver
    virtual void didAddEventListener(LocalDOMWindow*, const AtomicString&) override;
    virtual void didRemoveEventListener(LocalDOMWindow*, const AtomicString&) override;
    virtual void didRemoveAllEventListeners(LocalDOMWindow*) override;

    PersistentWillBeMember<GamepadList> m_gamepads;
    PersistentHeapDequeWillBeHeapDeque<Member<Gamepad> > m_pendingEvents;
    AsyncMethodRunner<NavigatorGamepad> m_dispatchOneEventRunner;
};

} // namespace blink

#endif // NavigatorGamepad_h
