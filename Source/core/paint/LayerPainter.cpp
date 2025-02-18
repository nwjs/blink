// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/paint/LayerPainter.h"

#include "core/frame/Settings.h"
#include "core/page/Page.h"
#include "core/paint/FilterPainter.h"
#include "core/paint/LayerClipRecorder.h"
#include "core/paint/ScrollableAreaPainter.h"
#include "core/paint/TransformRecorder.h"
#include "core/paint/TransparencyRecorder.h"
#include "core/rendering/ClipPathOperation.h"
#include "core/rendering/FilterEffectRenderer.h"
#include "core/rendering/PaintInfo.h"
#include "core/rendering/RenderBlock.h"
#include "core/rendering/RenderLayer.h"
#include "core/rendering/RenderView.h"
#include "core/rendering/svg/RenderSVGResourceClipper.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/DisplayItemList.h"
#include "platform/graphics/paint/TransformDisplayItem.h"
#include "platform/graphics/paint/TransparencyDisplayItem.h"

namespace blink {

static inline bool shouldSuppressPaintingLayer(RenderLayer* layer)
{
    // Avoid painting descendants of the root layer when stylesheets haven't loaded. This eliminates FOUC.
    // It's ok not to draw, because later on, when all the stylesheets do load, updateStyleSelector on the Document
    // will do a full paintInvalidationForWholeRenderer().
    if (layer->renderer()->document().didLayoutWithPendingStylesheets() && !layer->isRootLayer() && !layer->renderer()->isDocumentElement())
        return true;

    return false;
}

void LayerPainter::paint(GraphicsContext* context, const LayoutRect& damageRect, PaintBehavior paintBehavior, RenderObject* paintingRoot, PaintLayerFlags paintFlags)
{
    LayerPaintingInfo paintingInfo(&m_renderLayer, enclosingIntRect(damageRect), paintBehavior, LayoutSize(), paintingRoot);
    if (shouldPaintLayerInSoftwareMode(paintingInfo, paintFlags))
        paintLayer(context, paintingInfo, paintFlags);
}

static ShouldRespectOverflowClip shouldRespectOverflowClip(PaintLayerFlags paintFlags, const RenderObject* renderer)
{
    return (paintFlags & PaintLayerPaintingOverflowContents || (paintFlags & PaintLayerPaintingChildClippingMaskPhase && renderer->hasClipPath())) ? IgnoreOverflowClip : RespectOverflowClip;
}

void LayerPainter::paintLayer(GraphicsContext* context, const LayerPaintingInfo& paintingInfo, PaintLayerFlags paintFlags)
{
    // https://code.google.com/p/chromium/issues/detail?id=343772
    DisableCompositingQueryAsserts disabler;

    if (m_renderLayer.compositingState() != NotComposited) {
        if (paintingInfo.paintBehavior & PaintBehaviorFlattenCompositingLayers) {
            // FIXME: ok, but what about PaintBehaviorFlattenCompositingLayers? That's for printing.
            // FIXME: why isn't the code here global, as opposed to being set on each paintLayer() call?
            paintFlags |= PaintLayerUncachedClipRects;
        }
    }

    // Non self-painting leaf layers don't need to be painted as their renderer() should properly paint itself.
    if (!m_renderLayer.isSelfPaintingLayer() && !m_renderLayer.hasSelfPaintingLayerDescendant())
        return;

    if (shouldSuppressPaintingLayer(&m_renderLayer))
        return;

    // If this layer is totally invisible then there is nothing to paint.
    if (!m_renderLayer.renderer()->opacity())
        return;

    if (m_renderLayer.paintsWithTransparency(paintingInfo.paintBehavior))
        paintFlags |= PaintLayerHaveTransparency;

    // PaintLayerAppliedTransform is used in RenderReplica, to avoid applying the transform twice.
    if (m_renderLayer.paintsWithTransform(paintingInfo.paintBehavior) && !(paintFlags & PaintLayerAppliedTransform)) {
        paintLayerWithTransform(context, paintingInfo, paintFlags);
        return;
    }

    paintLayerContentsAndReflection(context, paintingInfo, paintFlags);
}

void LayerPainter::paintLayerContentsAndReflection(GraphicsContext* context, const LayerPaintingInfo& paintingInfo, PaintLayerFlags paintFlags)
{
    ASSERT(m_renderLayer.isSelfPaintingLayer() || m_renderLayer.hasSelfPaintingLayerDescendant());

    PaintLayerFlags localPaintFlags = paintFlags & ~(PaintLayerAppliedTransform);

    // Paint the reflection first if we have one.
    if (m_renderLayer.reflectionInfo())
        m_renderLayer.reflectionInfo()->paint(context, paintingInfo, localPaintFlags | PaintLayerPaintingReflection);

    localPaintFlags |= PaintLayerPaintingCompositingAllPhases;
    paintLayerContents(context, paintingInfo, localPaintFlags);
}

class ClipPathHelper {
public:
    ClipPathHelper(GraphicsContext* context, const RenderLayer& renderLayer, const LayerPaintingInfo& paintingInfo, LayoutRect& rootRelativeBounds, bool& rootRelativeBoundsComputed,
        const LayoutPoint& offsetFromRoot, PaintLayerFlags paintFlags)
        : m_resourceClipper(0), m_clipStateSaver(*context, false), m_renderLayer(renderLayer), m_context(context)
    {
        RenderStyle* style = renderLayer.renderer()->style();

        // Clip-path, like border radius, must not be applied to the contents of a composited-scrolling container.
        // It must, however, still be applied to the mask layer, so that the compositor can properly mask the
        // scrolling contents and scrollbars.
        if (!renderLayer.renderer()->hasClipPath() || !style || (renderLayer.needsCompositedScrolling() && !(paintFlags & PaintLayerPaintingChildClippingMaskPhase)))
            return;

        m_clipperState = RenderSVGResourceClipper::ClipperNotApplied;

        ASSERT(style->clipPath());
        if (style->clipPath()->type() == ClipPathOperation::SHAPE) {
            ShapeClipPathOperation* clipPath = toShapeClipPathOperation(style->clipPath());
            if (clipPath->isValid()) {
                m_clipStateSaver.save();

                if (!rootRelativeBoundsComputed) {
                    rootRelativeBounds = renderLayer.physicalBoundingBoxIncludingReflectionAndStackingChildren(paintingInfo.rootLayer, offsetFromRoot);
                    rootRelativeBoundsComputed = true;
                }

                context->clipPath(clipPath->path(rootRelativeBounds), clipPath->windRule());
            }
        } else if (style->clipPath()->type() == ClipPathOperation::REFERENCE) {
            ReferenceClipPathOperation* referenceClipPathOperation = toReferenceClipPathOperation(style->clipPath());
            Document& document = renderLayer.renderer()->document();
            // FIXME: It doesn't work with forward or external SVG references (https://bugs.webkit.org/show_bug.cgi?id=90405)
            Element* element = document.getElementById(referenceClipPathOperation->fragment());
            if (isSVGClipPathElement(element) && element->renderer()) {
                // FIXME: Saving at this point is not required in the 'mask'-
                // case, or if the clip ends up empty.
                m_clipStateSaver.save();
                if (!rootRelativeBoundsComputed) {
                    rootRelativeBounds = renderLayer.physicalBoundingBoxIncludingReflectionAndStackingChildren(paintingInfo.rootLayer, offsetFromRoot);
                    rootRelativeBoundsComputed = true;
                }

                m_resourceClipper = toRenderSVGResourceClipper(toRenderSVGResourceContainer(element->renderer()));
                if (!m_resourceClipper->applyClippingToContext(renderLayer.renderer(), rootRelativeBounds,
                    paintingInfo.paintDirtyRect, context, m_clipperState)) {
                    // No need to post-apply the clipper if this failed.
                    m_resourceClipper = 0;
                }
            }
        }
    }

