/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All rights reserved.
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

#ifndef StyleResolverState_h
#define StyleResolverState_h

#include "core/CSSPropertyNames.h"

#include "core/css/CSSSVGDocumentValue.h"
#include "core/css/CSSToLengthConversionData.h"
#include "core/css/resolver/CSSToStyleMap.h"
#include "core/css/resolver/ElementResolveContext.h"
#include "core/css/resolver/ElementStyleResources.h"
#include "core/css/resolver/FontBuilder.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/rendering/style/CachedUAStyle.h"
#include "core/rendering/style/RenderStyle.h"
#include "core/rendering/style/StyleInheritedData.h"

namespace blink {

class CSSAnimationUpdate;
class FontDescription;

class StyleResolverState {
    STACK_ALLOCATED();
    WTF_MAKE_NONCOPYABLE(StyleResolverState);
public:
    StyleResolverState(Document&, const ElementResolveContext&, RenderStyle* parentStyle);
    StyleResolverState(Document&, Element*, RenderStyle* parentStyle = 0);
    ~StyleResolverState();

    // In FontFaceSet and CanvasRenderingContext2D, we don't have an element to grab the document from.
    // This is why we have to store the document separately.
    Document& document() const { return *m_document; }
    // These are all just pass-through methods to ElementResolveContext.
    Element* element() const { return m_elementContext.element(); }
    const ContainerNode* parentNode() const { return m_elementContext.parentNode(); }
    const RenderStyle* rootElementStyle() const { return m_elementContext.rootElementStyle(); }
    EInsideLink elementLinkState() const { return m_elementContext.elementLinkState(); }
    bool distributedToInsertionPoint() const { return m_elementContext.distributedToInsertionPoint(); }

    const ElementResolveContext& elementContext() const { return m_elementContext; }

    void setStyle(PassRefPtr<RenderStyle> style)
    {
        // FIXME: Improve RAII of StyleResolverState to remove this function.
        m_style = style;
        m_cssToLengthConversionData = CSSToLengthConversionData(m_style.get(), rootElementStyle(), document().renderView(), m_style->effectiveZoom());
        m_fontBuilder.setFontDescription(m_style->fontDescription());
    }
    const RenderStyle* style() const { return m_style.get(); }
    RenderStyle* style() { return m_style.get(); }
    PassRefPtr<RenderStyle> takeStyle() { return m_style.release(); }

    const CSSToLengthConversionData& cssToLengthConversionData() const { return m_cssToLengthConversionData; }

    void setConversionFontSizes(const CSSToLengthConversionData::FontSizes& fontSizes) { m_cssToLengthConversionData.setFontSizes(fontSizes); }
    void setConversionZoom(float zoom) { m_cssToLengthConversionData.setZoom(zoom); }

    void setAnimationUpdate(PassOwnPtrWillBeRawPtr<CSSAnimationUpdate>);
    const CSSAnimationUpdate* animationUpdate() { return m_animationUpdate.get(); }
    PassOwnPtrWillBeRawPtr<CSSAnimationUpdate> takeAnimationUpdate();

    void setParentStyle(PassRefPtr<RenderStyle> parentStyle) { m_parentStyle = parentStyle; }
    const RenderStyle* parentStyle() const { return m_parentStyle.get(); }
    RenderStyle* parentStyle() { return m_parentStyle.get(); }

    // FIXME: These are effectively side-channel "out parameters" for the various
    // map functions. When we map from CSS to style objects we use this state object
    // to track various meta-data about that mapping (e.g. if it's cache-able).
    // We need to move this data off of StyleResolverState and closer to the
    // objects it applies to. Possibly separating (immutable) inputs from (mutable) outputs.
    void setApplyPropertyToRegularStyle(bool isApply) { m_applyPropertyToRegularStyle = isApply; }
    void setApplyPropertyToVisitedLinkStyle(bool isApply) { m_applyPropertyToVisitedLinkStyle = isApply; }
    bool applyPropertyToRegularStyle() const { return m_applyPropertyToRegularStyle; }
    bool applyPropertyToVisitedLinkStyle() const { return m_applyPropertyToVisitedLinkStyle; }

    void cacheUserAgentBorderAndBackground()
    {
        // RenderTheme only needs the cached style if it has an appearance,
        // and constructing it is expensive so we avoid it if possible.
        if (!style()->hasAppearance())
            return;

        m_cachedUAStyle = CachedUAStyle::create(style());
    }

    const CachedUAStyle* cachedUAStyle() const
    {
        return m_cachedUAStyle.get();
    }

    ElementStyleResources& elementStyleResources() { return m_elementStyleResources; }

    // FIXME: Once styleImage can be made to not take a StyleResolverState
    // this convenience function should be removed. As-is, without this, call
    // sites are extremely verbose.
    PassRefPtr<StyleImage> styleImage(CSSPropertyID propertyId, CSSValue* value)
    {
        return m_elementStyleResources.styleImage(document(), document().textLinkColors(), style()->color(), propertyId, value);
    }

    FontBuilder& fontBuilder() { return m_fontBuilder; }
    // FIXME: These exist as a primitive way to track mutations to font-related properties
    // on a RenderStyle. As designed, these are very error-prone, as some callers
    // set these directly on the RenderStyle w/o telling us. Presumably we'll
    // want to design a better wrapper around RenderStyle for tracking these mutations
    // and separate it from StyleResolverState.
    const FontDescription& parentFontDescription() { return m_parentStyle->fontDescription(); }
    void setZoom(float f) { m_fontBuilder.didChangeFontParameters(m_style->setZoom(f)); }
    void setEffectiveZoom(float f) { m_fontBuilder.didChangeFontParameters(m_style->setEffectiveZoom(f)); }
    void setWritingMode(WritingMode writingMode) { m_fontBuilder.didChangeFontParameters(m_style->setWritingMode(writingMode)); }
    void setTextOrientation(TextOrientation textOrientation) { m_fontBuilder.didChangeFontParameters(m_style->setTextOrientation(textOrientation)); }

private:
    ElementResolveContext m_elementContext;
    RawPtrWillBeMember<Document> m_document;

    // m_style is the primary output for each element's style resolve.
    RefPtr<RenderStyle> m_style;

    CSSToLengthConversionData m_cssToLengthConversionData;

    // m_parentStyle is not always just ElementResolveContext::parentStyle,
    // so we keep it separate.
    RefPtr<RenderStyle> m_parentStyle;

    OwnPtrWillBeMember<CSSAnimationUpdate> m_animationUpdate;

    bool m_applyPropertyToRegularStyle;
    bool m_applyPropertyToVisitedLinkStyle;

    FontBuilder m_fontBuilder;

    OwnPtr<CachedUAStyle> m_cachedUAStyle;

    ElementStyleResources m_elementStyleResources;
};

} // namespace blink

#endif // StyleResolverState_h
