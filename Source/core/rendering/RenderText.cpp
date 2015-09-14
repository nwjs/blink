/*
 * (C) 1999 Lars Knoll (knoll@kde.org)
 * (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007 Apple Inc. All rights reserved.
 * Copyright (C) 2006 Andrew Wellington (proton@wiretapped.net)
 * Copyright (C) 2006 Graham Dennis (graham.dennis@gmail.com)
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
#include "core/rendering/RenderText.h"

#include "core/accessibility/AXObjectCache.h"
#include "core/dom/Text.h"
#include "core/editing/TextIterator.h"
#include "core/frame/FrameView.h"
#include "core/frame/Settings.h"
#include "core/html/parser/TextResourceDecoder.h"
#include "core/rendering/AbstractInlineTextBox.h"
#include "core/rendering/EllipsisBox.h"
#include "core/rendering/InlineTextBox.h"
#include "core/rendering/RenderBlock.h"
#include "core/rendering/RenderCombineText.h"
#include "core/rendering/RenderLayer.h"
#include "core/rendering/RenderView.h"
#include "core/rendering/TextRunConstructor.h"
#include "core/rendering/break_lines.h"
#include "platform/fonts/Character.h"
#include "platform/fonts/FontCache.h"
#include "platform/geometry/FloatQuad.h"
#include "platform/text/BidiResolver.h"
#include "platform/text/TextBreakIterator.h"
#include "platform/text/TextRunIterator.h"
#include "wtf/text/StringBuffer.h"
#include "wtf/text/StringBuilder.h"
#include "wtf/unicode/CharacterNames.h"

using namespace WTF;
using namespace Unicode;

namespace blink {

struct SameSizeAsRenderText : public RenderObject {
    uint32_t bitfields : 16;
    float widths[4];
    String text;
    void* pointers[2];
};

COMPILE_ASSERT(sizeof(RenderText) == sizeof(SameSizeAsRenderText), RenderText_should_stay_small);

class SecureTextTimer;
typedef HashMap<RenderText*, SecureTextTimer*> SecureTextTimerMap;
static SecureTextTimerMap* gSecureTextTimers = 0;

class SecureTextTimer FINAL : public TimerBase {
public:
    SecureTextTimer(RenderText* renderText)
        : m_renderText(renderText)
        , m_lastTypedCharacterOffset(-1)
    {
    }

    void restartWithNewText(unsigned lastTypedCharacterOffset)
    {
        m_lastTypedCharacterOffset = lastTypedCharacterOffset;
        if (Settings* settings = m_renderText->document().settings())
            startOneShot(settings->passwordEchoDurationInSeconds(), FROM_HERE);
    }
    void invalidate() { m_lastTypedCharacterOffset = -1; }
    unsigned lastTypedCharacterOffset() { return m_lastTypedCharacterOffset; }

private:
    virtual void fired() OVERRIDE
    {
        ASSERT(gSecureTextTimers->contains(m_renderText));
        m_renderText->setText(m_renderText->text().impl(), true /* forcing setting text as it may be masked later */);
    }

    RenderText* m_renderText;
    int m_lastTypedCharacterOffset;
};

static void makeCapitalized(String* string, UChar previous)
{
    if (string->isNull())
        return;

    unsigned length = string->length();
    const StringImpl& input = *string->impl();

    if (length >= std::numeric_limits<unsigned>::max())
        CRASH();

    StringBuffer<UChar> stringWithPrevious(length + 1);
    stringWithPrevious[0] = previous == noBreakSpace ? ' ' : previous;
    for (unsigned i = 1; i < length + 1; i++) {
        // Replace &nbsp with a real space since ICU no longer treats &nbsp as a word separator.
        if (input[i - 1] == noBreakSpace)
            stringWithPrevious[i] = ' ';
        else
            stringWithPrevious[i] = input[i - 1];
    }

    TextBreakIterator* boundary = wordBreakIterator(stringWithPrevious.characters(), length + 1);
    if (!boundary)
        return;

    StringBuilder result;
    result.reserveCapacity(length);

    int32_t endOfWord;
    int32_t startOfWord = boundary->first();
    for (endOfWord = boundary->next(); endOfWord != TextBreakDone; startOfWord = endOfWord, endOfWord = boundary->next()) {
        if (startOfWord) // Ignore first char of previous string
            result.append(input[startOfWord - 1] == noBreakSpace ? noBreakSpace : toTitleCase(stringWithPrevious[startOfWord]));
        for (int i = startOfWord + 1; i < endOfWord; i++)
            result.append(input[i - 1]);
    }

    *string = result.toString();
}

RenderText::RenderText(Node* node, PassRefPtr<StringImpl> str)
    : RenderObject(!node || node->isDocumentNode() ? 0 : node)
    , m_hasTab(false)
    , m_linesDirty(false)
    , m_containsReversedText(false)
    , m_knownToHaveNoOverflowAndNoFallbackFonts(false)
    , m_minWidth(-1)
    , m_maxWidth(-1)
    , m_firstLineMinWidth(0)
    , m_lastLineLineMinWidth(0)
    , m_text(str)
    , m_firstTextBox(0)
    , m_lastTextBox(0)
{
    ASSERT(m_text);
    // FIXME: Some clients of RenderText (and subclasses) pass Document as node to create anonymous renderer.
    // They should be switched to passing null and using setDocumentForAnonymous.
    if (node && node->isDocumentNode())
        setDocumentForAnonymous(toDocument(node));

    m_isAllASCII = m_text.containsOnlyASCII();
    m_canUseSimpleFontCodePath = computeCanUseSimpleFontCodePath();
    setIsText();

    view()->frameView()->incrementVisuallyNonEmptyCharacterCount(m_text.length());
}

#if ENABLE(ASSERT)

RenderText::~RenderText()
{
    ASSERT(!m_firstTextBox);
    ASSERT(!m_lastTextBox);
}

#endif

const char* RenderText::renderName() const
{
    return "RenderText";
}

bool RenderText::isTextFragment() const
{
    return false;
}

bool RenderText::isWordBreak() const
{
    return false;
}

void RenderText::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    // There is no need to ever schedule repaints from a style change of a text run, since
    // we already did this for the parent of the text run.
    // We do have to schedule layouts, though, since a style change can force us to
    // need to relayout.
    if (diff.needsFullLayout()) {
        setNeedsLayoutAndPrefWidthsRecalcAndFullPaintInvalidation();
        m_knownToHaveNoOverflowAndNoFallbackFonts = false;
    }

    RenderStyle* newStyle = style();
    ETextTransform oldTransform = oldStyle ? oldStyle->textTransform() : TTNONE;
    ETextSecurity oldSecurity = oldStyle ? oldStyle->textSecurity() : TSNONE;
    if (oldTransform != newStyle->textTransform() || oldSecurity != newStyle->textSecurity())
        transformText();

    // This is an optimization that kicks off font load before layout.
    // In order to make it fast, we only check if the first character of the
    // text is included in the unicode ranges of the fonts.
    if (!text().containsOnlyWhitespace())
        newStyle->font().willUseFontData(text().characterStartingAt(0));
}

void RenderText::removeAndDestroyTextBoxes()
{
    if (!documentBeingDestroyed()) {
        if (firstTextBox()) {
            if (isBR()) {
                RootInlineBox* next = firstTextBox()->root().nextRootBox();
                if (next)
                    next->markDirty();
            }
            for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox())
                box->remove();
        } else if (parent())
            parent()->dirtyLinesFromChangedChild(this);
    }
    deleteTextBoxes();
}

void RenderText::willBeDestroyed()
{
    if (SecureTextTimer* secureTextTimer = gSecureTextTimers ? gSecureTextTimers->take(this) : 0)
        delete secureTextTimer;

    removeAndDestroyTextBoxes();
    RenderObject::willBeDestroyed();
}

void RenderText::extractTextBox(InlineTextBox* box)
{
    checkConsistency();

    m_lastTextBox = box->prevTextBox();
    if (box == m_firstTextBox)
        m_firstTextBox = 0;
    if (box->prevTextBox())
        box->prevTextBox()->setNextTextBox(0);
    box->setPreviousTextBox(0);
    for (InlineTextBox* curr = box; curr; curr = curr->nextTextBox())
        curr->setExtracted();

    checkConsistency();
}

void RenderText::attachTextBox(InlineTextBox* box)
{
    checkConsistency();

    if (m_lastTextBox) {
        m_lastTextBox->setNextTextBox(box);
        box->setPreviousTextBox(m_lastTextBox);
    } else
        m_firstTextBox = box;
    InlineTextBox* last = box;
    for (InlineTextBox* curr = box; curr; curr = curr->nextTextBox()) {
        curr->setExtracted(false);
        last = curr;
    }
    m_lastTextBox = last;

    checkConsistency();
}

