/*
 * Copyright (C) 2006 Apple Computer, Inc.
 * Copyright (C) 2006 Alexander Kellett <lypanov@kde.org>
 * Copyright (C) 2006 Oliver Hunt <ojh16@student.canterbury.ac.nz>
 * Copyright (C) 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2008 Rob Buis <buis@kde.org>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) Research In Motion Limited 2010-2012. All rights reserved.
 * Copyright (C) 2012 Google Inc.
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

#include "config.h"

#include "core/rendering/svg/RenderSVGText.h"

#include "core/editing/PositionWithAffinity.h"
#include "core/rendering/HitTestRequest.h"
#include "core/rendering/HitTestResult.h"
#include "core/rendering/PaintInfo.h"
#include "core/rendering/PointerEventsHitRules.h"
#include "core/rendering/style/ShadowList.h"
#include "core/rendering/svg/RenderSVGInline.h"
#include "core/rendering/svg/RenderSVGInlineText.h"
#include "core/rendering/svg/RenderSVGResource.h"
#include "core/rendering/svg/RenderSVGRoot.h"
#include "core/rendering/svg/SVGRenderSupport.h"
#include "core/rendering/svg/SVGResourcesCache.h"
#include "core/rendering/svg/SVGRootInlineBox.h"
#include "core/rendering/svg/SVGTextRunRenderingContext.h"
#include "core/svg/SVGLengthList.h"
#include "core/svg/SVGTextElement.h"
#include "core/svg/SVGTransformList.h"
#include "core/svg/SVGURIReference.h"
#include "platform/FloatConversion.h"
#include "platform/fonts/FontCache.h"
#include "platform/fonts/SimpleFontData.h"
#include "platform/geometry/FloatQuad.h"
#include "platform/geometry/TransformState.h"
#include "platform/graphics/GraphicsContextStateSaver.h"

namespace blink {

RenderSVGText::RenderSVGText(SVGTextElement* node)
    : RenderSVGBlock(node)
    , m_needsReordering(false)
    , m_needsPositioningValuesUpdate(false)
    , m_needsTransformUpdate(true)
    , m_needsTextMetricsUpdate(false)
{
}

RenderSVGText::~RenderSVGText()
{
    ASSERT(m_layoutAttributes.isEmpty());
}

bool RenderSVGText::isChildAllowed(RenderObject* child, RenderStyle*) const
{
    return child->isSVGInline() || (child->isText() && SVGRenderSupport::isRenderableTextNode(child));
}

RenderSVGText* RenderSVGText::locateRenderSVGTextAncestor(RenderObject* start)
{
    ASSERT(start);
    while (start && !start->isSVGText())
        start = start->parent();
    if (!start || !start->isSVGText())
        return 0;
    return toRenderSVGText(start);
}

const RenderSVGText* RenderSVGText::locateRenderSVGTextAncestor(const RenderObject* start)
{
    ASSERT(start);
    while (start && !start->isSVGText())
        start = start->parent();
    if (!start || !start->isSVGText())
        return 0;
    return toRenderSVGText(start);
}

void RenderSVGText::mapRectToPaintInvalidationBacking(const RenderLayerModelObject* paintInvalidationContainer, LayoutRect& rect, ViewportConstrainedPosition, const PaintInvalidationState* paintInvalidationState) const
{
    FloatRect paintInvalidationRect = rect;
    computeFloatRectForPaintInvalidation(paintInvalidationContainer, paintInvalidationRect, paintInvalidationState);
    rect = enclosingLayoutRect(paintInvalidationRect);
}

static inline void collectLayoutAttributes(RenderObject* text, Vector<SVGTextLayoutAttributes*>& attributes)
{
    for (RenderObject* descendant = text; descendant; descendant = descendant->nextInPreOrder(text)) {
        if (descendant->isSVGInlineText())
            attributes.append(toRenderSVGInlineText(descendant)->layoutAttributes());
    }
}

static inline bool findPreviousAndNextAttributes(RenderSVGText* root, RenderSVGInlineText* locateElement, SVGTextLayoutAttributes*& previous, SVGTextLayoutAttributes*& next)
{
    ASSERT(root);
    ASSERT(locateElement);
    bool stopAfterNext = false;
    RenderObject* current = root->firstChild();
    while (current) {
        if (current->isSVGInlineText()) {
            RenderSVGInlineText* text = toRenderSVGInlineText(current);
            if (locateElement != text) {
                if (stopAfterNext) {
                    next = text->layoutAttributes();
                    return true;
                }

                previous = text->layoutAttributes();
            } else {
                stopAfterNext = true;
            }
        } else if (current->isSVGInline()) {
            // Descend into text content (if possible).
            if (RenderObject* child = toRenderSVGInline(current)->firstChild()) {
                current = child;
                continue;
            }
        }

        current = current->nextInPreOrderAfterChildren(root);
    }
    return false;
}

inline bool RenderSVGText::shouldHandleSubtreeMutations() const
{
    if (beingDestroyed() || !everHadLayout()) {
        ASSERT(m_layoutAttributes.isEmpty());
        ASSERT(!m_layoutAttributesBuilder.numberOfTextPositioningElements());
        return false;
    }
    return true;
}

void RenderSVGText::subtreeChildWasAdded(RenderObject* child)
{
    ASSERT(child);
    if (!shouldHandleSubtreeMutations() || documentBeingDestroyed())
        return;

    // Always protect the cache before clearing text positioning elements when the cache will subsequently be rebuilt.
    FontCachePurgePreventer fontCachePurgePreventer;

    // The positioning elements cache doesn't include the new 'child' yet. Clear the
    // cache, as the next buildLayoutAttributesForTextRenderer() call rebuilds it.
    m_layoutAttributesBuilder.clearTextPositioningElements();

    if (!child->isSVGInlineText() && !child->isSVGInline())
        return;

    // Detect changes in layout attributes and only measure those text parts that have changed!
    Vector<SVGTextLayoutAttributes*> newLayoutAttributes;
    collectLayoutAttributes(this, newLayoutAttributes);
    if (newLayoutAttributes.isEmpty()) {
        ASSERT(m_layoutAttributes.isEmpty());
        return;
    }

    // Compare m_layoutAttributes with newLayoutAttributes to figure out which attribute got added.
    size_t size = newLayoutAttributes.size();
    SVGTextLayoutAttributes* attributes = 0;
    for (size_t i = 0; i < size; ++i) {
        attributes = newLayoutAttributes[i];
        if (m_layoutAttributes.find(attributes) == kNotFound) {
            // Every time this is invoked, there's only a single new entry in the newLayoutAttributes list, compared to the old in m_layoutAttributes.
            SVGTextLayoutAttributes* previous = 0;
            SVGTextLayoutAttributes* next = 0;
            ASSERT_UNUSED(child, attributes->context() == child);
            findPreviousAndNextAttributes(this, attributes->context(), previous, next);

            if (previous)
                m_layoutAttributesBuilder.buildLayoutAttributesForTextRenderer(previous->context());
            m_layoutAttributesBuilder.buildLayoutAttributesForTextRenderer(attributes->context());
            if (next)
                m_layoutAttributesBuilder.buildLayoutAttributesForTextRenderer(next->context());
            break;
        }
    }

#if ENABLE(ASSERT)
    // Verify that m_layoutAttributes only differs by a maximum of one entry.
    for (size_t i = 0; i < size; ++i)
        ASSERT(m_layoutAttributes.find(newLayoutAttributes[i]) != kNotFound || newLayoutAttributes[i] == attributes);
#endif

    m_layoutAttributes = newLayoutAttributes;
}

static inline void checkLayoutAttributesConsistency(RenderSVGText* text, Vector<SVGTextLayoutAttributes*>& expectedLayoutAttributes)
{
#if ENABLE(ASSERT)
    Vector<SVGTextLayoutAttributes*> newLayoutAttributes;
    collectLayoutAttributes(text, newLayoutAttributes);
    ASSERT(newLayoutAttributes == expectedLayoutAttributes);
#endif
}

void RenderSVGText::willBeDestroyed()
{
    m_layoutAttributes.clear();
    m_layoutAttributesBuilder.clearTextPositioningElements();

    RenderSVGBlock::willBeDestroyed();
}

void RenderSVGText::subtreeChildWillBeRemoved(RenderObject* child, Vector<SVGTextLayoutAttributes*, 2>& affectedAttributes)
{
    ASSERT(child);
    if (!shouldHandleSubtreeMutations())
        return;

    checkLayoutAttributesConsistency(this, m_layoutAttributes);

    // The positioning elements cache depends on the size of each text renderer in the
    // subtree. If this changes, clear the cache. It's going to be rebuilt below.
    m_layoutAttributesBuilder.clearTextPositioningElements();
    if (m_layoutAttributes.isEmpty() || !child->isSVGInlineText())
        return;

    // This logic requires that the 'text' child is still inserted in the tree.
    RenderSVGInlineText* text = toRenderSVGInlineText(child);
    SVGTextLayoutAttributes* previous = 0;
    SVGTextLayoutAttributes* next = 0;
    if (!documentBeingDestroyed())
        findPreviousAndNextAttributes(this, text, previous, next);

    if (previous)
        affectedAttributes.append(previous);
    if (next)
        affectedAttributes.append(next);

    size_t position = m_layoutAttributes.find(text->layoutAttributes());
    ASSERT(position != kNotFound);
    m_layoutAttributes.remove(position);
}

void RenderSVGText::subtreeChildWasRemoved(const Vector<SVGTextLayoutAttributes*, 2>& affectedAttributes)
{
    if (!shouldHandleSubtreeMutations() || documentBeingDestroyed()) {
        ASSERT(affectedAttributes.isEmpty());
        return;
    }

    // This is called immediately after subtreeChildWillBeDestroyed, once the RenderSVGInlineText::willBeDestroyed() method
    // passes on to the base class, which removes us from the render tree. At this point we can update the layout attributes.
    unsigned size = affectedAttributes.size();
    for (unsigned i = 0; i < size; ++i)
        m_layoutAttributesBuilder.buildLayoutAttributesForTextRenderer(affectedAttributes[i]->context());
}

void RenderSVGText::subtreeStyleDidChange()
{
    if (!shouldHandleSubtreeMutations() || documentBeingDestroyed())
        return;

    checkLayoutAttributesConsistency(this, m_layoutAttributes);

    // Only update the metrics cache, but not the text positioning element cache
    // nor the layout attributes cached in the leaf #text renderers.
    FontCachePurgePreventer fontCachePurgePreventer;
    for (RenderObject* descendant = firstChild(); descendant; descendant = descendant->nextInPreOrder(this)) {
        if (descendant->isSVGInlineText())
            m_layoutAttributesBuilder.rebuildMetricsForTextRenderer(toRenderSVGInlineText(descendant));
    }
}

void RenderSVGText::subtreeTextDidChange(RenderSVGInlineText* text)
{
    ASSERT(text);
    ASSERT(!beingDestroyed());
    if (!everHadLayout()) {
        ASSERT(m_layoutAttributes.isEmpty());
        ASSERT(!m_layoutAttributesBuilder.numberOfTextPositioningElements());
        return;
    }

    // Always protect the cache before clearing text positioning elements when the cache will subsequently be rebuilt.
    FontCachePurgePreventer fontCachePurgePreventer;

    // The positioning elements cache depends on the size of each text renderer in the
    // subtree. If this changes, clear the cache. It's going to be rebuilt below.
    m_layoutAttributesBuilder.clearTextPositioningElements();

    checkLayoutAttributesConsistency(this, m_layoutAttributes);
    for (RenderObject* descendant = text; descendant; descendant = descendant->nextInPreOrder(text)) {
        if (descendant->isSVGInlineText())
            m_layoutAttributesBuilder.buildLayoutAttributesForTextRenderer(toRenderSVGInlineText(descendant));
    }
}

static inline void updateFontInAllDescendants(RenderObject* start, SVGTextLayoutAttributesBuilder* builder = 0)
{
    for (RenderObject* descendant = start; descendant; descendant = descendant->nextInPreOrder(start)) {
        if (!descendant->isSVGInlineText())
            continue;
        RenderSVGInlineText* text = toRenderSVGInlineText(descendant);
        text->updateScaledFont();
        if (builder)
            builder->rebuildMetricsForTextRenderer(text);
    }
}

void RenderSVGText::layout()
{
    ASSERT(needsLayout());

    subtreeStyleDidChange();

    bool updateCachedBoundariesInParents = false;
    if (m_needsTransformUpdate) {
        m_localTransform = toSVGTextElement(node())->animatedLocalTransform();
        m_needsTransformUpdate = false;
        updateCachedBoundariesInParents = true;
    }

    if (!everHadLayout()) {
        // When laying out initially, collect all layout attributes, build the character data map,
        // and propogate resulting SVGLayoutAttributes to all RenderSVGInlineText children in the subtree.
        ASSERT(m_layoutAttributes.isEmpty());
        collectLayoutAttributes(this, m_layoutAttributes);
        updateFontInAllDescendants(this);
        m_layoutAttributesBuilder.buildLayoutAttributesForForSubtree(this);

        m_needsReordering = true;
        m_needsTextMetricsUpdate = false;
        m_needsPositioningValuesUpdate = false;
        updateCachedBoundariesInParents = true;
    } else if (m_needsPositioningValuesUpdate) {
        // When the x/y/dx/dy/rotate lists change, recompute the layout attributes, and eventually
        // update the on-screen font objects as well in all descendants.
        if (m_needsTextMetricsUpdate) {
            updateFontInAllDescendants(this);
            m_needsTextMetricsUpdate = false;
        }

        m_layoutAttributesBuilder.buildLayoutAttributesForForSubtree(this);
        m_needsReordering = true;
        m_needsPositioningValuesUpdate = false;
        updateCachedBoundariesInParents = true;
    } else if (m_needsTextMetricsUpdate || SVGRenderSupport::findTreeRootObject(this)->isLayoutSizeChanged()) {
        // If the root layout size changed (eg. window size changes) or the transform to the root
        // context has changed then recompute the on-screen font size.
        updateFontInAllDescendants(this, &m_layoutAttributesBuilder);

        ASSERT(!m_needsReordering);
        ASSERT(!m_needsPositioningValuesUpdate);
        m_needsTextMetricsUpdate = false;
        updateCachedBoundariesInParents = true;
    }

    checkLayoutAttributesConsistency(this, m_layoutAttributes);

    // Reduced version of RenderBlock::layoutBlock(), which only takes care of SVG text.
    // All if branches that could cause early exit in RenderBlocks layoutBlock() method are turned into assertions.
    ASSERT(!isInline());
    ASSERT(!simplifiedLayout());
    ASSERT(!scrollsOverflow());
    ASSERT(!hasControlClip());
    ASSERT(!hasColumns());
    ASSERT(!positionedObjects());
    ASSERT(!m_overflow);
    ASSERT(!isAnonymousBlock());

    if (!firstChild())
        setChildrenInline(true);

    // FIXME: We need to find a way to only layout the child boxes, if needed.
    FloatRect oldBoundaries = objectBoundingBox();
    ASSERT(childrenInline());

    rebuildFloatsFromIntruding();

    LayoutUnit beforeEdge = borderBefore() + paddingBefore();
    LayoutUnit afterEdge = borderAfter() + paddingAfter() + scrollbarLogicalHeight();
    setLogicalHeight(beforeEdge);

    LayoutUnit paintInvalidationLogicalTop = 0;
    LayoutUnit paintInvalidationLogicalBottom = 0;
    layoutInlineChildren(true, paintInvalidationLogicalTop, paintInvalidationLogicalBottom, afterEdge);

    if (m_needsReordering)
        m_needsReordering = false;

    if (!updateCachedBoundariesInParents)
        updateCachedBoundariesInParents = oldBoundaries != objectBoundingBox();

    // Invalidate all resources of this client if our layout changed.
    if (everHadLayout() && selfNeedsLayout())
        SVGResourcesCache::clientLayoutChanged(this);

    // If our bounds changed, notify the parents.
    if (updateCachedBoundariesInParents)
        RenderSVGBlock::setNeedsBoundariesUpdate();

    clearNeedsLayout();
}

RootInlineBox* RenderSVGText::createRootInlineBox()
{
    RootInlineBox* box = new SVGRootInlineBox(*this);
    box->setHasVirtualLogicalHeight();
    return box;
}

bool RenderSVGText::nodeAtFloatPoint(const HitTestRequest& request, HitTestResult& result, const FloatPoint& pointInParent, HitTestAction hitTestAction)
{
    PointerEventsHitRules hitRules(PointerEventsHitRules::SVG_TEXT_HITTESTING, request, style()->pointerEvents());
    bool isVisible = (style()->visibility() == VISIBLE);
    if (isVisible || !hitRules.requireVisible) {
        if ((hitRules.canHitBoundingBox && !objectBoundingBox().isEmpty())
            || (hitRules.canHitStroke && (style()->svgStyle().hasStroke() || !hitRules.requireStroke))
            || (hitRules.canHitFill && (style()->svgStyle().hasFill() || !hitRules.requireFill))) {
            FloatPoint localPoint;
            if (!SVGRenderSupport::transformToUserSpaceAndCheckClipping(this, localToParentTransform(), pointInParent, localPoint))
                return false;

            if (hitRules.canHitBoundingBox && !objectBoundingBox().contains(localPoint))
                return false;

            HitTestLocation hitTestLocation(LayoutPoint(flooredIntPoint(localPoint)));
            return RenderBlock::nodeAtPoint(request, result, hitTestLocation, LayoutPoint(), hitTestAction);
        }
    }

    return false;
}

PositionWithAffinity RenderSVGText::positionForPoint(const LayoutPoint& pointInContents)
{
    RootInlineBox* rootBox = firstRootBox();
    if (!rootBox)
        return createPositionWithAffinity(0, DOWNSTREAM);

    ASSERT(!rootBox->nextRootBox());
    ASSERT(childrenInline());

    InlineBox* closestBox = toSVGRootInlineBox(rootBox)->closestLeafChildForPosition(pointInContents);
    if (!closestBox)
        return createPositionWithAffinity(0, DOWNSTREAM);

    return closestBox->renderer().positionForPoint(LayoutPoint(pointInContents.x(), closestBox->y()));
}

void RenderSVGText::absoluteQuads(Vector<FloatQuad>& quads, bool* wasFixed) const
{
    quads.append(localToAbsoluteQuad(strokeBoundingBox(), 0 /* mode */, wasFixed));
}

