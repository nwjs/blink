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

#ifndef RenderSVGResourceLinearGradient_h
#define RenderSVGResourceLinearGradient_h

#include "core/rendering/svg/RenderSVGResourceGradient.h"
#include "core/svg/LinearGradientAttributes.h"

namespace blink {

class SVGLinearGradientElement;

class RenderSVGResourceLinearGradient final : public RenderSVGResourceGradient {
public:
    explicit RenderSVGResourceLinearGradient(SVGLinearGradientElement*);
    virtual ~RenderSVGResourceLinearGradient();

    virtual const char* renderName() const override { return "RenderSVGResourceLinearGradient"; }

    static const RenderSVGResourceType s_resourceType = LinearGradientResourceType;
    virtual RenderSVGResourceType resourceType() const override { return s_resourceType; }

    virtual SVGUnitTypes::SVGUnitType gradientUnits() const override { return m_attributes.gradientUnits(); }
    virtual void calculateGradientTransform(AffineTransform& transform) override { transform = m_attributes.gradientTransform(); }
    virtual bool collectGradientAttributes(SVGGradientElement*) override;
    virtual void buildGradient(GradientData*) const override;

    FloatPoint startPoint(const LinearGradientAttributes&) const;
    FloatPoint endPoint(const LinearGradientAttributes&) const;

    virtual void trace(Visitor*) override;

private:
    LinearGradientAttributes m_attributes;
};

} // namespace blink

#endif // RenderSVGResourceLinearGradient_h
