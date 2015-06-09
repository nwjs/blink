/*
 * Copyright (C) 2006 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NavigationAction_h
#define NavigationAction_h

#include "core/dom/DOMTimeStamp.h"
#include "core/events/Event.h"
#include "core/loader/FrameLoaderTypes.h"
#include "core/loader/NavigationPolicy.h"
#include "platform/network/ResourceRequest.h"
#include "platform/weborigin/KURL.h"
#include "wtf/Forward.h"

namespace blink {

    class NavigationAction {
    public:
        NavigationAction();
        NavigationAction(const ResourceRequest&, FrameLoadType = FrameLoadTypeStandard, bool isFormSubmission = false, PassRefPtrWillBeRawPtr<Event> = nullptr);

        const ResourceRequest& resourceRequest() const { return m_resourceRequest; }
        ResourceRequest& mutableResourceRequest() { return m_resourceRequest; }
        NavigationType type() const { return m_type; }
        DOMTimeStamp eventTimeStamp() const { return m_eventTimeStamp; }
        NavigationPolicy policy() const { return m_policy; }
        bool shouldOpenInNewWindow() const { return m_policy != NavigationPolicyCurrentTab; }
        void setPolicy(NavigationPolicy& policy) { m_policy = policy; }

    private:
        ResourceRequest m_resourceRequest;
        NavigationType m_type;
        DOMTimeStamp m_eventTimeStamp;
        NavigationPolicy m_policy;
    };

}

#endif
