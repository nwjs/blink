/*
 * Copyright (C) 2006, 2007 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "core/events/OverflowEvent.h"

namespace blink {

OverflowEvent::OverflowEvent()
    : Event(EventTypeNames::overflowchanged, false, false)
    , m_orient(VERTICAL)
    , m_horizontalOverflow(false)
    , m_verticalOverflow(false)
{
}

OverflowEvent::OverflowEvent(bool horizontalOverflowChanged, bool horizontalOverflow, bool verticalOverflowChanged, bool verticalOverflow)
    : Event(EventTypeNames::overflowchanged, false, false)
    , m_horizontalOverflow(horizontalOverflow)
    , m_verticalOverflow(verticalOverflow)
{
    ASSERT(horizontalOverflowChanged || verticalOverflowChanged);

    if (horizontalOverflowChanged && verticalOverflowChanged)
        m_orient = BOTH;
    else if (horizontalOverflowChanged)
        m_orient = HORIZONTAL;
    else
        m_orient = VERTICAL;
}

OverflowEvent::OverflowEvent(const AtomicString& type, const OverflowEventInit& initializer)
    : Event(type, initializer)
    , m_orient(0)
    , m_horizontalOverflow(false)
    , m_verticalOverflow(false)
{
    if (initializer.hasOrient())
        m_orient = initializer.orient();
    if (initializer.hasHorizontalOverflow())
        m_horizontalOverflow = initializer.horizontalOverflow();
    if (initializer.hasVerticalOverflow())
        m_verticalOverflow = initializer.verticalOverflow();
}

const AtomicString& OverflowEvent::interfaceName() const
{
    return EventNames::OverflowEvent;
}

void OverflowEvent::trace(Visitor* visitor)
{
    Event::trace(visitor);
}

}
