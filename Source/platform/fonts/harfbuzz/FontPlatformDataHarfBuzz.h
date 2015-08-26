/*
 * Copyright (c) 2006, 2007, 2008, Google Inc. All rights reserved.
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

#ifndef FontPlatformDataHarfBuzz_h
#define FontPlatformDataHarfBuzz_h

#include "SkPaint.h"
#include "platform/SharedBuffer.h"
#include "platform/fonts/FontDescription.h"
#include "platform/fonts/FontOrientation.h"
#include "platform/fonts/FontRenderStyle.h"
#include "platform/fonts/opentype/OpenTypeVerticalData.h"
#include "wtf/Forward.h"
#include "wtf/HashTableDeletedValueType.h"
#include "wtf/RefPtr.h"
#include "wtf/text/CString.h"
#include "wtf/text/StringImpl.h"

class SkTypeface;
typedef uint32_t SkFontID;

namespace blink {

class GraphicsContext;
class HarfBuzzFace;

class PLATFORM_EXPORT FontPlatformData {
public:
    // Used for deleted values in the font cache's hash tables. The hash table
    // will create us with this structure, and it will compare other values
    // to this "Deleted" one. It expects the Deleted one to be differentiable
    // from the 0 one (created with the empty constructor), so we can't just
    // set everything to 0.
    FontPlatformData(WTF::HashTableDeletedValueType);
    FontPlatformData();
    FontPlatformData(float textSize, bool syntheticBold, bool syntheticItalic);
    FontPlatformData(const FontPlatformData&);
    FontPlatformData(PassRefPtr<SkTypeface>, const char* name, float textSize, bool syntheticBold, bool syntheticItalic, FontOrientation = Horizontal, bool subpixelTextPosition = defaultUseSubpixelPositioning());
    FontPlatformData(const FontPlatformData& src, float textSize);
    ~FontPlatformData();

    String fontFamilyName() const;
    float size() const { return m_textSize; }
    bool isFixedPitch() const;

    SkTypeface* typeface() const { return m_typeface.get(); }
    HarfBuzzFace* harfBuzzFace() const;
    SkFontID uniqueID() const;
    unsigned hash() const;

    FontOrientation orientation() const { return m_orientation; }
    void setOrientation(FontOrientation orientation) { m_orientation = orientation; }
    void setSyntheticBold(bool syntheticBold) { m_syntheticBold = syntheticBold; }
    void setSyntheticItalic(bool syntheticItalic) { m_syntheticItalic = syntheticItalic; }
    bool operator==(const FontPlatformData&) const;
    FontPlatformData& operator=(const FontPlatformData&);
    bool isHashTableDeletedValue() const { return m_isHashTableDeletedValue; }
#if OS(WIN)
    void setMinSizeForAntiAlias(unsigned size) { m_minSizeForAntiAlias = size; }
    unsigned minSizeForAntiAlias() const { return m_minSizeForAntiAlias; }
    void setMinSizeForSubpixel(float size) { m_minSizeForSubpixel = size; }
    float minSizeForSubpixel() const { return m_minSizeForSubpixel; }
    void setHinting(SkPaint::Hinting style)
    {
        m_style.useAutoHint = 0;
        m_style.hintStyle = style;
    }
#endif
    bool fontContainsCharacter(UChar32 character);

#if ENABLE(OPENTYPE_VERTICAL)
    PassRefPtr<OpenTypeVerticalData> verticalData() const;
    PassRefPtr<SharedBuffer> openTypeTable(uint32_t table) const;
#endif

#ifndef NDEBUG
    String description() const;
#endif

    // The returned styles are all actual styles without FontRenderStyle::NoPreference.
    const FontRenderStyle& fontRenderStyle() const { return m_style; }
    void setupPaint(SkPaint*, GraphicsContext* = 0) const;

#if OS(WIN)
    int paintTextFlags() const { return m_paintTextFlags; }
#else
    static void setHinting(SkPaint::Hinting);
    static void setAutoHint(bool);
    static void setUseBitmaps(bool);
    static void setAntiAlias(bool);
    static void setSubpixelRendering(bool);
#endif

private:
    bool static defaultUseSubpixelPositioning();
    void querySystemForRenderStyle(bool useSkiaSubpixelPositioning);

    RefPtr<SkTypeface> m_typeface;
#if !OS(WIN)
    CString m_family;
#endif
    float m_textSize;
    bool m_syntheticBold;
    bool m_syntheticItalic;
    FontOrientation m_orientation;
    FontRenderStyle m_style;
    mutable RefPtr<HarfBuzzFace> m_harfBuzzFace;
    bool m_isHashTableDeletedValue;
#if OS(WIN)
    int m_paintTextFlags;
    bool m_useSubpixelPositioning;
    unsigned m_minSizeForAntiAlias;
    float m_minSizeForSubpixel;
#endif
};

} // namespace blink

#endif // ifdef FontPlatformDataHarfBuzz_h