void RenderText::removeTextBox(InlineTextBox* box)
{
    checkConsistency();

    if (box == m_firstTextBox)
        m_firstTextBox = box->nextTextBox();
    if (box == m_lastTextBox)
        m_lastTextBox = box->prevTextBox();
    if (box->nextTextBox())
        box->nextTextBox()->setPreviousTextBox(box->prevTextBox());
    if (box->prevTextBox())
        box->prevTextBox()->setNextTextBox(box->nextTextBox());

    checkConsistency();
}

void RenderText::deleteTextBoxes()
{
    if (firstTextBox()) {
        InlineTextBox* next;
        for (InlineTextBox* curr = firstTextBox(); curr; curr = next) {
            next = curr->nextTextBox();
            curr->destroy();
        }
        m_firstTextBox = m_lastTextBox = 0;
    }
}

PassRefPtr<StringImpl> RenderText::originalText() const
{
    Node* e = node();
    return (e && e->isTextNode()) ? toText(e)->dataImpl() : 0;
}

String RenderText::plainText() const
{
    if (node())
        return blink::plainText(rangeOfContents(node()).get());

    // FIXME: this is just a stopgap until TextIterator is adapted to support generated text.
    StringBuilder plainTextBuilder;
    for (InlineTextBox* textBox = firstTextBox(); textBox; textBox = textBox->nextTextBox()) {
        String text = m_text.substring(textBox->start(), textBox->len()).simplifyWhiteSpace(WTF::DoNotStripWhiteSpace);
        plainTextBuilder.append(text);
        if (textBox->nextTextBox() && textBox->nextTextBox()->start() > textBox->end() && text.length() && !text.right(1).containsOnlyWhitespace())
            plainTextBuilder.append(" ");
    }
    return plainTextBuilder.toString();
}

void RenderText::absoluteRects(Vector<IntRect>& rects, const LayoutPoint& accumulatedOffset) const
{
    for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox())
        rects.append(enclosingIntRect(FloatRect(accumulatedOffset + box->topLeft(), box->size())));
}

static FloatRect localQuadForTextBox(InlineTextBox* box, unsigned start, unsigned end, bool useSelectionHeight)
{
    unsigned realEnd = std::min(box->end() + 1, end);
    LayoutRect r = box->localSelectionRect(start, realEnd);
    if (r.height()) {
        if (!useSelectionHeight) {
            // Change the height and y position (or width and x for vertical text)
            // because selectionRect uses selection-specific values.
            if (box->isHorizontal()) {
                r.setHeight(box->height());
                r.setY(box->y());
            } else {
                r.setWidth(box->width());
                r.setX(box->x());
            }
        }
        return FloatRect(r);
    }
    return FloatRect();
}

void RenderText::absoluteRectsForRange(Vector<IntRect>& rects, unsigned start, unsigned end, bool useSelectionHeight, bool* wasFixed)
{
    // Work around signed/unsigned issues. This function takes unsigneds, and is often passed UINT_MAX
    // to mean "all the way to the end". InlineTextBox coordinates are unsigneds, so changing this
    // function to take ints causes various internal mismatches. But selectionRect takes ints, and
    // passing UINT_MAX to it causes trouble. Ideally we'd change selectionRect to take unsigneds, but
    // that would cause many ripple effects, so for now we'll just clamp our unsigned parameters to INT_MAX.
    ASSERT(end == UINT_MAX || end <= INT_MAX);
    ASSERT(start <= INT_MAX);
    start = std::min(start, static_cast<unsigned>(INT_MAX));
    end = std::min(end, static_cast<unsigned>(INT_MAX));

    for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox()) {
        // Note: box->end() returns the index of the last character, not the index past it
        if (start <= box->start() && box->end() < end) {
            FloatRect r = box->calculateBoundaries();
            if (useSelectionHeight) {
                LayoutRect selectionRect = box->localSelectionRect(start, end);
                if (box->isHorizontal()) {
                    r.setHeight(selectionRect.height().toFloat());
                    r.setY(selectionRect.y().toFloat());
                } else {
                    r.setWidth(selectionRect.width().toFloat());
                    r.setX(selectionRect.x().toFloat());
                }
            }
            rects.append(localToAbsoluteQuad(r, 0, wasFixed).enclosingBoundingBox());
        } else {
            // FIXME: This code is wrong. It's converting local to absolute twice. http://webkit.org/b/65722
            FloatRect rect = localQuadForTextBox(box, start, end, useSelectionHeight);
            if (!rect.isZero())
                rects.append(localToAbsoluteQuad(rect, 0, wasFixed).enclosingBoundingBox());
        }
    }
}

static IntRect ellipsisRectForBox(InlineTextBox* box, unsigned startPos, unsigned endPos)
{
    if (!box)
        return IntRect();

    unsigned short truncation = box->truncation();
    if (truncation == cNoTruncation)
        return IntRect();

    IntRect rect;
    if (EllipsisBox* ellipsis = box->root().ellipsisBox()) {
        int ellipsisStartPosition = std::max<int>(startPos - box->start(), 0);
        int ellipsisEndPosition = std::min<int>(endPos - box->start(), box->len());

        // The ellipsis should be considered to be selected if the end of
        // the selection is past the beginning of the truncation and the
        // beginning of the selection is before or at the beginning of the truncation.
        if (ellipsisEndPosition >= truncation && ellipsisStartPosition <= truncation)
            return ellipsis->selectionRect();
    }

    return IntRect();
}

void RenderText::absoluteQuads(Vector<FloatQuad>& quads, bool* wasFixed, ClippingOption option) const
{
    for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox()) {
        FloatRect boundaries = box->calculateBoundaries();

        // Shorten the width of this text box if it ends in an ellipsis.
        // FIXME: ellipsisRectForBox should switch to return FloatRect soon with the subpixellayout branch.
        IntRect ellipsisRect = (option == ClipToEllipsis) ? ellipsisRectForBox(box, 0, textLength()) : IntRect();
        if (!ellipsisRect.isEmpty()) {
            if (style()->isHorizontalWritingMode())
                boundaries.setWidth(ellipsisRect.maxX() - boundaries.x());
            else
                boundaries.setHeight(ellipsisRect.maxY() - boundaries.y());
        }
        quads.append(localToAbsoluteQuad(boundaries, 0, wasFixed));
    }
}

void RenderText::absoluteQuads(Vector<FloatQuad>& quads, bool* wasFixed) const
{
    absoluteQuads(quads, wasFixed, NoClipping);
}

void RenderText::absoluteQuadsForRange(Vector<FloatQuad>& quads, unsigned start, unsigned end, bool useSelectionHeight, bool* wasFixed)
{
    // Work around signed/unsigned issues. This function takes unsigneds, and is often passed UINT_MAX
    // to mean "all the way to the end". InlineTextBox coordinates are unsigneds, so changing this
    // function to take ints causes various internal mismatches. But selectionRect takes ints, and
    // passing UINT_MAX to it causes trouble. Ideally we'd change selectionRect to take unsigneds, but
    // that would cause many ripple effects, so for now we'll just clamp our unsigned parameters to INT_MAX.
    ASSERT(end == UINT_MAX || end <= INT_MAX);
    ASSERT(start <= INT_MAX);
    start = std::min(start, static_cast<unsigned>(INT_MAX));
    end = std::min(end, static_cast<unsigned>(INT_MAX));

    for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox()) {
        // Note: box->end() returns the index of the last character, not the index past it
        if (start <= box->start() && box->end() < end) {
            FloatRect r = box->calculateBoundaries();
            if (useSelectionHeight) {
                LayoutRect selectionRect = box->localSelectionRect(start, end);
                if (box->isHorizontal()) {
                    r.setHeight(selectionRect.height().toFloat());
                    r.setY(selectionRect.y().toFloat());
                } else {
                    r.setWidth(selectionRect.width().toFloat());
                    r.setX(selectionRect.x().toFloat());
                }
            }
            quads.append(localToAbsoluteQuad(r, 0, wasFixed));
        } else {
            FloatRect rect = localQuadForTextBox(box, start, end, useSelectionHeight);
            if (!rect.isZero())
                quads.append(localToAbsoluteQuad(rect, 0, wasFixed));
        }
    }
}

enum ShouldAffinityBeDownstream { AlwaysDownstream, AlwaysUpstream, UpstreamIfPositionIsNotAtStart };

