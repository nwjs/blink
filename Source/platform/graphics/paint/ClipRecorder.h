// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ClipRecorder_h
#define ClipRecorder_h

#include "SkRegion.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/graphics/paint/DisplayItem.h"

namespace blink {

class GraphicsContext;

class PLATFORM_EXPORT ClipRecorder {
    WTF_MAKE_FAST_ALLOCATED;
public:
    ClipRecorder(DisplayItemClient, GraphicsContext*, DisplayItem::Type, const LayoutRect& clipRect, SkRegion::Op = SkRegion::kIntersect_Op);
    ~ClipRecorder();
private:
    DisplayItemClient m_client;
    GraphicsContext* m_context;
};

} // namespace blink

#endif // ClipRecorder_h
