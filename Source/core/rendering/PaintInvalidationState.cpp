// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/rendering/PaintInvalidationState.h"

#include "core/rendering/RenderInline.h"
#include "core/rendering/RenderLayer.h"
#include "core/rendering/RenderView.h"
#include "core/rendering/svg/RenderSVGModelObject.h"
#include "platform/Partitions.h"

namespace blink {

PaintInvalidationState::PaintInvalidationState(RenderObject& renderer)
    : m_clipped(false)
    , m_cachedOffsetsEnabled(true)
    , m_forceCheckForPaintInvalidation(false)
    , m_paintInvalidationContainer(*renderer.containerForPaintInvalidation())
    , m_renderer(renderer)
{
    bool establishesPaintInvalidationContainer = &m_renderer == &m_paintInvalidationContainer;
    if (!establishesPaintInvalidationContainer) {
        if (!renderer.supportsPaintInvalidationStateCachedOffsets()) {
            m_cachedOffsetsEnabled = false;
            return;
        }
        bool invalidationContainerSkipped;
        RenderObject* container = renderer.container(&m_paintInvalidationContainer, &invalidationContainerSkipped);
        if (container && !invalidationContainerSkipped) {
            FloatPoint point = container->localToContainerPoint(FloatPoint(), &m_paintInvalidationContainer);
            if (container->isTableRow())
                point = FloatPoint(point.x() - toRenderBox(container)->x().toFloat(), point.y() - toRenderBox(container)->y().toFloat());
            m_paintOffset = LayoutSize(point.x(), point.y());

            applyClipIfNeeded(*container);
        }
    } else {
        applyClipIfNeeded(m_renderer);
    }
}

PaintInvalidationState::PaintInvalidationState(const PaintInvalidationState& next, RenderLayerModelObject& renderer, const RenderLayerModelObject& paintInvalidationContainer)
    : m_clipped(false)
    , m_cachedOffsetsEnabled(true)
    , m_forceCheckForPaintInvalidation(next.m_forceCheckForPaintInvalidation)
    , m_paintInvalidationContainer(paintInvalidationContainer)
    , m_renderer(renderer)
{
    // FIXME: SVG could probably benefit from a stack-based optimization like html does. crbug.com/391054
    bool establishesPaintInvalidationContainer = &m_renderer == &m_paintInvalidationContainer;
    bool fixed = m_renderer.isOutOfFlowPositioned() && m_renderer.style()->position() == FixedPosition;

    if (establishesPaintInvalidationContainer) {
        // When we hit a new paint invalidation container, we don't need to
        // continue forcing a check for paint invalidation because movement
        // from our parents will just move the whole invalidation container.
        m_forceCheckForPaintInvalidation = false;
    } else {
        if (!renderer.supportsPaintInvalidationStateCachedOffsets() || !next.m_cachedOffsetsEnabled) {
            m_cachedOffsetsEnabled = false;
        } else {
            LayoutSize offset = m_renderer.isBox() && !m_renderer.isTableRow() ? toRenderBox(renderer).locationOffset() : LayoutSize();
            if (fixed) {
                // FIXME: This doesn't work correctly with transforms.
                FloatPoint fixedOffset = m_renderer.view()->localToAbsolute(FloatPoint(), IsFixed);
                m_paintOffset = LayoutSize(fixedOffset.x(), fixedOffset.y()) + offset;
            } else {
                m_paintOffset = next.m_paintOffset + offset;
            }

            if (m_renderer.isOutOfFlowPositioned() && !fixed) {
                if (RenderObject* container = m_renderer.container()) {
                    if (container->style()->hasInFlowPosition() && container->isRenderInline())
                        m_paintOffset += toRenderInline(container)->offsetForInFlowPositionedInline(toRenderBox(renderer));
                }
            }

            if (m_renderer.style()->hasInFlowPosition() && renderer.hasLayer())
                m_paintOffset += renderer.layer()->offsetForInFlowPosition();
        }

        m_clipped = !fixed && next.m_clipped;
        if (m_clipped)
            m_clipRect = next.m_clipRect;
    }

    applyClipIfNeeded(renderer);

    // FIXME: <http://bugs.webkit.org/show_bug.cgi?id=13443> Apply control clip if present.
}

void PaintInvalidationState::applyClipIfNeeded(const RenderObject& renderer)
{
    if (!renderer.hasOverflowClip())
        return;

    const RenderBox& box = toRenderBox(renderer);

    // Do not clip scroll layer contents because the compositor expects the whole layer
    // to be always invalidated in-time.
    if (box.usesCompositedScrolling()) {
        ASSERT(!m_clipped); // The box should establish paint invalidation container, so no m_clipped inherited.
    } else {
        LayoutRect clipRect(toPoint(m_paintOffset), box.layer()->size());
        if (m_clipped) {
            m_clipRect.intersect(clipRect);
        } else {
            m_clipRect = clipRect;
            m_clipped = true;
        }
    }

    m_paintOffset -= box.scrolledContentOffset();
}

} // namespace blink