static bool lineDirectionPointFitsInBox(int pointLineDirection, InlineTextBox* box, ShouldAffinityBeDownstream& shouldAffinityBeDownstream)
{
    shouldAffinityBeDownstream = AlwaysDownstream;

    // the x coordinate is equal to the left edge of this box
    // the affinity must be downstream so the position doesn't jump back to the previous line
    // except when box is the first box in the line
    if (pointLineDirection <= box->logicalLeft()) {
        shouldAffinityBeDownstream = !box->prevLeafChild() ? UpstreamIfPositionIsNotAtStart : AlwaysDownstream;
        return true;
    }

    // and the x coordinate is to the left of the right edge of this box
    // check to see if position goes in this box
    if (pointLineDirection < box->logicalRight()) {
        shouldAffinityBeDownstream = UpstreamIfPositionIsNotAtStart;
        return true;
    }

    // box is first on line
    // and the x coordinate is to the left of the first text box left edge
    if (!box->prevLeafChildIgnoringLineBreak() && pointLineDirection < box->logicalLeft())
        return true;

    if (!box->nextLeafChildIgnoringLineBreak()) {
        // box is last on line
        // and the x coordinate is to the right of the last text box right edge
        // generate VisiblePosition, use UPSTREAM affinity if possible
        shouldAffinityBeDownstream = UpstreamIfPositionIsNotAtStart;
        return true;
    }

    return false;
}

static PositionWithAffinity createPositionWithAffinityForBox(const InlineBox* box, int offset, ShouldAffinityBeDownstream shouldAffinityBeDownstream)
{
    EAffinity affinity = VP_DEFAULT_AFFINITY;
    switch (shouldAffinityBeDownstream) {
    case AlwaysDownstream:
        affinity = DOWNSTREAM;
        break;
    case AlwaysUpstream:
        affinity = VP_UPSTREAM_IF_POSSIBLE;
        break;
    case UpstreamIfPositionIsNotAtStart:
        affinity = offset > box->caretMinOffset() ? VP_UPSTREAM_IF_POSSIBLE : DOWNSTREAM;
        break;
    }
    int textStartOffset = box->renderer().isText() ? toRenderText(box->renderer()).textStartOffset() : 0;
    return box->renderer().createPositionWithAffinity(offset + textStartOffset, affinity);
}

static PositionWithAffinity createPositionWithAffinityForBoxAfterAdjustingOffsetForBiDi(const InlineTextBox* box, int offset, ShouldAffinityBeDownstream shouldAffinityBeDownstream)
{
    ASSERT(box);
    ASSERT(offset >= 0);

    if (offset && static_cast<unsigned>(offset) < box->len())
        return createPositionWithAffinityForBox(box, box->start() + offset, shouldAffinityBeDownstream);

    bool positionIsAtStartOfBox = !offset;
    if (positionIsAtStartOfBox == box->isLeftToRightDirection()) {
        // offset is on the left edge

        const InlineBox* prevBox = box->prevLeafChildIgnoringLineBreak();
        if ((prevBox && prevBox->bidiLevel() == box->bidiLevel())
            || box->renderer().containingBlock()->style()->direction() == box->direction()) // FIXME: left on 12CBA
            return createPositionWithAffinityForBox(box, box->caretLeftmostOffset(), shouldAffinityBeDownstream);

        if (prevBox && prevBox->bidiLevel() > box->bidiLevel()) {
            // e.g. left of B in aDC12BAb
            const InlineBox* leftmostBox;
            do {
                leftmostBox = prevBox;
                prevBox = leftmostBox->prevLeafChildIgnoringLineBreak();
            } while (prevBox && prevBox->bidiLevel() > box->bidiLevel());
            return createPositionWithAffinityForBox(leftmostBox, leftmostBox->caretRightmostOffset(), shouldAffinityBeDownstream);
        }

        if (!prevBox || prevBox->bidiLevel() < box->bidiLevel()) {
            // e.g. left of D in aDC12BAb
            const InlineBox* rightmostBox;
            const InlineBox* nextBox = box;
            do {
                rightmostBox = nextBox;
                nextBox = rightmostBox->nextLeafChildIgnoringLineBreak();
            } while (nextBox && nextBox->bidiLevel() >= box->bidiLevel());
            return createPositionWithAffinityForBox(rightmostBox,
                box->isLeftToRightDirection() ? rightmostBox->caretMaxOffset() : rightmostBox->caretMinOffset(), shouldAffinityBeDownstream);
        }

        return createPositionWithAffinityForBox(box, box->caretRightmostOffset(), shouldAffinityBeDownstream);
    }

    const InlineBox* nextBox = box->nextLeafChildIgnoringLineBreak();
    if ((nextBox && nextBox->bidiLevel() == box->bidiLevel())
        || box->renderer().containingBlock()->style()->direction() == box->direction())
        return createPositionWithAffinityForBox(box, box->caretRightmostOffset(), shouldAffinityBeDownstream);

    // offset is on the right edge
    if (nextBox && nextBox->bidiLevel() > box->bidiLevel()) {
        // e.g. right of C in aDC12BAb
        const InlineBox* rightmostBox;
        do {
            rightmostBox = nextBox;
            nextBox = rightmostBox->nextLeafChildIgnoringLineBreak();
        } while (nextBox && nextBox->bidiLevel() > box->bidiLevel());
        return createPositionWithAffinityForBox(rightmostBox, rightmostBox->caretLeftmostOffset(), shouldAffinityBeDownstream);
    }

    if (!nextBox || nextBox->bidiLevel() < box->bidiLevel()) {
        // e.g. right of A in aDC12BAb
        const InlineBox* leftmostBox;
        const InlineBox* prevBox = box;
        do {
            leftmostBox = prevBox;
            prevBox = leftmostBox->prevLeafChildIgnoringLineBreak();
        } while (prevBox && prevBox->bidiLevel() >= box->bidiLevel());
        return createPositionWithAffinityForBox(leftmostBox,
            box->isLeftToRightDirection() ? leftmostBox->caretMinOffset() : leftmostBox->caretMaxOffset(), shouldAffinityBeDownstream);
    }

    return createPositionWithAffinityForBox(box, box->caretLeftmostOffset(), shouldAffinityBeDownstream);
}

PositionWithAffinity RenderText::positionForPoint(const LayoutPoint& point)
{
    if (!firstTextBox() || textLength() == 0)
        return createPositionWithAffinity(0, DOWNSTREAM);

    LayoutUnit pointLineDirection = firstTextBox()->isHorizontal() ? point.x() : point.y();
    LayoutUnit pointBlockDirection = firstTextBox()->isHorizontal() ? point.y() : point.x();
    bool blocksAreFlipped = style()->isFlippedBlocksWritingMode();

    InlineTextBox* lastBox = 0;
    for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox()) {
        if (box->isLineBreak() && !box->prevLeafChild() && box->nextLeafChild() && !box->nextLeafChild()->isLineBreak())
            box = box->nextTextBox();

        RootInlineBox& rootBox = box->root();
        LayoutUnit top = std::min(rootBox.selectionTop(), rootBox.lineTop());
        if (pointBlockDirection > top || (!blocksAreFlipped && pointBlockDirection == top)) {
            LayoutUnit bottom = rootBox.selectionBottom();
            if (rootBox.nextRootBox())
                bottom = std::min(bottom, rootBox.nextRootBox()->lineTop());

            if (pointBlockDirection < bottom || (blocksAreFlipped && pointBlockDirection == bottom)) {
                ShouldAffinityBeDownstream shouldAffinityBeDownstream;
                if (lineDirectionPointFitsInBox(pointLineDirection, box, shouldAffinityBeDownstream))
                    return createPositionWithAffinityForBoxAfterAdjustingOffsetForBiDi(box, box->offsetForPosition(pointLineDirection.toFloat()), shouldAffinityBeDownstream);
            }
        }
        lastBox = box;
    }

    if (lastBox) {
        ShouldAffinityBeDownstream shouldAffinityBeDownstream;
        lineDirectionPointFitsInBox(pointLineDirection, lastBox, shouldAffinityBeDownstream);
        return createPositionWithAffinityForBoxAfterAdjustingOffsetForBiDi(lastBox, lastBox->offsetForPosition(pointLineDirection.toFloat()) + lastBox->start(), shouldAffinityBeDownstream);
    }
    return createPositionWithAffinity(0, DOWNSTREAM);
}

