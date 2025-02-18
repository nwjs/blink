/*
 * Copyright (C) 2006 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
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

#ifndef RenderSVGResourceRadialGradient_h
#define RenderSVGResourceRadialGradient_h

#include "core/rendering/svg/RenderSVGResourceGradient.h"
#include "core/svg/RadialGradientAttributes.h"

namespace blink {

class SVGRadialGradientElement;

class RenderSVGResourceRadialGradient final : public RenderSVGResourceGradient {
public:
    explicit RenderSVGResourceRadialGradient(SVGRadialGradientElement*);
    virtual ~RenderSVGResourceRadialGradient();

    virtual const char* renderName() const override { return "RenderSVGResourceRadialGradient"; }

    static const RenderSVGResourceType s_resourceType = RadialGradientResourceType;
    virtual RenderSVGResourceType resourceType() const override { return s_resourceType; }

    virtual SVGUnitTypes::SVGUnitType gradientUnits() const override { return m_attributes.gradientUnits(); }
    virtual void calculateGradientTransform(AffineTransform& transform) override { transform = m_attributes.gradientTransform(); }
    virtual bool collectGradientAttributes(SVGGradientElement*) override;
    virtual void buildGradient(GradientData*) const override;

    FloatPoint centerPoint(const RadialGradientAttributes&) const;
    FloatPoint focalPoint(const RadialGradientAttributes&) const;
    float radius(const RadialGradientAttributes&) const;
    float focalRadius(const RadialGradientAttributes&) const;

    virtual void trace(Visitor*) override;

private:
    RadialGradientAttributes m_attributes;
};

DEFINE_RENDER_SVG_RESOURCE_TYPE_CASTS(RenderSVGResourceRadialGradient, RadialGradientResourceType);

}

#endif
