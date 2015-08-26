/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef Internals_h
#define Internals_h

#include "bindings/core/v8/ExceptionStatePlaceholder.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptValue.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "core/css/CSSComputedStyleDeclaration.h"
#include "core/dom/ContextLifecycleObserver.h"
#include "core/page/scrolling/ScrollingCoordinator.h"
#include "platform/heap/Handle.h"
#include "wtf/ArrayBuffer.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefCounted.h"
#include "wtf/text/WTFString.h"

namespace blink {

class CanvasRenderingContext2D;
class ClientRect;
class ClientRectList;
class WebKitPoint;
class DOMStringList;
class LocalDOMWindow;
class Document;
class DocumentMarker;
class Element;
class ExceptionState;
class ExecutionContext;
class GCObservation;
class HTMLMediaElement;
class InternalProfilers;
class InternalRuntimeFlags;
class InternalSettings;
class LayerRectList;
class LocalFrame;
class Node;
class Page;
class PagePopupController;
class PrivateScriptTest;
class Range;
class SerializedScriptValue;
class ShadowRoot;
template <typename NodeType> class StaticNodeTypeList;
typedef StaticNodeTypeList<Node> StaticNodeList;
class TypeConversions;

class Internals FINAL : public RefCountedWillBeGarbageCollectedFinalized<Internals>, public ScriptWrappable, public ContextLifecycleObserver {
public:
    static PassRefPtrWillBeRawPtr<Internals> create(Document*);
    virtual ~Internals();

    static void resetToConsistentState(Page*);

    String elementRenderTreeAsText(Element*, ExceptionState&);

    String address(Node*);

    PassRefPtrWillBeRawPtr<GCObservation> observeGC(ScriptValue);

    bool isPreloaded(const String& url);
    bool isLoadingFromMemoryCache(const String& url);

    void crash();

    void setStyleResolverStatsEnabled(bool);
    String styleResolverStatsReport(ExceptionState&) const;
    String styleResolverStatsTotalsReport(ExceptionState&) const;

    bool isSharingStyle(Element*, Element*) const;

    PassRefPtrWillBeRawPtr<CSSStyleDeclaration> computedStyleIncludingVisitedInfo(Node*) const;

    PassRefPtrWillBeRawPtr<ShadowRoot> createUserAgentShadowRoot(Element* host);

    ShadowRoot* shadowRoot(Element* host);
    ShadowRoot* youngestShadowRoot(Element* host);
    ShadowRoot* oldestShadowRoot(Element* host);
    ShadowRoot* youngerShadowRoot(Node* shadow, ExceptionState&);
    String shadowRootType(const Node*, ExceptionState&) const;
    bool hasShadowInsertionPoint(const Node*, ExceptionState&) const;
    bool hasContentElement(const Node*, ExceptionState&) const;
    size_t countElementShadow(const Node*, ExceptionState&) const;
    const AtomicString& shadowPseudoId(Element*);
    void setShadowPseudoId(Element*, const AtomicString&);

    // CSS Animation / Transition testing.
    void pauseAnimations(double pauseTime, ExceptionState&);

    bool isValidContentSelect(Element* insertionPoint, ExceptionState&);
    Node* treeScopeRootNode(Node*);
    Node* parentTreeScope(Node*);
    bool hasSelectorForIdInShadow(Element* host, const AtomicString& idValue, ExceptionState&);
    bool hasSelectorForClassInShadow(Element* host, const AtomicString& className, ExceptionState&);
    bool hasSelectorForAttributeInShadow(Element* host, const AtomicString& attributeName, ExceptionState&);
    bool hasSelectorForPseudoClassInShadow(Element* host, const String& pseudoClass, ExceptionState&);
    unsigned short compareTreeScopePosition(const Node*, const Node*, ExceptionState&) const;

    // FIXME: Rename these functions if walker is preferred.
    Node* nextSiblingByWalker(Node*);
    Node* firstChildByWalker(Node*);
    Node* lastChildByWalker(Node*);
    Node* nextNodeByWalker(Node*);
    Node* previousNodeByWalker(Node*);

    unsigned updateStyleAndReturnAffectedElementCount(ExceptionState&) const;
    unsigned needsLayoutCount(ExceptionState&) const;
    unsigned hitTestCount(Document*, ExceptionState&) const;

    String visiblePlaceholder(Element*);
    void selectColorInColorChooser(Element*, const String& colorValue);
    bool hasAutofocusRequest(Document*);
    bool hasAutofocusRequest();
    Vector<String> formControlStateOfHistoryItem(ExceptionState&);
    void setFormControlStateOfHistoryItem(const Vector<String>&, ExceptionState&);
    void setEnableMockPagePopup(bool, ExceptionState&);
    PassRefPtrWillBeRawPtr<PagePopupController> pagePopupController();

    PassRefPtrWillBeRawPtr<ClientRect> unscaledViewportRect(ExceptionState&);

    PassRefPtrWillBeRawPtr<ClientRect> absoluteCaretBounds(ExceptionState&);

    PassRefPtrWillBeRawPtr<ClientRect> boundingBox(Element*);