LayoutRect RenderText::localCaretRect(InlineBox* inlineBox, int caretOffset, LayoutUnit* extraWidthToEndOfLine)
{
    if (!inlineBox)
        return LayoutRect();

    ASSERT(inlineBox->isInlineTextBox());
    if (!inlineBox->isInlineTextBox())
        return LayoutRect();

    InlineTextBox* box = toInlineTextBox(inlineBox);

    int height = box->root().selectionHeight();
    int top = box->root().selectionTop();

    // Go ahead and round left to snap it to the nearest pixel.
    float left = box->positionForOffset(caretOffset);

    // Distribute the caret's width to either side of the offset.
    int caretWidthLeftOfOffset = caretWidth / 2;
    left -= caretWidthLeftOfOffset;
    int caretWidthRightOfOffset = caretWidth - caretWidthLeftOfOffset;

    left = roundf(left);

    float rootLeft = box->root().logicalLeft();
    float rootRight = box->root().logicalRight();

    // FIXME: should we use the width of the root inline box or the
    // width of the containing block for this?
    if (extraWidthToEndOfLine)
        *extraWidthToEndOfLine = (box->root().logicalWidth() + rootLeft) - (left + 1);

    RenderBlock* cb = containingBlock();
    RenderStyle* cbStyle = cb->style();

    float leftEdge;
    float rightEdge;
    leftEdge = std::min<float>(0, rootLeft);
    rightEdge = std::max<float>(cb->logicalWidth().toFloat(), rootRight);

    bool rightAligned = false;
    switch (cbStyle->textAlign()) {
    case RIGHT:
    case WEBKIT_RIGHT:
        rightAligned = true;
        break;
    case LEFT:
    case WEBKIT_LEFT:
    case CENTER:
    case WEBKIT_CENTER:
        break;
    case JUSTIFY:
    case TASTART:
        rightAligned = !cbStyle->isLeftToRightDirection();
        break;
    case TAEND:
        rightAligned = cbStyle->isLeftToRightDirection();
        break;
    }

    if (rightAligned) {
        left = std::max(left, leftEdge);
        left = std::min(left, rootRight - caretWidth);
    } else {
        left = std::min(left, rightEdge - caretWidthRightOfOffset);
        left = std::max(left, rootLeft);
    }

    return style()->isHorizontalWritingMode() ? IntRect(left, top, caretWidth, height) : IntRect(top, left, height, caretWidth);
}

ALWAYS_INLINE float RenderText::widthFromCache(const Font& f, int start, int len, float xPos, TextDirection textDirection, HashSet<const SimpleFontData*>* fallbackFonts, GlyphOverflow* glyphOverflow) const
{
    if (style()->hasTextCombine() && isCombineText()) {
        const RenderCombineText* combineText = toRenderCombineText(this);
        if (combineText->isCombined())
            return combineText->combinedTextWidth(f);
    }

    if (f.isFixedPitch() && f.fontDescription().variant() == FontVariantNormal && m_isAllASCII && (!glyphOverflow || !glyphOverflow->computeBounds)) {
        float monospaceCharacterWidth = f.spaceWidth();
        float w = 0;
        bool isSpace;
        ASSERT(m_text);
        StringImpl& text = *m_text.impl();
        for (int i = start; i < start + len; i++) {
            char c = text[i];
            if (c <= ' ') {
                if (c == ' ' || c == '\n') {
                    w += monospaceCharacterWidth;
                    isSpace = true;
                } else if (c == '\t') {
                    if (style()->collapseWhiteSpace()) {
                        w += monospaceCharacterWidth;
                        isSpace = true;
                    } else {
                        w += f.tabWidth(style()->tabSize(), xPos + w);
                        isSpace = false;
                    }
                } else
                    isSpace = false;
            } else {
                w += monospaceCharacterWidth;
                isSpace = false;
            }
            if (isSpace && i > start)
                w += f.fontDescription().wordSpacing();
        }
        return w;
    }

    TextRun run = constructTextRun(const_cast<RenderText*>(this), f, this, start, len, style(), textDirection);
    run.setCharactersLength(textLength() - start);
    ASSERT(run.charactersLength() >= run.length());

    run.setCharacterScanForCodePath(!canUseSimpleFontCodePath());
    run.setTabSize(!style()->collapseWhiteSpace(), style()->tabSize());
    run.setXPos(xPos);
    FontCachePurgePreventer fontCachePurgePreventer;
    return f.width(run, fallbackFonts, glyphOverflow);
}

void RenderText::trimmedPrefWidths(float leadWidth,
    float& firstLineMinWidth, bool& hasBreakableStart,
    float& lastLineMinWidth, bool& hasBreakableEnd,
    bool& hasBreakableChar, bool& hasBreak,
    float& firstLineMaxWidth, float& lastLineMaxWidth,
    float& minWidth, float& maxWidth, bool& stripFrontSpaces,
    TextDirection direction)
{
    bool collapseWhiteSpace = style()->collapseWhiteSpace();
    if (!collapseWhiteSpace)
        stripFrontSpaces = false;

    if (m_hasTab || preferredLogicalWidthsDirty())
        computePreferredLogicalWidths(leadWidth);

    hasBreakableStart = !stripFrontSpaces && m_hasBreakableStart;
    hasBreakableEnd = m_hasBreakableEnd;

    int len = textLength();

    if (!len || (stripFrontSpaces && text().impl()->containsOnlyWhitespace())) {
        firstLineMinWidth = 0;
        lastLineMinWidth = 0;
        firstLineMaxWidth = 0;
        lastLineMaxWidth = 0;
        minWidth = 0;
        maxWidth = 0;
        hasBreak = false;
        return;
    }

    minWidth = m_minWidth;
    maxWidth = m_maxWidth;

    firstLineMinWidth = m_firstLineMinWidth;
    lastLineMinWidth = m_lastLineLineMinWidth;

    hasBreakableChar = m_hasBreakableChar;
    hasBreak = m_hasBreak;

    ASSERT(m_text);
    StringImpl& text = *m_text.impl();
    if (text[0] == ' ' || (text[0] == '\n' && !style()->preserveNewline()) || text[0] == '\t') {
        const Font& font = style()->font(); // FIXME: This ignores first-line.
        if (stripFrontSpaces) {
            const UChar space = ' ';
            float spaceWidth = font.width(constructTextRun(this, font, &space, 1, style(), direction));
            maxWidth -= spaceWidth;
        } else {
            maxWidth += font.fontDescription().wordSpacing();
        }
    }

    stripFrontSpaces = collapseWhiteSpace && m_hasEndWhiteSpace;

    if (!style()->autoWrap() || minWidth > maxWidth)
        minWidth = maxWidth;

    // Compute our max widths by scanning the string for newlines.
    if (hasBreak) {
        const Font& f = style()->font(); // FIXME: This ignores first-line.
        bool firstLine = true;
        firstLineMaxWidth = maxWidth;
        lastLineMaxWidth = maxWidth;
        for (int i = 0; i < len; i++) {
            int linelen = 0;
            while (i + linelen < len && text[i + linelen] != '\n')
                linelen++;

            if (linelen) {
                lastLineMaxWidth = widthFromCache(f, i, linelen, leadWidth + lastLineMaxWidth, direction, 0, 0);
                if (firstLine) {
                    firstLine = false;
                    leadWidth = 0;
                    firstLineMaxWidth = lastLineMaxWidth;
                }
                i += linelen;
            } else if (firstLine) {
                firstLineMaxWidth = 0;
                firstLine = false;
                leadWidth = 0;
            }

            if (i == len - 1) {
                // A <pre> run that ends with a newline, as in, e.g.,
                // <pre>Some text\n\n<span>More text</pre>
                lastLineMaxWidth = 0;
            }
        }
    }
}

float RenderText::minLogicalWidth() const
{
    if (preferredLogicalWidthsDirty())
        const_cast<RenderText*>(this)->computePreferredLogicalWidths(0);

    return m_minWidth;
}

float RenderText::maxLogicalWidth() const
{
    if (preferredLogicalWidthsDirty())
        const_cast<RenderText*>(this)->computePreferredLogicalWidths(0);

    return m_maxWidth;
}

void RenderText::computePreferredLogicalWidths(float leadWidth)
{
    HashSet<const SimpleFontData*> fallbackFonts;
    GlyphOverflow glyphOverflow;
    computePreferredLogicalWidths(leadWidth, fallbackFonts, glyphOverflow);

    // We shouldn't change our mind once we "know".
    ASSERT(!m_knownToHaveNoOverflowAndNoFallbackFonts || (fallbackFonts.isEmpty() && glyphOverflow.isZero()));
    m_knownToHaveNoOverflowAndNoFallbackFonts = fallbackFonts.isEmpty() && glyphOverflow.isZero();
}

static inline float hyphenWidth(RenderText* renderer, const Font& font, TextDirection direction)
{
    RenderStyle* style = renderer->style();
    return font.width(constructTextRun(renderer, font, style->hyphenString().string(), style, direction));
}