void RenderSVGText::paint(PaintInfo& paintInfo, const LayoutPoint&)
{
    if (paintInfo.phase != PaintPhaseForeground
     && paintInfo.phase != PaintPhaseSelection)
         return;

    PaintInfo blockInfo(paintInfo);
    GraphicsContextStateSaver stateSaver(*blockInfo.context, false);
    const AffineTransform& localTransform = localToParentTransform();
    if (!localTransform.isIdentity()) {
        stateSaver.save();
        blockInfo.applyTransform(localTransform, false);
    }
    RenderBlock::paint(blockInfo, LayoutPoint());

    // Paint the outlines, if any
    if (paintInfo.phase == PaintPhaseForeground) {
        blockInfo.phase = PaintPhaseSelfOutline;
        RenderBlock::paint(blockInfo, LayoutPoint());
    }
}

FloatRect RenderSVGText::strokeBoundingBox() const
{
    FloatRect strokeBoundaries = objectBoundingBox();
    const SVGRenderStyle& svgStyle = style()->svgStyle();
    if (!svgStyle.hasStroke())
        return strokeBoundaries;

    ASSERT(node());
    ASSERT(node()->isSVGElement());
    SVGLengthContext lengthContext(toSVGElement(node()));
    strokeBoundaries.inflate(svgStyle.strokeWidth()->value(lengthContext));
    return strokeBoundaries;
}

FloatRect RenderSVGText::paintInvalidationRectInLocalCoordinates() const
{
    FloatRect paintInvalidationRect = strokeBoundingBox();
    SVGRenderSupport::intersectPaintInvalidationRectWithResources(this, paintInvalidationRect);

    if (const ShadowList* textShadow = style()->textShadow())
        textShadow->adjustRectForShadow(paintInvalidationRect);

    return paintInvalidationRect;
}

void RenderSVGText::addChild(RenderObject* child, RenderObject* beforeChild)
{
    RenderSVGBlock::addChild(child, beforeChild);

    SVGResourcesCache::clientWasAddedToTree(child, child->style());
    subtreeChildWasAdded(child);
}

void RenderSVGText::removeChild(RenderObject* child)
{
    SVGResourcesCache::clientWillBeRemovedFromTree(child);

    Vector<SVGTextLayoutAttributes*, 2> affectedAttributes;
    FontCachePurgePreventer fontCachePurgePreventer;
    subtreeChildWillBeRemoved(child, affectedAttributes);
    RenderSVGBlock::removeChild(child);
    subtreeChildWasRemoved(affectedAttributes);
}

}