    unsigned markerCountForNode(Node*, const String&, ExceptionState&);
    unsigned activeMarkerCountForNode(Node*);
    PassRefPtrWillBeRawPtr<Range> markerRangeForNode(Node*, const String& markerType, unsigned index, ExceptionState&);
    String markerDescriptionForNode(Node*, const String& markerType, unsigned index, ExceptionState&);
    void addTextMatchMarker(const Range*, bool isActive);
    void setMarkersActive(Node*, unsigned startOffset, unsigned endOffset, bool);
    void setMarkedTextMatchesAreHighlighted(Document*, bool);

    void setScrollViewPosition(Document*, long x, long y, ExceptionState&);
    String viewportAsText(Document*, float devicePixelRatio, int availableWidth, int availableHeight, ExceptionState&);

    bool wasLastChangeUserEdit(Element* textField, ExceptionState&);
    bool elementShouldAutoComplete(Element* inputElement, ExceptionState&);
    String suggestedValue(Element*, ExceptionState&);
    void setSuggestedValue(Element*, const String&, ExceptionState&);
    void setEditingValue(Element* inputElement, const String&, ExceptionState&);
    void setAutofilled(Element*, bool enabled, ExceptionState&);
    void scrollElementToRect(Element*, long x, long y, long w, long h, ExceptionState&);

    PassRefPtrWillBeRawPtr<Range> rangeFromLocationAndLength(Element* scope, int rangeLocation, int rangeLength);
    unsigned locationFromRange(Element* scope, const Range*);
    unsigned lengthFromRange(Element* scope, const Range*);
    String rangeAsText(const Range*);

    PassRefPtrWillBeRawPtr<WebKitPoint> touchPositionAdjustedToBestClickableNode(long x, long y, long width, long height, Document*, ExceptionState&);
    Node* touchNodeAdjustedToBestClickableNode(long x, long y, long width, long height, Document*, ExceptionState&);
    PassRefPtrWillBeRawPtr<WebKitPoint> touchPositionAdjustedToBestContextMenuNode(long x, long y, long width, long height, Document*, ExceptionState&);
    Node* touchNodeAdjustedToBestContextMenuNode(long x, long y, long width, long height, Document*, ExceptionState&);
    PassRefPtrWillBeRawPtr<ClientRect> bestZoomableAreaForTouchPoint(long x, long y, long width, long height, Document*, ExceptionState&);

    int lastSpellCheckRequestSequence(Document*, ExceptionState&);
    int lastSpellCheckProcessedSequence(Document*, ExceptionState&);

    Vector<AtomicString> userPreferredLanguages() const;
    void setUserPreferredLanguages(const Vector<String>&);

    unsigned activeDOMObjectCount(Document*);
    unsigned wheelEventHandlerCount(Document*);
    unsigned scrollEventHandlerCount(Document*);
    unsigned touchEventHandlerCount(Document*);
    PassRefPtrWillBeRawPtr<LayerRectList> touchEventTargetLayerRects(Document*, ExceptionState&);

    // This is used to test rect based hit testing like what's done on touch screens.
    PassRefPtrWillBeRawPtr<StaticNodeList> nodesFromRect(Document*, int x, int y, unsigned topPadding, unsigned rightPadding,
        unsigned bottomPadding, unsigned leftPadding, bool ignoreClipping, bool allowChildFrameContent, ExceptionState&) const;

    void emitInspectorDidBeginFrame(int frameId = 0);
    void emitInspectorDidCancelFrame();

    bool hasSpellingMarker(Document*, int from, int length);
    bool hasGrammarMarker(Document*, int from, int length);
    void setContinuousSpellCheckingEnabled(bool);

    bool isOverwriteModeEnabled(Document*);
    void toggleOverwriteModeEnabled(Document*);

    unsigned numberOfScrollableAreas(Document*);

    bool isPageBoxVisible(Document*, int pageNumber);

    static const char* internalsId;

    InternalSettings* settings() const;
    InternalRuntimeFlags* runtimeFlags() const;
    InternalProfilers* profilers();
    unsigned workerThreadCount() const;

    void setDeviceProximity(Document*, const String& eventType, double value, double min, double max, ExceptionState&);

    String layerTreeAsText(Document*, unsigned flags, ExceptionState&) const;
    String layerTreeAsText(Document*, ExceptionState&) const;
    String elementLayerTreeAsText(Element*, unsigned flags, ExceptionState&) const;
    String elementLayerTreeAsText(Element*, ExceptionState&) const;

    bool scrollsWithRespectTo(Element*, Element*, ExceptionState&);
    bool isUnclippedDescendant(Element*, ExceptionState&);

    String scrollingStateTreeAsText(Document*) const;
    String mainThreadScrollingReasons(Document*, ExceptionState&) const;
    PassRefPtrWillBeRawPtr<ClientRectList> nonFastScrollableRects(Document*, ExceptionState&) const;

    void garbageCollectDocumentResources(Document*) const;
    void evictAllResources() const;