void RenderText::computePreferredLogicalWidths(float leadWidth, HashSet<const SimpleFontData*>& fallbackFonts, GlyphOverflow& glyphOverflow)
{
    ASSERT(m_hasTab || preferredLogicalWidthsDirty() || !m_knownToHaveNoOverflowAndNoFallbackFonts);

    m_minWidth = 0;
    m_maxWidth = 0;
    m_firstLineMinWidth = 0;
    m_lastLineLineMinWidth = 0;

    if (isBR())
        return;

    float currMinWidth = 0;
    float currMaxWidth = 0;
    m_hasBreakableChar = false;
    m_hasBreak = false;
    m_hasTab = false;
    m_hasBreakableStart = false;
    m_hasBreakableEnd = false;
    m_hasEndWhiteSpace = false;

    RenderStyle* styleToUse = style();
    const Font& f = styleToUse->font(); // FIXME: This ignores first-line.
    float wordSpacing = styleToUse->wordSpacing();
    int len = textLength();
    LazyLineBreakIterator breakIterator(m_text, styleToUse->locale());
    bool needsWordSpacing = false;
    bool ignoringSpaces = false;
    bool isSpace = false;
    bool firstWord = true;
    bool firstLine = true;
    int nextBreakable = -1;
    int lastWordBoundary = 0;
    float cachedWordTrailingSpaceWidth[2] = { 0, 0 }; // LTR, RTL

    int firstGlyphLeftOverflow = -1;

    bool breakAll = (styleToUse->wordBreak() == BreakAllWordBreak || styleToUse->wordBreak() == BreakWordBreak) && styleToUse->autoWrap();

    TextRun textRun(text());
    BidiResolver<TextRunIterator, BidiCharacterRun> bidiResolver;

    BidiCharacterRun* run;
    TextDirection textDirection = styleToUse->direction();
    if (isOverride(styleToUse->unicodeBidi())) {
        run = 0;
    } else {
        BidiStatus status(textDirection, false);
        bidiResolver.setStatus(status);
        bidiResolver.setPositionIgnoringNestedIsolates(TextRunIterator(&textRun, 0));
        bool hardLineBreak = false;
        bool reorderRuns = false;
        bidiResolver.createBidiRunsForLine(TextRunIterator(&textRun, textRun.length()), NoVisualOverride, hardLineBreak, reorderRuns);
        BidiRunList<BidiCharacterRun>& bidiRuns = bidiResolver.runs();
        run = bidiRuns.firstRun();
    }

    for (int i = 0; i < len; i++) {
        UChar c = uncheckedCharacterAt(i);

        if (run) {
            // Treat adjacent runs with the same resolved directionality
            // (TextDirection as opposed to WTF::Unicode::Direction) as belonging
            // to the same run to avoid breaking unnecessarily.
            while (i >= run->stop() || (run->next() && run->next()->direction() == run->direction()))
                run = run->next();

            ASSERT(run);
            ASSERT(i <= run->stop());
            textDirection = run->direction();
        }

        bool previousCharacterIsSpace = isSpace;
        bool isNewline = false;
        if (c == '\n') {
            if (styleToUse->preserveNewline()) {
                m_hasBreak = true;
                isNewline = true;
                isSpace = false;
            } else
                isSpace = true;
        } else if (c == '\t') {
            if (!styleToUse->collapseWhiteSpace()) {
                m_hasTab = true;
                isSpace = false;
            } else
                isSpace = true;
        } else
            isSpace = c == ' ';

        bool isBreakableLocation = isNewline || (isSpace && styleToUse->autoWrap());
        if (!i)
            m_hasBreakableStart = isBreakableLocation;
        if (i == len - 1) {
            m_hasBreakableEnd = isBreakableLocation;
            m_hasEndWhiteSpace = isNewline || isSpace;
        }

        if (!ignoringSpaces && styleToUse->collapseWhiteSpace() && previousCharacterIsSpace && isSpace)
            ignoringSpaces = true;

        if (ignoringSpaces && !isSpace)
            ignoringSpaces = false;

        // Ignore spaces and soft hyphens
        if (ignoringSpaces) {
            ASSERT(lastWordBoundary == i);
            lastWordBoundary++;
            continue;
        } else if (c == softHyphen) {
            currMaxWidth += widthFromCache(f, lastWordBoundary, i - lastWordBoundary, leadWidth + currMaxWidth, textDirection, &fallbackFonts, &glyphOverflow);
            if (firstGlyphLeftOverflow < 0)
                firstGlyphLeftOverflow = glyphOverflow.left;
            lastWordBoundary = i + 1;
            continue;
        }

        bool hasBreak = breakAll || isBreakable(breakIterator, i, nextBreakable);
        bool betweenWords = true;
        int j = i;
        while (c != '\n' && c != ' ' && c != '\t' && (c != softHyphen)) {
            j++;
            if (j == len)
                break;
            c = uncheckedCharacterAt(j);
            if (isBreakable(breakIterator, j, nextBreakable) && characterAt(j - 1) != softHyphen)
                break;
            if (breakAll) {
                betweenWords = false;
                break;
            }
        }

        // Terminate word boundary at bidi run boundary.
        if (run)
            j = std::min(j, run->stop() + 1);
        int wordLen = j - i;
        if (wordLen) {
            bool isSpace = (j < len) && c == ' ';

            // Non-zero only when kerning is enabled, in which case we measure words with their trailing
            // space, then subtract its width.
            float wordTrailingSpaceWidth = 0;
            if (isSpace && (f.fontDescription().typesettingFeatures() & Kerning)) {
                ASSERT(textDirection >=0 && textDirection <= 1);
                if (!cachedWordTrailingSpaceWidth[textDirection])
                    cachedWordTrailingSpaceWidth[textDirection] = f.width(constructTextRun(this, f, &space, 1, styleToUse, textDirection)) + wordSpacing;
                wordTrailingSpaceWidth = cachedWordTrailingSpaceWidth[textDirection];
            }

            float w;
            if (wordTrailingSpaceWidth && isSpace)
                w = widthFromCache(f, i, wordLen + 1, leadWidth + currMaxWidth, textDirection, &fallbackFonts, &glyphOverflow) - wordTrailingSpaceWidth;
            else {
                w = widthFromCache(f, i, wordLen, leadWidth + currMaxWidth, textDirection, &fallbackFonts, &glyphOverflow);
                if (c == softHyphen)
                    currMinWidth += hyphenWidth(this, f, textDirection);
            }

            if (firstGlyphLeftOverflow < 0)
                firstGlyphLeftOverflow = glyphOverflow.left;
            currMinWidth += w;
            if (betweenWords) {
                if (lastWordBoundary == i)
                    currMaxWidth += w;
                else
                    currMaxWidth += widthFromCache(f, lastWordBoundary, j - lastWordBoundary, leadWidth + currMaxWidth, textDirection, &fallbackFonts, &glyphOverflow);
                lastWordBoundary = j;
            }

            bool isCollapsibleWhiteSpace = (j < len) && styleToUse->isCollapsibleWhiteSpace(c);
            if (j < len && styleToUse->autoWrap())
                m_hasBreakableChar = true;

            // Add in wordSpacing to our currMaxWidth, but not if this is the last word on a line or the
            // last word in the run.
            if (wordSpacing && (isSpace || isCollapsibleWhiteSpace) && !containsOnlyWhitespace(j, len-j))
                currMaxWidth += wordSpacing;

            if (firstWord) {
                firstWord = false;
                // If the first character in the run is breakable, then we consider ourselves to have a beginning
                // minimum width of 0, since a break could occur right before our run starts, preventing us from ever
                // being appended to a previous text run when considering the total minimum width of the containing block.
                if (hasBreak)
                    m_hasBreakableChar = true;
                m_firstLineMinWidth = hasBreak ? 0 : currMinWidth;
            }
            m_lastLineLineMinWidth = currMinWidth;

            if (currMinWidth > m_minWidth)
                m_minWidth = currMinWidth;
            currMinWidth = 0;

            i += wordLen - 1;
        } else {
            // Nowrap can never be broken, so don't bother setting the
            // breakable character boolean. Pre can only be broken if we encounter a newline.
            if (style()->autoWrap() || isNewline)
                m_hasBreakableChar = true;

            if (currMinWidth > m_minWidth)
                m_minWidth = currMinWidth;
            currMinWidth = 0;

            if (isNewline) { // Only set if preserveNewline was true and we saw a newline.
                if (firstLine) {
                    firstLine = false;
                    leadWidth = 0;
                    if (!styleToUse->autoWrap())
                        m_firstLineMinWidth = currMaxWidth;
                }

                if (currMaxWidth > m_maxWidth)
                    m_maxWidth = currMaxWidth;
                currMaxWidth = 0;
            } else {
                TextRun run = constructTextRun(this, f, this, i, 1, styleToUse, textDirection);
                run.setCharactersLength(len - i);
                ASSERT(run.charactersLength() >= run.length());
                run.setTabSize(!style()->collapseWhiteSpace(), style()->tabSize());
                run.setXPos(leadWidth + currMaxWidth);

                currMaxWidth += f.width(run);
                glyphOverflow.right = 0;
                needsWordSpacing = isSpace && !previousCharacterIsSpace && i == len - 1;
            }
            ASSERT(lastWordBoundary == i);
            lastWordBoundary++;
        }
    }
    if (run)
        bidiResolver.runs().deleteRuns();

    if (firstGlyphLeftOverflow > 0)
        glyphOverflow.left = firstGlyphLeftOverflow;

    if ((needsWordSpacing && len > 1) || (ignoringSpaces && !firstWord))
        currMaxWidth += wordSpacing;

    m_minWidth = std::max(currMinWidth, m_minWidth);
    m_maxWidth = std::max(currMaxWidth, m_maxWidth);

    if (!styleToUse->autoWrap())
        m_minWidth = m_maxWidth;

    if (styleToUse->whiteSpace() == PRE) {
        if (firstLine)
            m_firstLineMinWidth = m_maxWidth;
        m_lastLineLineMinWidth = currMaxWidth;
    }

    clearPreferredLogicalWidthsDirty();
}

