/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef FrameFetchContext_h
#define FrameFetchContext_h

#include "core/fetch/FetchContext.h"
#include "platform/network/ResourceRequest.h"
#include "wtf/PassOwnPtr.h"

namespace blink {

class Document;
class DocumentLoader;
class LocalFrame;
class Page;
class ResourceError;
class ResourceLoader;
class ResourceResponse;
class ResourceRequest;

class FrameFetchContext FINAL : public FetchContext {
public:
    static PassOwnPtr<FrameFetchContext> create(LocalFrame* frame) { return adoptPtr(new FrameFetchContext(frame)); }

    virtual void reportLocalLoadFailed(const KURL&) OVERRIDE;
    virtual void addAdditionalRequestHeaders(Document*, ResourceRequest&, FetchResourceType) OVERRIDE;
    virtual void setFirstPartyForCookies(ResourceRequest&) OVERRIDE;
    virtual CachePolicy cachePolicy(Document*) const OVERRIDE;
    virtual void dispatchDidChangeResourcePriority(unsigned long identifier, ResourceLoadPriority, int intraPriorityValue);
    virtual void dispatchWillSendRequest(DocumentLoader*, unsigned long identifier, ResourceRequest&, const ResourceResponse& redirectResponse, const FetchInitiatorInfo& = FetchInitiatorInfo()) OVERRIDE;
    virtual void dispatchDidLoadResourceFromMemoryCache(const ResourceRequest&, const ResourceResponse&) OVERRIDE;
    virtual void dispatchDidReceiveResponse(DocumentLoader*, unsigned long identifier, const ResourceResponse&, ResourceLoader* = 0) OVERRIDE;
    virtual void dispatchDidReceiveData(DocumentLoader*, unsigned long identifier, const char* data, int dataLength, int encodedDataLength) OVERRIDE;
    virtual void dispatchDidDownloadData(DocumentLoader*, unsigned long identifier, int dataLength, int encodedDataLength)  OVERRIDE;
    virtual void dispatchDidFinishLoading(DocumentLoader*, unsigned long identifier, double finishTime, int64_t encodedDataLength) OVERRIDE;
    virtual void dispatchDidFail(DocumentLoader*, unsigned long identifier, const ResourceError&, bool isInternalRequest) OVERRIDE;
    virtual void sendRemainingDelegateMessages(DocumentLoader*, unsigned long identifier, const ResourceResponse&, int dataLength) OVERRIDE;

private:
    explicit FrameFetchContext(LocalFrame*);
    inline DocumentLoader* ensureLoader(DocumentLoader*);

    LocalFrame* m_frame;
};

}

#endif