    unsigned numberOfLiveNodes() const;
    unsigned numberOfLiveDocuments() const;
    String dumpRefCountedInstanceCounts() const;
    Vector<String> consoleMessageArgumentCounts(Document*) const;
    PassRefPtrWillBeRawPtr<LocalDOMWindow> openDummyInspectorFrontend(const String& url);
    void closeDummyInspectorFrontend();
    Vector<unsigned long> setMemoryCacheCapacities(unsigned long minDeadBytes, unsigned long maxDeadBytes, unsigned long totalBytes);
    void setInspectorResourcesDataSizeLimits(int maximumResourcesContentSize, int maximumSingleResourceContentSize, ExceptionState&);

    String counterValue(Element*);

    int pageNumber(Element*, float pageWidth = 800, float pageHeight = 600);
    Vector<String> shortcutIconURLs(Document*) const;
    Vector<String> allIconURLs(Document*) const;

    int numberOfPages(float pageWidthInPixels = 800, float pageHeightInPixels = 600);
    String pageProperty(String, int, ExceptionState& = ASSERT_NO_EXCEPTION) const;
    String pageSizeAndMarginsInPixels(int, int, int, int, int, int, int, ExceptionState& = ASSERT_NO_EXCEPTION) const;

    void setDeviceScaleFactor(float scaleFactor, ExceptionState&);

    void setIsCursorVisible(Document*, bool, ExceptionState&);

    void mediaPlayerRequestFullscreen(HTMLMediaElement*);
    double effectiveMediaVolume(HTMLMediaElement*);

    void registerURLSchemeAsBypassingContentSecurityPolicy(const String& scheme);
    void removeURLSchemeRegisteredAsBypassingContentSecurityPolicy(const String& scheme);

    PassRefPtrWillBeRawPtr<TypeConversions> typeConversions() const;
    PrivateScriptTest* privateScriptTest() const;

    Vector<String> getReferencedFilePaths() const;

    void startTrackingRepaints(Document*, ExceptionState&);
    void stopTrackingRepaints(Document*, ExceptionState&);
    void updateLayoutIgnorePendingStylesheetsAndRunPostLayoutTasks(ExceptionState&);
    void updateLayoutIgnorePendingStylesheetsAndRunPostLayoutTasks(Node*, ExceptionState&);
    void forceFullRepaint(Document*, ExceptionState&);

    PassRefPtrWillBeRawPtr<ClientRectList> draggableRegions(Document*, ExceptionState&);
    PassRefPtrWillBeRawPtr<ClientRectList> nonDraggableRegions(Document*, ExceptionState&);

    PassRefPtr<ArrayBuffer> serializeObject(PassRefPtr<SerializedScriptValue>) const;
    PassRefPtr<SerializedScriptValue> deserializeBuffer(PassRefPtr<ArrayBuffer>) const;

    String getCurrentCursorInfo(Document*, ExceptionState&);

    String markerTextForListItem(Element*);

    void forceReload(bool endToEnd);

    String getImageSourceURL(Element*);

    bool isSelectPopupVisible(Node*);
    bool selectPopupItemStyleIsRtl(Node*, int);
    int selectPopupItemStyleFontHeight(Node*, int);

    PassRefPtrWillBeRawPtr<ClientRect> selectionBounds(ExceptionState&);
    String baseURL(Document*);

    bool loseSharedGraphicsContext3D();

    void forceCompositingUpdate(Document*, ExceptionState&);

    void setZoomFactor(float);

    void setShouldRevealPassword(Element*, bool, ExceptionState&);

    ScriptPromise createResolvedPromise(ScriptState*, ScriptValue);
    ScriptPromise createRejectedPromise(ScriptState*, ScriptValue);
    ScriptPromise addOneToPromise(ScriptState*, ScriptPromise);
    ScriptPromise promiseCheck(ScriptState*, long, bool, const Dictionary&, const String&, const Vector<String>&, ExceptionState&);
    ScriptPromise promiseCheckWithoutExceptionState(ScriptState*, const Dictionary&, const String&, const Vector<String>&);
    ScriptPromise promiseCheckRange(ScriptState*, long);

    void trace(Visitor*);

    void setValueForUser(Element*, const String&);

    String textSurroundingNode(Node*, int x, int y, unsigned long maxLength);

    void setFocused(bool);

    bool ignoreLayoutWithPendingStylesheets(Document*);

    void setNetworkStateNotifierTestOnly(bool);
    // Test must call setNetworkStateNotifierTestOnly(true) before calling setNetworkConnectionInfo.
    void setNetworkConnectionInfo(const String&, ExceptionState&);
    String serializeNavigationMarkup();
    void hideAllTransitionElements();

    unsigned countHitRegions(CanvasRenderingContext2D*);

private:
    explicit Internals(Document*);
    Document* contextDocument() const;
    LocalFrame* frame() const;
    Vector<String> iconURLs(Document*, int iconTypesMask) const;
    PassRefPtrWillBeRawPtr<ClientRectList> annotatedRegions(Document*, bool draggable, ExceptionState&);

    DocumentMarker* markerAt(Node*, const String& markerType, unsigned index, ExceptionState&);
    RefPtrWillBeMember<InternalRuntimeFlags> m_runtimeFlags;
    RefPtrWillBeMember<InternalProfilers> m_profilers;
};

} // namespace blink

#endif