bool RenderText::isAllCollapsibleWhitespace() const
{
    unsigned length = textLength();
    if (is8Bit()) {
        for (unsigned i = 0; i < length; ++i) {
            if (!style()->isCollapsibleWhiteSpace(characters8()[i]))
                return false;
        }
        return true;
    }
    for (unsigned i = 0; i < length; ++i) {
        if (!style()->isCollapsibleWhiteSpace(characters16()[i]))
            return false;
    }
    return true;
}

bool RenderText::containsOnlyWhitespace(unsigned from, unsigned len) const
{
    ASSERT(m_text);
    StringImpl& text = *m_text.impl();
    unsigned currPos;
    for (currPos = from;
         currPos < from + len && (text[currPos] == '\n' || text[currPos] == ' ' || text[currPos] == '\t');
         currPos++) { }
    return currPos >= (from + len);
}

FloatPoint RenderText::firstRunOrigin() const
{
    return IntPoint(firstRunX(), firstRunY());
}

float RenderText::firstRunX() const
{
    return m_firstTextBox ? m_firstTextBox->x() : 0;
}

float RenderText::firstRunY() const
{
    return m_firstTextBox ? m_firstTextBox->y() : 0;
}

void RenderText::setSelectionState(SelectionState state)
{
    RenderObject::setSelectionState(state);

    if (canUpdateSelectionOnRootLineBoxes()) {
        if (state == SelectionStart || state == SelectionEnd || state == SelectionBoth) {
            int startPos, endPos;
            selectionStartEnd(startPos, endPos);
            if (selectionState() == SelectionStart) {
                endPos = textLength();

                // to handle selection from end of text to end of line
                if (startPos && startPos == endPos)
                    startPos = endPos - 1;
            } else if (selectionState() == SelectionEnd)
                startPos = 0;

            for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox()) {
                if (box->isSelected(startPos, endPos)) {
                    box->root().setHasSelectedChildren(true);
                }
            }
        } else {
            for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox()) {
                box->root().setHasSelectedChildren(state == SelectionInside);
            }
        }
    }

    // The containing block can be null in case of an orphaned tree.
    RenderBlock* containingBlock = this->containingBlock();
    if (containingBlock && !containingBlock->isRenderView())
        containingBlock->setSelectionState(state);
}

void RenderText::setTextWithOffset(PassRefPtr<StringImpl> text, unsigned offset, unsigned len, bool force)
{
    if (!force && equal(m_text.impl(), text.get()))
        return;

    unsigned oldLen = textLength();
    unsigned newLen = text->length();
    int delta = newLen - oldLen;
    unsigned end = len ? offset + len - 1 : offset;

    RootInlineBox* firstRootBox = 0;
    RootInlineBox* lastRootBox = 0;

    bool dirtiedLines = false;

    // Dirty all text boxes that include characters in between offset and offset+len.
    for (InlineTextBox* curr = firstTextBox(); curr; curr = curr->nextTextBox()) {
        // FIXME: This shouldn't rely on the end of a dirty line box. See https://bugs.webkit.org/show_bug.cgi?id=97264
        // Text run is entirely before the affected range.
        if (curr->end() < offset)
            continue;

        // Text run is entirely after the affected range.
        if (curr->start() > end) {
            curr->offsetRun(delta);
            RootInlineBox* root = &curr->root();
            if (!firstRootBox) {
                firstRootBox = root;
                // The affected area was in between two runs. Go ahead and mark the root box of
                // the run after the affected area as dirty.
                firstRootBox->markDirty();
                dirtiedLines = true;
            }
            lastRootBox = root;
        } else if (curr->end() >= offset && curr->end() <= end) {
            // Text run overlaps with the left end of the affected range.
            curr->dirtyLineBoxes();
            dirtiedLines = true;
        } else if (curr->start() <= offset && curr->end() >= end) {
            // Text run subsumes the affected range.
            curr->dirtyLineBoxes();
            dirtiedLines = true;
        } else if (curr->start() <= end && curr->end() >= end) {
            // Text run overlaps with right end of the affected range.
            curr->dirtyLineBoxes();
            dirtiedLines = true;
        }
    }

    // Now we have to walk all of the clean lines and adjust their cached line break information
    // to reflect our updated offsets.
    if (lastRootBox)
        lastRootBox = lastRootBox->nextRootBox();
    if (firstRootBox) {
        RootInlineBox* prev = firstRootBox->prevRootBox();
        if (prev)
            firstRootBox = prev;
    } else if (lastTextBox()) {
        ASSERT(!lastRootBox);
        firstRootBox = &lastTextBox()->root();
        firstRootBox->markDirty();
        dirtiedLines = true;
    }
    for (RootInlineBox* curr = firstRootBox; curr && curr != lastRootBox; curr = curr->nextRootBox()) {
        if (curr->lineBreakObj() == this && curr->lineBreakPos() > end)
            curr->setLineBreakPos(clampToInteger(curr->lineBreakPos() + delta));
    }

    // If the text node is empty, dirty the line where new text will be inserted.
    if (!firstTextBox() && parent()) {
        parent()->dirtyLinesFromChangedChild(this);
        dirtiedLines = true;
    }

    m_linesDirty = dirtiedLines;
    setText(text, force || dirtiedLines);
}

void RenderText::transformText()
{
    if (RefPtr<StringImpl> textToTransform = originalText())
        setText(textToTransform.release(), true);
}

static inline bool isInlineFlowOrEmptyText(const RenderObject* o)
{
    if (o->isRenderInline())
        return true;
    if (!o->isText())
        return false;
    return toRenderText(o)->text().isEmpty();
}

UChar RenderText::previousCharacter() const
{
    // find previous text renderer if one exists
    const RenderObject* previousText = previousInPreOrder();
    for (; previousText; previousText = previousText->previousInPreOrder())
        if (!isInlineFlowOrEmptyText(previousText))
            break;
    UChar prev = ' ';
    if (previousText && previousText->isText())
        if (StringImpl* previousString = toRenderText(previousText)->text().impl())
            prev = (*previousString)[previousString->length() - 1];
    return prev;
}

void RenderText::addLayerHitTestRects(LayerHitTestRects&, const RenderLayer* currentLayer, const LayoutPoint& layerOffset, const LayoutRect& containerRect) const
{
    // Text nodes aren't event targets, so don't descend any further.
}

void applyTextTransform(const RenderStyle* style, String& text, UChar previousCharacter)
{
    if (!style)
        return;

    switch (style->textTransform()) {
    case TTNONE:
        break;
    case CAPITALIZE:
        makeCapitalized(&text, previousCharacter);
        break;
    case UPPERCASE:
        text = text.upper(style->locale());
        break;
    case LOWERCASE:
        text = text.lower(style->locale());
        break;
    }
}

void RenderText::setTextInternal(PassRefPtr<StringImpl> text)
{
    ASSERT(text);
    m_text = text;

    if (style()) {
        applyTextTransform(style(), m_text, previousCharacter());

        // We use the same characters here as for list markers.
        // See the listMarkerText function in RenderListMarker.cpp.
        switch (style()->textSecurity()) {
        case TSNONE:
            break;
        case TSCIRCLE:
            secureText(whiteBullet);
            break;
        case TSDISC:
            secureText(bullet);
            break;
        case TSSQUARE:
            secureText(blackSquare);
        }
    }

    ASSERT(m_text);
    ASSERT(!isBR() || (textLength() == 1 && m_text[0] == '\n'));

    m_isAllASCII = m_text.containsOnlyASCII();
    m_canUseSimpleFontCodePath = computeCanUseSimpleFontCodePath();
}

void RenderText::secureText(UChar mask)
{
    if (!m_text.length())
        return;

    int lastTypedCharacterOffsetToReveal = -1;
    UChar revealedText;
    SecureTextTimer* secureTextTimer = gSecureTextTimers ? gSecureTextTimers->get(this) : 0;
    if (secureTextTimer && secureTextTimer->isActive()) {
        lastTypedCharacterOffsetToReveal = secureTextTimer->lastTypedCharacterOffset();
        if (lastTypedCharacterOffsetToReveal >= 0)
            revealedText = m_text[lastTypedCharacterOffsetToReveal];
    }

    m_text.fill(mask);
    if (lastTypedCharacterOffsetToReveal >= 0) {
        m_text.replace(lastTypedCharacterOffsetToReveal, 1, String(&revealedText, 1));
        // m_text may be updated later before timer fires. We invalidate the lastTypedCharacterOffset to avoid inconsistency.
        secureTextTimer->invalidate();
    }
}