    ~ClipPathHelper()
    {
        if (m_resourceClipper)
            m_resourceClipper->postApplyStatefulResource(m_renderLayer.renderer(), m_context, m_clipperState);
    }
private:
    RenderSVGResourceClipper* m_resourceClipper;
    GraphicsContextStateSaver m_clipStateSaver;
    RenderSVGResourceClipper::ClipperState m_clipperState;
    const RenderLayer& m_renderLayer;
    GraphicsContext* m_context;
};

void LayerPainter::paintLayerContents(GraphicsContext* context, const LayerPaintingInfo& paintingInfo, PaintLayerFlags paintFlags)
{
    ASSERT(m_renderLayer.isSelfPaintingLayer() || m_renderLayer.hasSelfPaintingLayerDescendant());
    ASSERT(!(paintFlags & PaintLayerAppliedTransform));

    bool isSelfPaintingLayer = m_renderLayer.isSelfPaintingLayer();
    bool isPaintingOverlayScrollbars = paintFlags & PaintLayerPaintingOverlayScrollbars;
    bool isPaintingScrollingContent = paintFlags & PaintLayerPaintingCompositingScrollingPhase;
    bool isPaintingCompositedForeground = paintFlags & PaintLayerPaintingCompositingForegroundPhase;
    bool isPaintingCompositedBackground = paintFlags & PaintLayerPaintingCompositingBackgroundPhase;
    bool isPaintingOverflowContents = paintFlags & PaintLayerPaintingOverflowContents;
    // Outline always needs to be painted even if we have no visible content. Also,
    // the outline is painted in the background phase during composited scrolling.
    // If it were painted in the foreground phase, it would move with the scrolled
    // content. When not composited scrolling, the outline is painted in the
    // foreground phase. Since scrolled contents are moved by paint invalidation in this
    // case, the outline won't get 'dragged along'.
    bool shouldPaintOutline = isSelfPaintingLayer && !isPaintingOverlayScrollbars
        && ((isPaintingScrollingContent && isPaintingCompositedBackground)
        || (!isPaintingScrollingContent && isPaintingCompositedForeground));
    bool shouldPaintContent = m_renderLayer.hasVisibleContent() && isSelfPaintingLayer && !isPaintingOverlayScrollbars;

    if (paintFlags & PaintLayerPaintingRootBackgroundOnly && !m_renderLayer.renderer()->isRenderView() && !m_renderLayer.renderer()->isDocumentElement())
        return;

    // Ensure our lists are up-to-date.
    m_renderLayer.stackingNode()->updateLayerListsIfNeeded();

    LayoutPoint offsetFromRoot;
    m_renderLayer.convertToLayerCoords(paintingInfo.rootLayer, offsetFromRoot);

    if (m_renderLayer.compositingState() == PaintsIntoOwnBacking)
        offsetFromRoot.move(m_renderLayer.subpixelAccumulation());

    LayoutRect rootRelativeBounds;
    bool rootRelativeBoundsComputed = false;

    // These helpers output clip and transparency layers using a RAII pattern. Stack-allocated-varibles are destructed in the reverse order of construction,
    // so they are nested properly.
    ClipPathHelper clipPathHelper(context, m_renderLayer, paintingInfo, rootRelativeBounds, rootRelativeBoundsComputed, offsetFromRoot, paintFlags);

    OwnPtr<TransparencyRecorder> transparencyRecorder;
    OwnPtr<LayerClipRecorder> clipRecorder;
    // Blending operations must be performed only with the nearest ancestor stacking context.
    // Note that there is no need to create a transparency layer if we're painting the root.
    // FIXME: this should be unified further into RenderLayer::paintsWithTransparency().
    bool shouldUseTransparencyLayerForBlendMode = (!m_renderLayer.renderer()->isDocumentElement() || m_renderLayer.renderer()->isSVGRoot()) && m_renderLayer.stackingNode()->isStackingContext() && m_renderLayer.hasNonIsolatedDescendantWithBlendMode();
    if (shouldUseTransparencyLayerForBlendMode || m_renderLayer.paintsWithTransparency(paintingInfo.paintBehavior)) {
        clipRecorder = adoptPtr(new LayerClipRecorder(m_renderLayer.renderer(), context, DisplayItem::TransparencyClip,
            m_renderLayer.paintingExtent(paintingInfo.rootLayer, paintingInfo.paintDirtyRect, paintingInfo.subPixelAccumulation, paintingInfo.paintBehavior),
            &paintingInfo, LayoutPoint(), paintFlags));

        transparencyRecorder = adoptPtr(new TransparencyRecorder(context, m_renderLayer.renderer()->displayItemClient(),
            context->compositeOperation(), m_renderLayer.renderer()->style()->blendMode(), m_renderLayer.renderer()->opacity(), context->compositeOperation()));
    }

    LayerPaintingInfo localPaintingInfo(paintingInfo);

    LayerFragments layerFragments;
    if (shouldPaintContent || shouldPaintOutline || isPaintingOverlayScrollbars) {
        // Collect the fragments. This will compute the clip rectangles and paint offsets for each layer fragment.
        m_renderLayer.collectFragments(layerFragments, localPaintingInfo.rootLayer, localPaintingInfo.paintDirtyRect,
            (paintFlags & PaintLayerUncachedClipRects) ? UncachedClipRects : PaintingClipRects, IgnoreOverlayScrollbarSize,
            shouldRespectOverflowClip(paintFlags, m_renderLayer.renderer()), &offsetFromRoot, localPaintingInfo.subPixelAccumulation);
        if (shouldPaintContent)
            shouldPaintContent = atLeastOneFragmentIntersectsDamageRect(layerFragments, localPaintingInfo, paintFlags, offsetFromRoot);
    }

    bool selectionOnly = localPaintingInfo.paintBehavior & PaintBehaviorSelectionOnly;
    // If this layer's renderer is a child of the paintingRoot, we render unconditionally, which
    // is done by passing a nil paintingRoot down to our renderer (as if no paintingRoot was ever set).
    // Else, our renderer tree may or may not contain the painting root, so we pass that root along
    // so it will be tested against as we descend through the renderers.
    RenderObject* paintingRootForRenderer = 0;
    if (localPaintingInfo.paintingRoot && !m_renderLayer.renderer()->isDescendantOf(localPaintingInfo.paintingRoot))
        paintingRootForRenderer = localPaintingInfo.paintingRoot;

    { // Begin block for the lifetime of any filter.
        FilterPainter filterPainter(m_renderLayer, context, offsetFromRoot, layerFragments.isEmpty() ? ClipRect() : layerFragments[0].backgroundRect, localPaintingInfo, paintFlags,
            rootRelativeBounds, rootRelativeBoundsComputed);

        ASSERT(!(localPaintingInfo.paintBehavior & PaintBehaviorForceBlackText));

        bool shouldPaintBackground = isPaintingCompositedBackground && shouldPaintContent && !selectionOnly;
        bool shouldPaintNegZOrderList = (isPaintingScrollingContent && isPaintingOverflowContents) || (!isPaintingScrollingContent && isPaintingCompositedBackground);
        bool shouldPaintOwnContents = isPaintingCompositedForeground && shouldPaintContent;
        bool shouldPaintNormalFlowAndPosZOrderLists = isPaintingCompositedForeground;
        bool shouldPaintOverlayScrollbars = isPaintingOverlayScrollbars;

        PaintBehavior paintBehavior = PaintBehaviorNormal;
        if (paintFlags & PaintLayerPaintingSkipRootBackground)
            paintBehavior |= PaintBehaviorSkipRootBackground;
        else if (paintFlags & PaintLayerPaintingRootBackgroundOnly)
            paintBehavior |= PaintBehaviorRootBackgroundOnly;

        if (shouldPaintBackground) {
            paintBackgroundForFragments(layerFragments, context, paintingInfo.paintDirtyRect,
                localPaintingInfo, paintBehavior, paintingRootForRenderer, paintFlags);
        }

        if (shouldPaintNegZOrderList)
            paintChildren(NegativeZOrderChildren, context, paintingInfo, paintFlags);

        if (shouldPaintOwnContents) {
            paintForegroundForFragments(layerFragments, context, paintingInfo.paintDirtyRect,
                localPaintingInfo, paintBehavior, paintingRootForRenderer, selectionOnly, paintFlags);
        }

        if (shouldPaintOutline)
            paintOutlineForFragments(layerFragments, context, localPaintingInfo, paintBehavior, paintingRootForRenderer, paintFlags);

        if (shouldPaintNormalFlowAndPosZOrderLists)
            paintChildren(NormalFlowChildren | PositiveZOrderChildren, context, paintingInfo, paintFlags);

        if (shouldPaintOverlayScrollbars)
            paintOverflowControlsForFragments(layerFragments, context, localPaintingInfo, paintFlags);
    } // FilterPainter block

    bool shouldPaintMask = (paintFlags & PaintLayerPaintingCompositingMaskPhase) && shouldPaintContent && m_renderLayer.renderer()->hasMask() && !selectionOnly;
    bool shouldPaintClippingMask = (paintFlags & PaintLayerPaintingChildClippingMaskPhase) && shouldPaintContent && !selectionOnly;

    if (shouldPaintMask)
        paintMaskForFragments(layerFragments, context, localPaintingInfo, paintingRootForRenderer, paintFlags);
    if (shouldPaintClippingMask) {
        // Paint the border radius mask for the fragments.
        paintChildClippingMaskForFragments(layerFragments, context, localPaintingInfo, paintingRootForRenderer, paintFlags);
    }
}

bool LayerPainter::needsToClip(const LayerPaintingInfo& localPaintingInfo, const ClipRect& clipRect)
{
    return clipRect.rect() != localPaintingInfo.paintDirtyRect || clipRect.hasRadius();
}

bool LayerPainter::atLeastOneFragmentIntersectsDamageRect(LayerFragments& fragments, const LayerPaintingInfo& localPaintingInfo, PaintLayerFlags localPaintFlags, const LayoutPoint& offsetFromRoot)
{
    if (m_renderLayer.enclosingPaginationLayer())
        return true; // The fragments created have already been found to intersect with the damage rect.

    if (&m_renderLayer == localPaintingInfo.rootLayer && (localPaintFlags & PaintLayerPaintingOverflowContents))
        return true;

    for (LayerFragment& fragment: fragments) {
        LayoutPoint newOffsetFromRoot = offsetFromRoot + fragment.paginationOffset;
        // Note that this really only works reliably on the first fragment. If the layer has visible
        // overflow and a subsequent fragment doesn't intersect with the border box of the layer
        // (i.e. only contains an overflow portion of the layer), intersection will fail. The reason
        // for this is that fragment.layerBounds is set to the border box, not the bounding box, of
        // the layer.
        if (m_renderLayer.intersectsDamageRect(fragment.layerBounds, fragment.backgroundRect.rect(), localPaintingInfo.rootLayer, &newOffsetFromRoot))
            return true;
    }
    return false;
}

void LayerPainter::paintLayerWithTransform(GraphicsContext* context, const LayerPaintingInfo& paintingInfo, PaintLayerFlags paintFlags)
{
    TransformationMatrix layerTransform = m_renderLayer.renderableTransform(paintingInfo.paintBehavior);
    // If the transform can't be inverted, then don't paint anything.
    if (!layerTransform.isInvertible())
        return;

    // FIXME: We should make sure that we don't walk past paintingInfo.rootLayer here.
    // m_renderLayer may be the "root", and then we should avoid looking at its parent.
    RenderLayer* parentLayer = m_renderLayer.parent();

    ClipRect clipRect(LayoutRect::infiniteRect());
    if (parentLayer) {
        // Calculate the clip rectangle that the ancestors establish.
        ClipRectsContext clipRectsContext(paintingInfo.rootLayer, (paintFlags & PaintLayerUncachedClipRects) ? UncachedClipRects : PaintingClipRects, IgnoreOverlayScrollbarSize);
        if (shouldRespectOverflowClip(paintFlags, m_renderLayer.renderer()) == IgnoreOverflowClip)
            clipRectsContext.setIgnoreOverflowClip();
        clipRect = m_renderLayer.clipper().backgroundClipRect(clipRectsContext);
    }

    RenderLayer* paginationLayer = m_renderLayer.enclosingPaginationLayer();
    LayerFragments fragments;
    if (paginationLayer) {
        // FIXME: This is a mess. Look closely at this code and the code in RenderLayer and fix any
        // issues in it & refactor to make it obvious from code structure what it does and that it's
        // correct.
        ClipRectsCacheSlot cacheSlot = (paintFlags & PaintLayerUncachedClipRects) ? UncachedClipRects : PaintingClipRects;
        ShouldRespectOverflowClip respectOverflowClip = shouldRespectOverflowClip(paintFlags, m_renderLayer.renderer());
        // Calculate the transformed bounding box in the current coordinate space, to figure out
        // which fragmentainers (e.g. columns) we need to visit.
        LayoutRect transformedExtent = RenderLayer::transparencyClipBox(&m_renderLayer, paginationLayer, RenderLayer::PaintingTransparencyClipBox, RenderLayer::RootOfTransparencyClipBox, paintingInfo.subPixelAccumulation, paintingInfo.paintBehavior);
        // FIXME: we don't check if paginationLayer is within paintingInfo.rootLayer here.
        paginationLayer->collectFragments(fragments, paintingInfo.rootLayer, paintingInfo.paintDirtyRect, cacheSlot, IgnoreOverlayScrollbarSize, respectOverflowClip, 0, paintingInfo.subPixelAccumulation, &transformedExtent);
    } else {
        // We don't need to collect any fragments in the regular way here. We have already
        // calculated a clip rectangle for the ancestry if it was needed, and clipping this
        // layer is something that can be done further down the path, when the transform has
        // been applied.
        LayerFragment fragment;
        fragment.backgroundRect = paintingInfo.paintDirtyRect;
        fragments.append(fragment);
    }

    for (const auto& fragment: fragments) {
        OwnPtr<LayerClipRecorder> clipRecorder;
        if (parentLayer) {
            ClipRect clipRectForFragment(clipRect);
            clipRectForFragment.moveBy(fragment.paginationOffset);
            clipRectForFragment.intersect(fragment.backgroundRect);
            if (clipRectForFragment.isEmpty())
                continue;
            if (needsToClip(paintingInfo, clipRectForFragment))
                clipRecorder = adoptPtr(new LayerClipRecorder(parentLayer->renderer(), context, DisplayItem::ClipLayerParent, clipRectForFragment, &paintingInfo, fragment.paginationOffset, paintFlags));
        }

        paintFragmentByApplyingTransform(context, paintingInfo, paintFlags, fragment.paginationOffset);
    }
}

void LayerPainter::paintFragmentByApplyingTransform(GraphicsContext* context, const LayerPaintingInfo& paintingInfo, PaintLayerFlags paintFlags, const LayoutPoint& fragmentTranslation)
{
    // This involves subtracting out the position of the layer in our current coordinate space, but preserving
    // the accumulated error for sub-pixel layout.
    LayoutPoint delta;
    m_renderLayer.convertToLayerCoords(paintingInfo.rootLayer, delta);
    delta.moveBy(fragmentTranslation);
    TransformationMatrix transform(m_renderLayer.renderableTransform(paintingInfo.paintBehavior));
    IntPoint roundedDelta = roundedIntPoint(delta);
    transform.translateRight(roundedDelta.x(), roundedDelta.y());
    LayoutSize adjustedSubPixelAccumulation = paintingInfo.subPixelAccumulation + (delta - roundedDelta);

    TransformRecorder transformRecorder(*context, m_renderLayer.renderer()->displayItemClient(), transform.toAffineTransform());

    // Now do a paint with the root layer shifted to be us.
    LayerPaintingInfo transformedPaintingInfo(&m_renderLayer, enclosingIntRect(transform.inverse().mapRect(paintingInfo.paintDirtyRect)), paintingInfo.paintBehavior,
        adjustedSubPixelAccumulation, paintingInfo.paintingRoot);
    paintLayerContentsAndReflection(context, transformedPaintingInfo, paintFlags);
}

void LayerPainter::paintChildren(unsigned childrenToVisit, GraphicsContext* context, const LayerPaintingInfo& paintingInfo, PaintLayerFlags paintFlags)
{
    if (!m_renderLayer.hasSelfPaintingLayerDescendant())
        return;

#if ENABLE(ASSERT)
    LayerListMutationDetector mutationChecker(m_renderLayer.stackingNode());
#endif

    RenderLayerStackingNodeIterator iterator(*m_renderLayer.stackingNode(), childrenToVisit);
    while (RenderLayerStackingNode* child = iterator.next()) {
        LayerPainter childPainter(*child->layer());
        // If this RenderLayer should paint into its own backing or a grouped backing, that will be done via CompositedLayerMapping::paintContents()
        // and CompositedLayerMapping::doPaintTask().
        if (!childPainter.shouldPaintLayerInSoftwareMode(paintingInfo, paintFlags))
            continue;

        if (!child->layer()->isPaginated())
            childPainter.paintLayer(context, paintingInfo, paintFlags);
        else
            childPainter.paintPaginatedChildLayer(child->layer(), context, paintingInfo, paintFlags);
    }
}

// FIXME: inline this.
static bool paintForFixedRootBackground(const RenderLayer* layer, PaintLayerFlags paintFlags)
{
    return layer->renderer()->isDocumentElement() && (paintFlags & PaintLayerPaintingRootBackgroundOnly);
}

bool LayerPainter::shouldPaintLayerInSoftwareMode(const LayerPaintingInfo& paintingInfo, PaintLayerFlags paintFlags)
{
    DisableCompositingQueryAsserts disabler;

    return m_renderLayer.compositingState() == NotComposited
        || (paintingInfo.paintBehavior & PaintBehaviorFlattenCompositingLayers)
        || ((paintFlags & PaintLayerPaintingReflection) && !m_renderLayer.has3DTransform())
        || paintForFixedRootBackground(&m_renderLayer, paintFlags);
}

static inline LayoutSize subPixelAccumulationIfNeeded(const LayoutSize& subPixelAccumulation, CompositingState compositingState)
{
    // Only apply the sub-pixel accumulation if we don't paint into our own backing layer, otherwise the position
    // of the renderer already includes any sub-pixel offset.
    if (compositingState == PaintsIntoOwnBacking)
        return LayoutSize();
    return subPixelAccumulation;
}

void LayerPainter::paintOverflowControlsForFragments(const LayerFragments& layerFragments, GraphicsContext* context, const LayerPaintingInfo& localPaintingInfo, PaintLayerFlags paintFlags)
{
    for (size_t i = 0; i < layerFragments.size(); ++i) {
        const LayerFragment& fragment = layerFragments.at(i);

        OwnPtr<LayerClipRecorder> clipRecorder;

        if (needsToClip(localPaintingInfo, fragment.backgroundRect)) {
            clipRecorder = adoptPtr(new LayerClipRecorder(m_renderLayer.renderer(), context, DisplayItem::ClipLayerOverflowControls, fragment.backgroundRect, &localPaintingInfo, fragment.paginationOffset, paintFlags));
        }
        if (RenderLayerScrollableArea* scrollableArea = m_renderLayer.scrollableArea())
            ScrollableAreaPainter(*scrollableArea).paintOverflowControls(context, roundedIntPoint(toPoint(fragment.layerBounds.location() - m_renderLayer.renderBoxLocation() + subPixelAccumulationIfNeeded(localPaintingInfo.subPixelAccumulation, m_renderLayer.compositingState()))), pixelSnappedIntRect(fragment.backgroundRect.rect()), true);
    }
}

static bool checkContainingBlockChainForPagination(RenderLayerModelObject* renderer, RenderBox* ancestorColumnsRenderer)
{
    RenderView* view = renderer->view();
    RenderLayerModelObject* prevBlock = renderer;
    RenderBlock* containingBlock;
    for (containingBlock = renderer->containingBlock();
        containingBlock && containingBlock != view && containingBlock != ancestorColumnsRenderer;
        containingBlock = containingBlock->containingBlock())
        prevBlock = containingBlock;

    // If the columns block wasn't in our containing block chain, then we aren't paginated by it.
    if (containingBlock != ancestorColumnsRenderer)
        return false;

    // If the previous block is absolutely positioned, then we can't be paginated by the columns block.
    if (prevBlock->isOutOfFlowPositioned())
        return false;

    // Otherwise we are paginated by the columns block.
    return true;
}

void LayerPainter::paintPaginatedChildLayer(RenderLayer* childLayer, GraphicsContext* context, const LayerPaintingInfo& paintingInfo, PaintLayerFlags paintFlags)
{
    // We need to do multiple passes, breaking up our child layer into strips.
    Vector<RenderLayer*> columnLayers;
    RenderLayerStackingNode* ancestorNode = m_renderLayer.stackingNode()->isNormalFlowOnly() ? m_renderLayer.parent()->stackingNode() : m_renderLayer.stackingNode()->ancestorStackingContextNode();
    for (RenderLayer* curr = childLayer->parent(); curr; curr = curr->parent()) {
        if (curr->renderer()->hasColumns() && checkContainingBlockChainForPagination(childLayer->renderer(), curr->renderBox()))
            columnLayers.append(curr);
        if (curr->stackingNode() == ancestorNode)
            break;
    }

    // It is possible for paintLayer() to be called after the child layer ceases to be paginated but before
    // updatePaginationRecusive() is called and resets the isPaginated() flag, see <rdar://problem/10098679>.
    // If this is the case, just bail out, since the upcoming call to updatePaginationRecusive() will paint invalidate the layer.
    // FIXME: Is this true anymore? This seems very suspicious.
    if (!columnLayers.size())
        return;

    paintChildLayerIntoColumns(childLayer, context, paintingInfo, paintFlags, columnLayers, columnLayers.size() - 1);
}

void LayerPainter::paintChildLayerIntoColumns(RenderLayer* childLayer, GraphicsContext* context, const LayerPaintingInfo& paintingInfo,
    PaintLayerFlags paintFlags, const Vector<RenderLayer*>& columnLayers, size_t colIndex)
{
    RenderBlock* columnBlock = toRenderBlock(columnLayers[colIndex]->renderer());

    ASSERT(columnBlock && columnBlock->hasColumns());
    if (!columnBlock || !columnBlock->hasColumns())
        return;

    LayoutPoint layerOffset;
    // FIXME: It looks suspicious to call convertToLayerCoords here
    // as canUseConvertToLayerCoords is true for this layer.
    columnBlock->layer()->convertToLayerCoords(paintingInfo.rootLayer, layerOffset);

    bool isHorizontal = columnBlock->style()->isHorizontalWritingMode();

    ColumnInfo* colInfo = columnBlock->columnInfo();
    unsigned colCount = columnBlock->columnCount(colInfo);
    LayoutUnit currLogicalTopOffset = 0;
    for (unsigned i = 0; i < colCount; i++) {
        // For each rect, we clip to the rect, and then we adjust our coords.
        LayoutRect colRect = columnBlock->columnRectAt(colInfo, i);
        columnBlock->flipForWritingMode(colRect);
        LayoutUnit logicalLeftOffset = (isHorizontal ? colRect.x() : colRect.y()) - columnBlock->logicalLeftOffsetForContent();
        LayoutSize offset;
        if (isHorizontal) {
            if (colInfo->progressionAxis() == ColumnInfo::InlineAxis)
                offset = LayoutSize(logicalLeftOffset, currLogicalTopOffset);
            else
                offset = LayoutSize(0, colRect.y() + currLogicalTopOffset - columnBlock->borderTop() - columnBlock->paddingTop());
        } else {
            if (colInfo->progressionAxis() == ColumnInfo::InlineAxis)
                offset = LayoutSize(currLogicalTopOffset, logicalLeftOffset);
            else
                offset = LayoutSize(colRect.x() + currLogicalTopOffset - columnBlock->borderLeft() - columnBlock->paddingLeft(), 0);
        }

        colRect.moveBy(layerOffset);

        LayoutRect localDirtyRect(paintingInfo.paintDirtyRect);
        localDirtyRect.intersect(colRect);

        if (!localDirtyRect.isEmpty()) {
            GraphicsContextStateSaver stateSaver(*context);

            // Each strip pushes a clip, since column boxes are specified as being
            // like overflow:hidden.
            context->clip(enclosingIntRect(colRect));

            if (!colIndex) {
                // Apply a translation transform to change where the layer paints.
                TransformationMatrix oldTransform;
                bool oldHasTransform = childLayer->transform();
                if (oldHasTransform)
                    oldTransform = *childLayer->transform();
                TransformationMatrix newTransform(oldTransform);
                newTransform.translateRight(roundToInt(offset.width()), roundToInt(offset.height()));

                childLayer->setTransform(adoptPtr(new TransformationMatrix(newTransform)));

                LayerPaintingInfo localPaintingInfo(paintingInfo);
                localPaintingInfo.paintDirtyRect = localDirtyRect;
                LayerPainter(*childLayer).paintLayer(context, localPaintingInfo, paintFlags);

                if (oldHasTransform)
                    childLayer->setTransform(adoptPtr(new TransformationMatrix(oldTransform)));
                else
                    childLayer->clearTransform();
            } else {
                // Adjust the transform such that the renderer's upper left corner will paint at (0,0) in user space.
                // This involves subtracting out the position of the layer in our current coordinate space.
                LayoutPoint childOffset;
                columnLayers[colIndex - 1]->convertToLayerCoords(paintingInfo.rootLayer, childOffset);
                TransformationMatrix transform;
                transform.translateRight(roundToInt(childOffset.x() + offset.width()), roundToInt(childOffset.y() + offset.height()));

                // Apply the transform.
                context->concatCTM(transform.toAffineTransform());

                // Now do a paint with the root layer shifted to be the next multicol block.
                LayerPaintingInfo columnPaintingInfo(paintingInfo);
                columnPaintingInfo.rootLayer = columnLayers[colIndex - 1];
                columnPaintingInfo.paintDirtyRect = transform.inverse().mapRect(localDirtyRect);
                paintChildLayerIntoColumns(childLayer, context, columnPaintingInfo, paintFlags, columnLayers, colIndex - 1);
            }
        }

        // Move to the next position.
        LayoutUnit blockDelta = isHorizontal ? colRect.height() : colRect.width();
        if (columnBlock->style()->isFlippedBlocksWritingMode())
            currLogicalTopOffset += blockDelta;
        else
            currLogicalTopOffset -= blockDelta;
    }
}

void LayerPainter::paintFragmentWithPhase(PaintPhase phase, const LayerFragment& fragment, GraphicsContext* context, const ClipRect& clipRect, const LayerPaintingInfo& paintingInfo, PaintBehavior paintBehavior, RenderObject* paintingRootForRenderer, PaintLayerFlags paintFlags, ClipState clipState)
{
    OwnPtr<LayerClipRecorder> clipRecorder;
    if (clipState != HasClipped && paintingInfo.clipToDirtyRect && needsToClip(paintingInfo, clipRect)) {
        LayerClipRecorder::BorderRadiusClippingRule clippingRule = LayerClipRecorder::IncludeSelfForBorderRadius;
        DisplayItem::Type clipType = DisplayItem::ClipLayerFragmentFloat;
        switch (phase) {
        case PaintPhaseFloat:
            break;
        case PaintPhaseForeground:
            clipType = DisplayItem::ClipLayerFragmentForeground;
            break;
        case PaintPhaseChildOutlines:
            clipType = DisplayItem::ClipLayerFragmentChildOutline;
            break;
        case PaintPhaseSelection:
            clipType = DisplayItem::ClipLayerFragmentSelection;
            break;
        case PaintPhaseChildBlockBackgrounds:
            clipType = DisplayItem::ClipLayerFragmentChildBlockBackgrounds;
            break;
        case PaintPhaseBlockBackground:
            clipType = DisplayItem::ClipLayerBackground;
            clippingRule = LayerClipRecorder::DoNotIncludeSelfForBorderRadius; // Background painting will handle clipping to self.
            break;
        case PaintPhaseSelfOutline:
            clipType = DisplayItem::ClipLayerFragmentOutline;
            clippingRule = LayerClipRecorder::DoNotIncludeSelfForBorderRadius;
            break;
        case PaintPhaseMask:
            clipType = DisplayItem::ClipLayerFragmentMask;
            clippingRule = LayerClipRecorder::DoNotIncludeSelfForBorderRadius; // Mask painting will handle clipping to self.
            break;
        case PaintPhaseClippingMask:
            clipType = DisplayItem::ClipLayerFragmentClippingMask;
            break;
        default:
            ASSERT_NOT_REACHED();
        }

        clipRecorder = adoptPtr(new LayerClipRecorder(m_renderLayer.renderer(), context, clipType, clipRect, &paintingInfo, fragment.paginationOffset, paintFlags, clippingRule));
    }

    PaintInfo paintInfo(context, pixelSnappedIntRect(clipRect.rect()), phase, paintBehavior, paintingRootForRenderer, 0, paintingInfo.rootLayer->renderer());
    m_renderLayer.renderer()->paint(paintInfo, toPoint(fragment.layerBounds.location() - m_renderLayer.renderBoxLocation() + subPixelAccumulationIfNeeded(paintingInfo.subPixelAccumulation, m_renderLayer.compositingState())));
}

void LayerPainter::paintBackgroundForFragments(const LayerFragments& layerFragments, GraphicsContext* context,
    const LayoutRect& transparencyPaintDirtyRect, const LayerPaintingInfo& localPaintingInfo, PaintBehavior paintBehavior,
    RenderObject* paintingRootForRenderer, PaintLayerFlags paintFlags)
{
    for (const auto& fragment: layerFragments) {
        paintFragmentWithPhase(PaintPhaseBlockBackground, fragment, context, fragment.backgroundRect, localPaintingInfo, paintBehavior, paintingRootForRenderer, paintFlags, HasNotClipped);
    }
}

void LayerPainter::paintForegroundForFragments(const LayerFragments& layerFragments, GraphicsContext* context,
    const LayoutRect& transparencyPaintDirtyRect, const LayerPaintingInfo& localPaintingInfo, PaintBehavior paintBehavior,
    RenderObject* paintingRootForRenderer, bool selectionOnly, PaintLayerFlags paintFlags)
{
    // Optimize clipping for the single fragment case.
    bool shouldClip = localPaintingInfo.clipToDirtyRect && layerFragments.size() == 1 && !layerFragments[0].foregroundRect.isEmpty();
    ClipState clipState = HasNotClipped;
    OwnPtr<LayerClipRecorder> clipRecorder;
    if (shouldClip && needsToClip(localPaintingInfo, layerFragments[0].foregroundRect)) {
        clipRecorder = adoptPtr(new LayerClipRecorder(m_renderLayer.renderer(), context, DisplayItem::ClipLayerForeground, layerFragments[0].foregroundRect, &localPaintingInfo, layerFragments[0].paginationOffset, paintFlags));
        clipState = HasClipped;
    }

    // We have to loop through every fragment multiple times, since we have to issue paint invalidations in each specific phase in order for
    // interleaving of the fragments to work properly.
    paintForegroundForFragmentsWithPhase(selectionOnly ? PaintPhaseSelection : PaintPhaseChildBlockBackgrounds, layerFragments,
        context, localPaintingInfo, paintBehavior, paintingRootForRenderer, paintFlags, clipState);

    if (!selectionOnly) {
        paintForegroundForFragmentsWithPhase(PaintPhaseFloat, layerFragments, context, localPaintingInfo, paintBehavior, paintingRootForRenderer, paintFlags, clipState);
        paintForegroundForFragmentsWithPhase(PaintPhaseForeground, layerFragments, context, localPaintingInfo, paintBehavior, paintingRootForRenderer, paintFlags, clipState);
        paintForegroundForFragmentsWithPhase(PaintPhaseChildOutlines, layerFragments, context, localPaintingInfo, paintBehavior, paintingRootForRenderer, paintFlags, clipState);
    }
}

void LayerPainter::paintForegroundForFragmentsWithPhase(PaintPhase phase, const LayerFragments& layerFragments, GraphicsContext* context,
    const LayerPaintingInfo& localPaintingInfo, PaintBehavior paintBehavior, RenderObject* paintingRootForRenderer, PaintLayerFlags paintFlags, ClipState clipState)
{
    for (const auto& fragment: layerFragments) {
        if (!fragment.foregroundRect.isEmpty())
            paintFragmentWithPhase(phase, fragment, context, fragment.foregroundRect, localPaintingInfo, paintBehavior, paintingRootForRenderer, paintFlags, clipState);
    }
}

void LayerPainter::paintOutlineForFragments(const LayerFragments& layerFragments, GraphicsContext* context, const LayerPaintingInfo& localPaintingInfo,
    PaintBehavior paintBehavior, RenderObject* paintingRootForRenderer, PaintLayerFlags paintFlags)
{
    for (const auto& fragment: layerFragments) {
        if (!fragment.outlineRect.isEmpty())
            paintFragmentWithPhase(PaintPhaseSelfOutline, fragment, context, fragment.outlineRect, localPaintingInfo, paintBehavior, paintingRootForRenderer, paintFlags, HasNotClipped);
    }
}

void LayerPainter::paintMaskForFragments(const LayerFragments& layerFragments, GraphicsContext* context, const LayerPaintingInfo& localPaintingInfo,
    RenderObject* paintingRootForRenderer, PaintLayerFlags paintFlags)
{
    for (const auto& fragment: layerFragments)
        paintFragmentWithPhase(PaintPhaseMask, fragment, context, fragment.backgroundRect, localPaintingInfo, PaintBehaviorNormal, paintingRootForRenderer, paintFlags, HasNotClipped);
}

void LayerPainter::paintChildClippingMaskForFragments(const LayerFragments& layerFragments, GraphicsContext* context, const LayerPaintingInfo& localPaintingInfo,
    RenderObject* paintingRootForRenderer, PaintLayerFlags paintFlags)
{
    for (const auto& fragment: layerFragments)
        paintFragmentWithPhase(PaintPhaseClippingMask, fragment, context, fragment.foregroundRect, localPaintingInfo, PaintBehaviorNormal, paintingRootForRenderer, paintFlags, HasNotClipped);
}

void LayerPainter::paintOverlayScrollbars(GraphicsContext* context, const LayoutRect& damageRect, PaintBehavior paintBehavior, RenderObject* paintingRoot)
{
    if (!m_renderLayer.containsDirtyOverlayScrollbars())
        return;

    LayerPaintingInfo paintingInfo(&m_renderLayer, enclosingIntRect(damageRect), paintBehavior, LayoutSize(), paintingRoot);
    paintLayer(context, paintingInfo, PaintLayerPaintingOverlayScrollbars);

    m_renderLayer.setContainsDirtyOverlayScrollbars(false);
}

} // namespace blink
