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

#include "config.h"
#include "web/PageScaleConstraintsSet.h"

#include "platform/Length.h"
#include "wtf/Assertions.h"

namespace blink {

static const float defaultMinimumScale = 0.25f;
static const float defaultMaximumScale = 5.0f;

PageScaleConstraintsSet::PageScaleConstraintsSet()
    : m_finalConstraints(1, 1, 1)
    , m_lastContentsWidth(0)
    , m_needsReset(false)
    , m_constraintsDirty(false)
{
}

PageScaleConstraints PageScaleConstraintsSet::defaultConstraints() const
{
    return PageScaleConstraints(-1, defaultMinimumScale, defaultMaximumScale);
}

void PageScaleConstraintsSet::updatePageDefinedConstraints(const ViewportDescription& description, Length legacyFallbackWidth)
{
    m_pageDefinedConstraints = description.resolve(m_viewSize, legacyFallbackWidth);

    m_constraintsDirty = true;
}

void PageScaleConstraintsSet::setUserAgentConstraints(const PageScaleConstraints& userAgentConstraints)
{
    m_userAgentConstraints = userAgentConstraints;
    m_constraintsDirty = true;
}

PageScaleConstraints PageScaleConstraintsSet::computeConstraintsStack() const
{
    PageScaleConstraints constraints = defaultConstraints();
    constraints.overrideWith(m_pageDefinedConstraints);
    constraints.overrideWith(m_userAgentConstraints);
    return constraints;
}

void PageScaleConstraintsSet::computeFinalConstraints()
{
    m_finalConstraints = computeConstraintsStack();

    m_constraintsDirty = false;
}

void PageScaleConstraintsSet::adjustFinalConstraintsToContentsSize(IntSize contentsSize, int nonOverlayScrollbarWidth)
{
    m_finalConstraints.fitToContentsWidth(contentsSize.width(), m_viewSize.width() - nonOverlayScrollbarWidth);
}

void PageScaleConstraintsSet::setNeedsReset(bool needsReset)
{
    m_needsReset = needsReset;
    if (needsReset)
        m_constraintsDirty = true;
}

void PageScaleConstraintsSet::didChangeContentsSize(IntSize contentsSize, float pageScaleFactor)
{
    // If a large fixed-width element expanded the size of the document late in
    // loading and our initial scale is not set (or set to be less than the last
    // minimum scale), reset the page scale factor to the new initial scale.
    if (contentsSize.width() > m_lastContentsWidth
        && pageScaleFactor == finalConstraints().minimumScale
        && computeConstraintsStack().initialScale < finalConstraints().minimumScale)
        setNeedsReset(true);

    m_constraintsDirty = true;
    m_lastContentsWidth = contentsSize.width();
}

static float computeDeprecatedTargetDensityDPIFactor(const ViewportDescription& description, float deviceScaleFactor)
{
    if (description.deprecatedTargetDensityDPI == ViewportDescription::ValueDeviceDPI)
        return 1.0f / deviceScaleFactor;

    float targetDPI = -1.0f;
    if (description.deprecatedTargetDensityDPI == ViewportDescription::ValueLowDPI)
        targetDPI = 120.0f;
    else if (description.deprecatedTargetDensityDPI == ViewportDescription::ValueMediumDPI)
        targetDPI = 160.0f;
    else if (description.deprecatedTargetDensityDPI == ViewportDescription::ValueHighDPI)
        targetDPI = 240.0f;
    else if (description.deprecatedTargetDensityDPI != ViewportDescription::ValueAuto)
        targetDPI = description.deprecatedTargetDensityDPI;
    return targetDPI > 0 ? 160.0f / targetDPI : 1.0f;
}

static float getLayoutWidthForNonWideViewport(const FloatSize& deviceSize, float initialScale)
{
    return initialScale == -1 ? deviceSize.width() : deviceSize.width() / initialScale;
}

static float computeHeightByAspectRatio(float width, const FloatSize& deviceSize)
{
    return width * (deviceSize.height() / deviceSize.width());
}

void PageScaleConstraintsSet::didChangeViewSize(const IntSize& size)
{
    if (m_viewSize == size)
        return;

    m_viewSize = size;
    m_constraintsDirty = true;
}

IntSize PageScaleConstraintsSet::mainFrameSize(const IntSize& contentsSize) const
{
    // If there's no explicit minimum scale factor set, size the frame so that its width == content width
    // so there's no horizontal scrolling at the minimum scale.
    if (m_pageDefinedConstraints.minimumScale < finalConstraints().minimumScale
        && m_userAgentConstraints.minimumScale < finalConstraints().minimumScale
        && contentsSize.width()
        && m_viewSize.width())
        return IntSize(contentsSize.width(), computeHeightByAspectRatio(contentsSize.width(), m_viewSize));

    // If there is a minimum scale (or there's no content size yet), the frame size should match the viewport
    // size at minimum scale, since the viewport must always be contained by the frame.
    IntSize frameSize(m_viewSize);
    frameSize.scale(1 / finalConstraints().minimumScale);
    return frameSize;
}

void PageScaleConstraintsSet::adjustForAndroidWebViewQuirks(const ViewportDescription& description, int layoutFallbackWidth, float deviceScaleFactor, bool supportTargetDensityDPI, bool wideViewportQuirkEnabled, bool useWideViewport, bool loadWithOverviewMode, bool nonUserScalableQuirkEnabled)
{
    if (!supportTargetDensityDPI && !wideViewportQuirkEnabled && loadWithOverviewMode && !nonUserScalableQuirkEnabled)
        return;

    const float oldInitialScale = m_pageDefinedConstraints.initialScale;
    if (!loadWithOverviewMode) {
        bool resetInitialScale = false;
        if (description.zoom == -1) {
            if (description.maxWidth.isAuto() || description.maxWidth.type() == ExtendToZoom)
                resetInitialScale = true;
            if (useWideViewport || description.maxWidth.type() == DeviceWidth)
                resetInitialScale = true;
        }
        if (resetInitialScale)
            m_pageDefinedConstraints.initialScale = 1.0f;
    }

    float adjustedLayoutSizeWidth = m_pageDefinedConstraints.layoutSize.width();
    float adjustedLayoutSizeHeight = m_pageDefinedConstraints.layoutSize.height();
    float targetDensityDPIFactor = 1.0f;

    if (supportTargetDensityDPI) {
        targetDensityDPIFactor = computeDeprecatedTargetDensityDPIFactor(description, deviceScaleFactor);
        if (m_pageDefinedConstraints.initialScale != -1)
            m_pageDefinedConstraints.initialScale *= targetDensityDPIFactor;
        if (m_pageDefinedConstraints.minimumScale != -1)
            m_pageDefinedConstraints.minimumScale *= targetDensityDPIFactor;
        if (m_pageDefinedConstraints.maximumScale != -1)
            m_pageDefinedConstraints.maximumScale *= targetDensityDPIFactor;
        if (wideViewportQuirkEnabled && (!useWideViewport || description.maxWidth.type() == DeviceWidth)) {
            adjustedLayoutSizeWidth /= targetDensityDPIFactor;
            adjustedLayoutSizeHeight /= targetDensityDPIFactor;
        }
    }

    if (wideViewportQuirkEnabled) {
        if (useWideViewport && (description.maxWidth.isAuto() || description.maxWidth.type() == ExtendToZoom) && description.zoom != 1.0f) {
            adjustedLayoutSizeWidth = layoutFallbackWidth;
            adjustedLayoutSizeHeight = computeHeightByAspectRatio(adjustedLayoutSizeWidth, m_viewSize);
        } else if (!useWideViewport) {
            const float nonWideScale = description.zoom < 1 && description.maxWidth.type() != DeviceWidth && description.maxWidth.type() != DeviceHeight ? -1 : oldInitialScale;
            adjustedLayoutSizeWidth = getLayoutWidthForNonWideViewport(m_viewSize, nonWideScale) / targetDensityDPIFactor;
            float newInitialScale = targetDensityDPIFactor;
            if (m_userAgentConstraints.initialScale != -1 && (description.maxWidth.type() == DeviceWidth || ((description.maxWidth.isAuto() || description.maxWidth.type() == ExtendToZoom) && description.zoom == -1))) {
                adjustedLayoutSizeWidth /= m_userAgentConstraints.initialScale;
                newInitialScale = m_userAgentConstraints.initialScale;
            }
            adjustedLayoutSizeHeight = computeHeightByAspectRatio(adjustedLayoutSizeWidth, m_viewSize);
            if (description.zoom < 1) {
                m_pageDefinedConstraints.initialScale = newInitialScale;
                if (m_pageDefinedConstraints.minimumScale != -1)
                    m_pageDefinedConstraints.minimumScale = std::min<float>(m_pageDefinedConstraints.minimumScale, m_pageDefinedConstraints.initialScale);
                if (m_pageDefinedConstraints.maximumScale != -1)
                    m_pageDefinedConstraints.maximumScale = std::max<float>(m_pageDefinedConstraints.maximumScale, m_pageDefinedConstraints.initialScale);
            }
        }
    }

    if (nonUserScalableQuirkEnabled && !description.userZoom) {
        m_pageDefinedConstraints.initialScale = targetDensityDPIFactor;
        m_pageDefinedConstraints.minimumScale = m_pageDefinedConstraints.initialScale;
        m_pageDefinedConstraints.maximumScale = m_pageDefinedConstraints.initialScale;
        if (description.maxWidth.isAuto() || description.maxWidth.type() == ExtendToZoom || description.maxWidth.type() == DeviceWidth) {
            adjustedLayoutSizeWidth = m_viewSize.width() / targetDensityDPIFactor;
            adjustedLayoutSizeHeight = computeHeightByAspectRatio(adjustedLayoutSizeWidth, m_viewSize);
        }
    }

    m_pageDefinedConstraints.layoutSize.setWidth(adjustedLayoutSizeWidth);
    m_pageDefinedConstraints.layoutSize.setHeight(adjustedLayoutSizeHeight);
}

} // namespace blink