void RenderText::setText(PassRefPtr<StringImpl> text, bool force)
{
    ASSERT(text);

    if (!force && equal(m_text.impl(), text.get()))
        return;

    setTextInternal(text);
    // If preferredLogicalWidthsDirty() of an orphan child is true, RenderObjectChildList::
    // insertChildNode() fails to set true to owner. To avoid that, we call
    // setNeedsLayoutAndPrefWidthsRecalc() only if this RenderText has parent.
    if (parent())
        setNeedsLayoutAndPrefWidthsRecalcAndFullPaintInvalidation();
    m_knownToHaveNoOverflowAndNoFallbackFonts = false;

    if (AXObjectCache* cache = document().existingAXObjectCache())
        cache->textChanged(this);
}

void RenderText::dirtyLineBoxes(bool fullLayout)
{
    if (fullLayout)
        deleteTextBoxes();
    else if (!m_linesDirty) {
        for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox())
            box->dirtyLineBoxes();
    }
    m_linesDirty = false;
}

InlineTextBox* RenderText::createTextBox()
{
    return new InlineTextBox(*this);
}

InlineTextBox* RenderText::createInlineTextBox()
{
    InlineTextBox* textBox = createTextBox();
    if (!m_firstTextBox)
        m_firstTextBox = m_lastTextBox = textBox;
    else {
        m_lastTextBox->setNextTextBox(textBox);
        textBox->setPreviousTextBox(m_lastTextBox);
        m_lastTextBox = textBox;
    }
    textBox->setIsText(true);
    return textBox;
}

void RenderText::positionLineBox(InlineBox* box)
{
    InlineTextBox* s = toInlineTextBox(box);

    // FIXME: should not be needed!!!
    if (!s->len()) {
        // We want the box to be destroyed.
        s->remove(DontMarkLineBoxes);
        if (m_firstTextBox == s)
            m_firstTextBox = s->nextTextBox();
        else
            s->prevTextBox()->setNextTextBox(s->nextTextBox());
        if (m_lastTextBox == s)
            m_lastTextBox = s->prevTextBox();
        else
            s->nextTextBox()->setPreviousTextBox(s->prevTextBox());
        s->destroy();
        return;
    }

    m_containsReversedText |= !s->isLeftToRightDirection();
}

float RenderText::width(unsigned from, unsigned len, float xPos, TextDirection textDirection, bool firstLine, HashSet<const SimpleFontData*>* fallbackFonts, GlyphOverflow* glyphOverflow) const
{
    if (from >= textLength())
        return 0;

    if (from + len > textLength())
        len = textLength() - from;

    return width(from, len, style(firstLine)->font(), xPos, textDirection, fallbackFonts, glyphOverflow);
}

float RenderText::width(unsigned from, unsigned len, const Font& f, float xPos, TextDirection textDirection, HashSet<const SimpleFontData*>* fallbackFonts, GlyphOverflow* glyphOverflow) const
{
    ASSERT(from + len <= textLength());
    if (!textLength())
        return 0;

    float w;
    if (&f == &style()->font()) {
        if (!style()->preserveNewline() && !from && len == textLength() && (!glyphOverflow || !glyphOverflow->computeBounds)) {
            if (fallbackFonts) {
                ASSERT(glyphOverflow);
                if (preferredLogicalWidthsDirty() || !m_knownToHaveNoOverflowAndNoFallbackFonts) {
                    const_cast<RenderText*>(this)->computePreferredLogicalWidths(0, *fallbackFonts, *glyphOverflow);
                    // We shouldn't change our mind once we "know".
                    ASSERT(!m_knownToHaveNoOverflowAndNoFallbackFonts
                        || (fallbackFonts->isEmpty() && glyphOverflow->isZero()));
                    m_knownToHaveNoOverflowAndNoFallbackFonts = fallbackFonts->isEmpty() && glyphOverflow->isZero();
                }
                w = m_maxWidth;
            } else {
                w = maxLogicalWidth();
            }
        } else {
            w = widthFromCache(f, from, len, xPos, textDirection, fallbackFonts, glyphOverflow);
        }
    } else {
        TextRun run = constructTextRun(const_cast<RenderText*>(this), f, this, from, len, style(), textDirection);
        run.setCharactersLength(textLength() - from);
        ASSERT(run.charactersLength() >= run.length());

        run.setCharacterScanForCodePath(!canUseSimpleFontCodePath());
        run.setTabSize(!style()->collapseWhiteSpace(), style()->tabSize());
        run.setXPos(xPos);
        w = f.width(run, fallbackFonts, glyphOverflow);
    }

    return w;
}

IntRect RenderText::linesBoundingBox() const
{
    IntRect result;

    ASSERT(!firstTextBox() == !lastTextBox());  // Either both are null or both exist.
    if (firstTextBox() && lastTextBox()) {
        // Return the width of the minimal left side and the maximal right side.
        float logicalLeftSide = 0;
        float logicalRightSide = 0;
        for (InlineTextBox* curr = firstTextBox(); curr; curr = curr->nextTextBox()) {
            if (curr == firstTextBox() || curr->logicalLeft() < logicalLeftSide)
                logicalLeftSide = curr->logicalLeft();
            if (curr == firstTextBox() || curr->logicalRight() > logicalRightSide)
                logicalRightSide = curr->logicalRight();
        }

        bool isHorizontal = style()->isHorizontalWritingMode();

        float x = isHorizontal ? logicalLeftSide : firstTextBox()->x();
        float y = isHorizontal ? firstTextBox()->y() : logicalLeftSide;
        float width = isHorizontal ? logicalRightSide - logicalLeftSide : lastTextBox()->logicalBottom() - x;
        float height = isHorizontal ? lastTextBox()->logicalBottom() - y : logicalRightSide - logicalLeftSide;
        result = enclosingIntRect(FloatRect(x, y, width, height));
    }

    return result;
}

LayoutRect RenderText::linesVisualOverflowBoundingBox() const
{
    if (!firstTextBox())
        return LayoutRect();

    // Return the width of the minimal left side and the maximal right side.
    LayoutUnit logicalLeftSide = LayoutUnit::max();
    LayoutUnit logicalRightSide = LayoutUnit::min();
    for (InlineTextBox* curr = firstTextBox(); curr; curr = curr->nextTextBox()) {
        LayoutRect logicalVisualOverflow = curr->logicalOverflowRect();
        logicalLeftSide = std::min(logicalLeftSide, logicalVisualOverflow.x());
        logicalRightSide = std::max(logicalRightSide, logicalVisualOverflow.maxX());
    }

    LayoutUnit logicalTop = firstTextBox()->logicalTopVisualOverflow();
    LayoutUnit logicalWidth = logicalRightSide - logicalLeftSide;
    LayoutUnit logicalHeight = lastTextBox()->logicalBottomVisualOverflow() - logicalTop;

    LayoutRect rect(logicalLeftSide, logicalTop, logicalWidth, logicalHeight);
    if (!style()->isHorizontalWritingMode())
        rect = rect.transposedRect();
    return rect;
}

LayoutRect RenderText::clippedOverflowRectForPaintInvalidation(const RenderLayerModelObject* paintInvalidationContainer, const PaintInvalidationState* paintInvalidationState) const
{
    RenderObject* rendererToRepaint = containingBlock();

    // Do not cross self-painting layer boundaries.
    RenderObject* enclosingLayerRenderer = enclosingLayer()->renderer();
    if (enclosingLayerRenderer != rendererToRepaint && !rendererToRepaint->isDescendantOf(enclosingLayerRenderer))
        rendererToRepaint = enclosingLayerRenderer;

    // The renderer we chose to repaint may be an ancestor of paintInvalidationContainer, but we need to do a paintInvalidationContainer-relative repaint.
    if (paintInvalidationContainer && paintInvalidationContainer != rendererToRepaint && !rendererToRepaint->isDescendantOf(paintInvalidationContainer))
        return paintInvalidationContainer->clippedOverflowRectForPaintInvalidation(paintInvalidationContainer, paintInvalidationState);

    return rendererToRepaint->clippedOverflowRectForPaintInvalidation(paintInvalidationContainer, paintInvalidationState);
}

