/*
 * (C) 1999 Lars Knoll (knoll@kde.org)
 * (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007 Apple Inc. All rights reserved.
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
 *
 */

#include "config.h"
#include "core/rendering/RenderTextFragment.h"

#include "core/dom/FirstLetterPseudoElement.h"
#include "core/dom/PseudoElement.h"
#include "core/dom/StyleChangeReason.h"
#include "core/dom/Text.h"
#include "core/rendering/HitTestResult.h"
#include "core/rendering/RenderBlock.h"

namespace blink {

RenderTextFragment::RenderTextFragment(Node* node, StringImpl* str, int startOffset, int length)
    : RenderText(node, str ? str->substring(startOffset, length) : PassRefPtr<StringImpl>(nullptr))
    , m_start(startOffset)
    , m_end(length)
    , m_isRemainingTextRenderer(false)
    , m_contentString(str)
    , m_firstLetterPseudoElement(nullptr)
{
}

RenderTextFragment::RenderTextFragment(Node* node, StringImpl* str)
    : RenderText(node, str)
    , m_start(0)
    , m_end(str ? str->length() : 0)
    , m_isRemainingTextRenderer(false)
    , m_contentString(str)
    , m_firstLetterPseudoElement(nullptr)
{
}

RenderTextFragment::~RenderTextFragment()
{
    ASSERT(!m_firstLetterPseudoElement);
}

void RenderTextFragment::destroy()
{
    if (m_isRemainingTextRenderer && m_firstLetterPseudoElement)
        m_firstLetterPseudoElement->setRemainingTextRenderer(nullptr);
    m_firstLetterPseudoElement = nullptr;
    RenderText::destroy();
}

void RenderTextFragment::trace(Visitor* visitor)
{
    visitor->trace(m_firstLetterPseudoElement);
    RenderText::trace(visitor);
}

PassRefPtr<StringImpl> RenderTextFragment::completeText() const
{
    Text* text = associatedTextNode();
    return text ? text->dataImpl() : contentString();
}

void RenderTextFragment::setContentString(StringImpl* str)
{
    m_contentString = str;
    setText(str);
}

PassRefPtr<StringImpl> RenderTextFragment::originalText() const
{
    RefPtr<StringImpl> result = completeText();
    if (!result)
        return nullptr;
    return result->substring(start(), end());
}

void RenderTextFragment::setText(PassRefPtr<StringImpl> text, bool force)
{
    RenderText::setText(text, force);

    m_start = 0;
    m_end = textLength();

    // If we're the remaining text from a first letter then we have to tell the
    // first letter pseudo element to reattach itself so it can re-calculate the
    // correct first-letter settings.
    if (RenderObject* previous = previousSibling()) {
        if (!previous->isPseudoElement() || !previous->node()->isFirstLetterPseudoElement())
            return;

        // Tell the first letter container node, and the first-letter node
        // that their style may have changed.
        // e.g. fast/css/first-letter-detach.html
        toFirstLetterPseudoElement(previous->node())->setNeedsUpdate();
    }
}

void RenderTextFragment::transformText()
{
    // Note, we have to call RenderText::setText here because, if we use our
    // version we will, potentially, screw up the first-letter settings where
    // we only use portions of the string.
    if (RefPtr<StringImpl> textToTransform = originalText())
        RenderText::setText(textToTransform.release(), true);
}

UChar RenderTextFragment::previousCharacter() const
{
    if (start()) {
        StringImpl* original = completeText().get();
        if (original && start() <= original->length())
            return (*original)[start() - 1];
    }

    return RenderText::previousCharacter();
}

// If this is the renderer for a first-letter pseudoNode then we have to look
// at the node for the remaining text to find our content.
Text* RenderTextFragment::associatedTextNode() const
{
    Node* node = m_isRemainingTextRenderer ? this->node() : this->firstLetterPseudoElement();
    if (!node)
        return nullptr;

    if (node->isFirstLetterPseudoElement()) {
        FirstLetterPseudoElement* pseudo = toFirstLetterPseudoElement(node);
        RenderObject* nextRenderer = FirstLetterPseudoElement::firstLetterTextRenderer(*pseudo);
        if (!nextRenderer)
            return nullptr;
        node = nextRenderer->node();
    }
    return (node && node->isTextNode()) ? toText(node) : nullptr;
}

} // namespace blink