LayoutRect RenderText::selectionRectForPaintInvalidation(const RenderLayerModelObject* paintInvalidationContainer, bool clipToVisibleContent)
{
    ASSERT(!needsLayout());

    if (selectionState() == SelectionNone)
        return LayoutRect();
    RenderBlock* cb = containingBlock();
    if (!cb)
        return LayoutRect();

    // Now calculate startPos and endPos for painting selection.
    // We include a selection while endPos > 0
    int startPos, endPos;
    if (selectionState() == SelectionInside) {
        // We are fully selected.
        startPos = 0;
        endPos = textLength();
    } else {
        selectionStartEnd(startPos, endPos);
        if (selectionState() == SelectionStart)
            endPos = textLength();
        else if (selectionState() == SelectionEnd)
            startPos = 0;
    }

    if (startPos == endPos)
        return IntRect();

    LayoutRect rect;
    for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox()) {
        rect.unite(box->localSelectionRect(startPos, endPos));
        rect.unite(ellipsisRectForBox(box, startPos, endPos));
    }

    if (clipToVisibleContent) {
        mapRectToPaintInvalidationBacking(paintInvalidationContainer, rect, IsNotFixedPosition, 0);
    } else {
        if (cb->hasColumns())
            cb->adjustRectForColumns(rect);

        rect = localToContainerQuad(FloatRect(rect), paintInvalidationContainer).enclosingBoundingBox();
    }

    return rect;
}

int RenderText::caretMinOffset() const
{
    InlineTextBox* box = firstTextBox();
    if (!box)
        return 0;
    int minOffset = box->start();
    for (box = box->nextTextBox(); box; box = box->nextTextBox())
        minOffset = std::min<int>(minOffset, box->start());
    return minOffset;
}

int RenderText::caretMaxOffset() const
{
    InlineTextBox* box = lastTextBox();
    if (!lastTextBox())
        return textLength();

    int maxOffset = box->start() + box->len();
    for (box = box->prevTextBox(); box; box = box->prevTextBox())
        maxOffset = std::max<int>(maxOffset, box->start() + box->len());
    return maxOffset;
}

unsigned RenderText::renderedTextLength() const
{
    int l = 0;
    for (InlineTextBox* box = firstTextBox(); box; box = box->nextTextBox())
        l += box->len();
    return l;
}

int RenderText::previousOffset(int current) const
{
    if (isAllASCII() || m_text.is8Bit())
        return current - 1;

    StringImpl* textImpl = m_text.impl();
    TextBreakIterator* iterator = cursorMovementIterator(textImpl->characters16(), textImpl->length());
    if (!iterator)
        return current - 1;

    long result = iterator->preceding(current);
    if (result == TextBreakDone)
        result = current - 1;


    return result;
}

#if OS(POSIX)

#define HANGUL_CHOSEONG_START (0x1100)
#define HANGUL_CHOSEONG_END (0x115F)
#define HANGUL_JUNGSEONG_START (0x1160)
#define HANGUL_JUNGSEONG_END (0x11A2)
#define HANGUL_JONGSEONG_START (0x11A8)
#define HANGUL_JONGSEONG_END (0x11F9)
#define HANGUL_SYLLABLE_START (0xAC00)
#define HANGUL_SYLLABLE_END (0xD7AF)
#define HANGUL_JONGSEONG_COUNT (28)

enum HangulState {
    HangulStateL,
    HangulStateV,
    HangulStateT,
    HangulStateLV,
    HangulStateLVT,
    HangulStateBreak
};

inline bool isHangulLVT(UChar32 character)
{
    return (character - HANGUL_SYLLABLE_START) % HANGUL_JONGSEONG_COUNT;
}

inline bool isMark(UChar32 c)
{
    int8_t charType = u_charType(c);
    return charType == U_NON_SPACING_MARK || charType == U_ENCLOSING_MARK || charType == U_COMBINING_SPACING_MARK;
}

inline bool isRegionalIndicator(UChar32 c)
{
    // National flag emoji each consists of a pair of regional indicator symbols.
    return 0x1F1E6 <= c && c <= 0x1F1FF;
}

#endif

int RenderText::previousOffsetForBackwardDeletion(int current) const
{
#if OS(POSIX)
    ASSERT(m_text);
    StringImpl& text = *m_text.impl();
    UChar32 character;
    bool sawRegionalIndicator = false;
    while (current > 0) {
        if (U16_IS_TRAIL(text[--current]))
            --current;
        if (current < 0)
            break;

        UChar32 character = text.characterStartingAt(current);

        if (sawRegionalIndicator) {
            // We don't check if the pair of regional indicator symbols before current position can actually be combined
            // into a flag, and just delete it. This may not agree with how the pair is rendered in edge cases,
            // but is good enough in practice.
            if (isRegionalIndicator(character))
                break;
            // Don't delete a preceding character that isn't a regional indicator symbol.
            U16_FWD_1_UNSAFE(text, current);
        }

        // We don't combine characters in Armenian ... Limbu range for backward deletion.
        if ((character >= 0x0530) && (character < 0x1950))
            break;

        if (isRegionalIndicator(character)) {
            sawRegionalIndicator = true;
            continue;
        }

        if (!isMark(character) && (character != 0xFF9E) && (character != 0xFF9F))
            break;
    }

    if (current <= 0)
        return current;

    // Hangul
    character = text.characterStartingAt(current);
    if (((character >= HANGUL_CHOSEONG_START) && (character <= HANGUL_JONGSEONG_END)) || ((character >= HANGUL_SYLLABLE_START) && (character <= HANGUL_SYLLABLE_END))) {
        HangulState state;

        if (character < HANGUL_JUNGSEONG_START)
            state = HangulStateL;
        else if (character < HANGUL_JONGSEONG_START)
            state = HangulStateV;
        else if (character < HANGUL_SYLLABLE_START)
            state = HangulStateT;
        else
            state = isHangulLVT(character) ? HangulStateLVT : HangulStateLV;

        while (current > 0 && ((character = text.characterStartingAt(current - 1)) >= HANGUL_CHOSEONG_START) && (character <= HANGUL_SYLLABLE_END) && ((character <= HANGUL_JONGSEONG_END) || (character >= HANGUL_SYLLABLE_START))) {
            switch (state) {
            case HangulStateV:
                if (character <= HANGUL_CHOSEONG_END)
                    state = HangulStateL;
                else if ((character >= HANGUL_SYLLABLE_START) && (character <= HANGUL_SYLLABLE_END) && !isHangulLVT(character))
                    state = HangulStateLV;
                else if (character > HANGUL_JUNGSEONG_END)
                    state = HangulStateBreak;
                break;
            case HangulStateT:
                if ((character >= HANGUL_JUNGSEONG_START) && (character <= HANGUL_JUNGSEONG_END))
                    state = HangulStateV;
                else if ((character >= HANGUL_SYLLABLE_START) && (character <= HANGUL_SYLLABLE_END))
                    state = (isHangulLVT(character) ? HangulStateLVT : HangulStateLV);
                else if (character < HANGUL_JUNGSEONG_START)
                    state = HangulStateBreak;
                break;
            default:
                state = (character < HANGUL_JUNGSEONG_START) ? HangulStateL : HangulStateBreak;
                break;
            }
            if (state == HangulStateBreak)
                break;

            --current;
        }
    }

    return current;
#else
    // Platforms other than Unix-like delete by one code point.
    if (U16_IS_TRAIL(m_text[--current]))
        --current;
    if (current < 0)
        current = 0;
    return current;
#endif
}

int RenderText::nextOffset(int current) const
{
    if (isAllASCII() || m_text.is8Bit())
        return current + 1;

    StringImpl* textImpl = m_text.impl();
    TextBreakIterator* iterator = cursorMovementIterator(textImpl->characters16(), textImpl->length());
    if (!iterator)
        return current + 1;

    long result = iterator->following(current);
    if (result == TextBreakDone)
        result = current + 1;

    return result;
}

bool RenderText::computeCanUseSimpleFontCodePath() const
{
    if (isAllASCII() || m_text.is8Bit())
        return true;
    return Character::characterRangeCodePath(characters16(), length()) == SimplePath;
}

#if ENABLE(ASSERT)

void RenderText::checkConsistency() const
{
#ifdef CHECK_CONSISTENCY
    const InlineTextBox* prev = 0;
    for (const InlineTextBox* child = m_firstTextBox; child != 0; child = child->nextTextBox()) {
        ASSERT(child->renderer() == this);
        ASSERT(child->prevTextBox() == prev);
        prev = child;
    }
    ASSERT(prev == m_lastTextBox);
#endif
}

#endif

void RenderText::momentarilyRevealLastTypedCharacter(unsigned lastTypedCharacterOffset)
{
    if (!gSecureTextTimers)
        gSecureTextTimers = new SecureTextTimerMap;

    SecureTextTimer* secureTextTimer = gSecureTextTimers->get(this);
    if (!secureTextTimer) {
        secureTextTimer = new SecureTextTimer(this);
        gSecureTextTimers->add(this, secureTextTimer);
    }
    secureTextTimer->restartWithNewText(lastTypedCharacterOffset);
}

PassRefPtr<AbstractInlineTextBox> RenderText::firstAbstractInlineTextBox()
{
    return AbstractInlineTextBox::getOrCreate(this, m_firstTextBox);
}

} // namespace blink
