/*
 * Copyright (C) 2007 Apple Inc.  All rights reserved.
 * Copyright (C) 2009 Joseph Pecoraro
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
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

/**
 * @constructor
 * @extends {WebInspector.SidebarPane}
 * @param {!WebInspector.ComputedStyleSidebarPane} computedStylePane
 * @param {function(!WebInspector.DOMNode, string, boolean)=} setPseudoClassCallback
 */
WebInspector.StylesSidebarPane = function(computedStylePane, setPseudoClassCallback)
{
    WebInspector.SidebarPane.call(this, WebInspector.UIString("Styles"));

    this._elementStateButton = document.createElement("button");
    this._elementStateButton.className = "pane-title-button element-state";
    this._elementStateButton.title = WebInspector.UIString("Toggle Element State");
    this._elementStateButton.addEventListener("click", this._toggleElementStatePane.bind(this), false);
    this.titleElement.appendChild(this._elementStateButton);

    var addButton = document.createElement("button");
    addButton.className = "pane-title-button add";
    addButton.id = "add-style-button-test-id";
    addButton.title = WebInspector.UIString("New Style Rule");
    addButton.addEventListener("click", this._createNewRuleInViaInspectorStyleSheet.bind(this), false);
    this.titleElement.appendChild(addButton);

    this._computedStylePane = computedStylePane;
    computedStylePane.setHostingPane(this);
    this._setPseudoClassCallback = setPseudoClassCallback;
    this.element.addEventListener("contextmenu", this._contextMenuEventFired.bind(this), true);
    WebInspector.settings.colorFormat.addChangeListener(this._colorFormatSettingChanged.bind(this));
    WebInspector.settings.showUserAgentStyles.addChangeListener(this._showUserAgentStylesSettingChanged.bind(this));

    this._createElementStatePane();
    this.bodyElement.appendChild(this._elementStatePane);
    this._sectionsContainer = document.createElement("div");
    this.bodyElement.appendChild(this._sectionsContainer);

    this._spectrumHelper = new WebInspector.SpectrumPopupHelper();
    this._linkifier = new WebInspector.Linkifier(new WebInspector.Linkifier.DefaultCSSFormatter());

    this.element.classList.add("styles-pane");
    this.element.classList.toggle("show-user-styles", WebInspector.settings.showUserAgentStyles.get());
    this.element.addEventListener("mousemove", this._mouseMovedOverElement.bind(this), false);
    document.body.addEventListener("keydown", this._keyDown.bind(this), false);
    document.body.addEventListener("keyup", this._keyUp.bind(this), false);
}

// Keep in sync with RenderStyleConstants.h PseudoId enum. Array below contains pseudo id names for corresponding enum indexes.
// First item is empty due to its artificial NOPSEUDO nature in the enum.
// FIXME: find a way of generating this mapping or getting it from combination of RenderStyleConstants and CSSSelector.cpp at
// runtime.
WebInspector.StylesSidebarPane.PseudoIdNames = [
    "", "first-line", "first-letter", "before", "after", "backdrop", "selection", "", "-webkit-scrollbar",
    "-webkit-scrollbar-thumb", "-webkit-scrollbar-button", "-webkit-scrollbar-track", "-webkit-scrollbar-track-piece",
    "-webkit-scrollbar-corner", "-webkit-resizer"
];

WebInspector.StylesSidebarPane._colorRegex = /((?:rgb|hsl)a?\([^)]+\)|#[0-9a-fA-F]{6}|#[0-9a-fA-F]{3}|\b\w+\b(?!-))/g;

/**
 * @param {!WebInspector.CSSProperty} property
 * @return {!Element}
 */
WebInspector.StylesSidebarPane.createExclamationMark = function(property)
{
    var exclamationElement = document.createElement("div");
    exclamationElement.className = "exclamation-mark" + (WebInspector.StylesSidebarPane._ignoreErrorsForProperty(property) ? "" : " warning-icon-small");
    exclamationElement.title = WebInspector.CSSMetadata.cssPropertiesMetainfo.keySet()[property.name.toLowerCase()] ? WebInspector.UIString("Invalid property value.") : WebInspector.UIString("Unknown property name.");
    return exclamationElement;
}

/**
 * @param {!WebInspector.Color} color
 */
WebInspector.StylesSidebarPane._colorFormat = function(color)
{
    const cf = WebInspector.Color.Format;
    var format;
    var formatSetting = WebInspector.settings.colorFormat.get();
    if (formatSetting === cf.Original)
        format = cf.Original;
    else if (formatSetting === cf.RGB)
        format = (color.hasAlpha() ? cf.RGBA : cf.RGB);
    else if (formatSetting === cf.HSL)
        format = (color.hasAlpha() ? cf.HSLA : cf.HSL);
    else if (!color.hasAlpha())
        format = (color.canBeShortHex() ? cf.ShortHEX : cf.HEX);
    else
        format = cf.RGBA;

    return format;
}

/**
 * @param {!WebInspector.CSSProperty} property
 */
WebInspector.StylesSidebarPane._ignoreErrorsForProperty = function(property) {
    function hasUnknownVendorPrefix(string)
    {
        return !string.startsWith("-webkit-") && /^[-_][\w\d]+-\w/.test(string);
    }

    var name = property.name.toLowerCase();

    // IE hack.
    if (name.charAt(0) === "_")
        return true;

    // IE has a different format for this.
    if (name === "filter")
        return true;

    // Common IE-specific property prefix.
    if (name.startsWith("scrollbar-"))
        return true;
    if (hasUnknownVendorPrefix(name))
        return true;

    var value = property.value.toLowerCase();

    // IE hack.
    if (value.endsWith("\9"))
        return true;
    if (hasUnknownVendorPrefix(value))
        return true;

    return false;
}

WebInspector.StylesSidebarPane.prototype = {
    /**
     * @param {!WebInspector.CSSRule} editedRule
     * @param {!WebInspector.TextRange} oldRange
     * @param {!WebInspector.TextRange} newRange
     */
    _styleSheetRuleEdited: function(editedRule, oldRange, newRange)
    {
        var styleRuleSections = this.sections[0];
        for (var i = 1; i < styleRuleSections.length; ++i)
            styleRuleSections[i]._styleSheetRuleEdited(editedRule, oldRange, newRange);
    },

    /**
     * @param {!Event} event
     */
    _contextMenuEventFired: function(event)
    {
        // We start editing upon click -> default navigation to resources panel is not available
        // Hence we add a soft context menu for hrefs.
        var contextMenu = new WebInspector.ContextMenu(event);
        contextMenu.appendApplicableItems(/** @type {!Node} */ (event.target));
        contextMenu.show();
    },

    /**
     * @param {!Element} matchedStylesElement
     * @param {!Element} computedStylesElement
     */
    setFilterBoxContainers: function(matchedStylesElement, computedStylesElement)
    {
        matchedStylesElement.appendChild(this._createCSSFilterControl());
        this._computedStylePane.setFilterBoxContainer(computedStylesElement);
    },

    /**
     * @return {!Element}
     */
    _createCSSFilterControl: function()
    {
        var filterInput = this._createPropertyFilterElement(false, searchHandler.bind(this));

        /**
         * @param {?RegExp} regex
         * @this {WebInspector.StylesSidebarPane}
         */
        function searchHandler(regex)
        {
            this._filterRegex = regex;
        }

        return filterInput;
    },

    get _forcedPseudoClasses()
    {
        return this._node ? (this._node.getUserProperty(WebInspector.CSSStyleModel.PseudoStatePropertyName) || undefined) : undefined;
    },

    _updateForcedPseudoStateInputs: function()
    {
        if (!this._node)
            return;

        var hasPseudoType = !!this._node.pseudoType();
        this._elementStateButton.classList.toggle("hidden", hasPseudoType);
        this._elementStatePane.classList.toggle("expanded", !hasPseudoType && this._elementStateButton.classList.contains("toggled"));

        var nodePseudoState = this._forcedPseudoClasses;
        if (!nodePseudoState)
            nodePseudoState = [];

        var inputs = this._elementStatePane.inputs;
        for (var i = 0; i < inputs.length; ++i)
            inputs[i].checked = nodePseudoState.indexOf(inputs[i].state) >= 0;
    },

    /**
     * @param {?WebInspector.DOMNode} node
     * @param {boolean=} forceUpdate
     */
    update: function(node, forceUpdate)
    {
        this._spectrumHelper.hide();
        this._discardElementUnderMouse();

        var refresh = false;

        if (forceUpdate)
            delete this._node;

        if (!forceUpdate && (node === this._node))
            refresh = true;

        if (node && node.nodeType() === Node.TEXT_NODE && node.parentNode)
            node = node.parentNode;

        if (node && node.nodeType() !== Node.ELEMENT_NODE)
            node = null;

        if (node) {
            this._updateTarget(node.target());
            this._node = node;
        } else
            node = this._node;

        this._updateForcedPseudoStateInputs();

        if (refresh)
            this._refreshUpdate();
        else
            this._rebuildUpdate();
    },

    /**
     * @param {!WebInspector.Target} target
     */
    _updateTarget: function(target)
    {
        if (this._target === target)
            return;
        if (this._target) {
            this._target.cssModel.removeEventListener(WebInspector.CSSStyleModel.Events.StyleSheetAdded, this._styleSheetOrMediaQueryResultChanged, this);
            this._target.cssModel.removeEventListener(WebInspector.CSSStyleModel.Events.StyleSheetRemoved, this._styleSheetOrMediaQueryResultChanged, this);
            this._target.cssModel.removeEventListener(WebInspector.CSSStyleModel.Events.StyleSheetChanged, this._styleSheetOrMediaQueryResultChanged, this);
            this._target.cssModel.removeEventListener(WebInspector.CSSStyleModel.Events.MediaQueryResultChanged, this._styleSheetOrMediaQueryResultChanged, this);
            this._target.domModel.removeEventListener(WebInspector.DOMModel.Events.AttrModified, this._attributeChanged, this);
            this._target.domModel.removeEventListener(WebInspector.DOMModel.Events.AttrRemoved, this._attributeChanged, this);
            this._target.resourceTreeModel.removeEventListener(WebInspector.ResourceTreeModel.EventTypes.FrameResized, this._frameResized, this);
        }
        this._target = target;
        this._target.cssModel.addEventListener(WebInspector.CSSStyleModel.Events.StyleSheetAdded, this._styleSheetOrMediaQueryResultChanged, this);
        this._target.cssModel.addEventListener(WebInspector.CSSStyleModel.Events.StyleSheetRemoved, this._styleSheetOrMediaQueryResultChanged, this);
        this._target.cssModel.addEventListener(WebInspector.CSSStyleModel.Events.StyleSheetChanged, this._styleSheetOrMediaQueryResultChanged, this);
        this._target.cssModel.addEventListener(WebInspector.CSSStyleModel.Events.MediaQueryResultChanged, this._styleSheetOrMediaQueryResultChanged, this);
        this._target.domModel.addEventListener(WebInspector.DOMModel.Events.AttrModified, this._attributeChanged, this);
        this._target.domModel.addEventListener(WebInspector.DOMModel.Events.AttrRemoved, this._attributeChanged, this);
        this._target.resourceTreeModel.addEventListener(WebInspector.ResourceTreeModel.EventTypes.FrameResized, this._frameResized, this);
    },

    /**
     * @param {!WebInspector.StylePropertiesSection=} editedSection
     * @param {boolean=} forceFetchComputedStyle
     * @param {function()=} userCallback
     */
    _refreshUpdate: function(editedSection, forceFetchComputedStyle, userCallback)
    {
        var callbackWrapper = function()
        {
            if (this._filterRegex)
                this._updateFilter(false);
            if (userCallback)
                userCallback();
        }.bind(this);

        if (this._refreshUpdateInProgress) {
            this._lastNodeForInnerRefresh = this._node;
            return;
        }

        var node = this._validateNode(userCallback);
        if (!node)
            return;

        /**
         * @param {?WebInspector.CSSStyleDeclaration} computedStyle
         * @this {WebInspector.StylesSidebarPane}
         */
        function computedStyleCallback(computedStyle)
        {
            delete this._refreshUpdateInProgress;

            if (this._lastNodeForInnerRefresh) {
                delete this._lastNodeForInnerRefresh;
                this._refreshUpdate(editedSection, forceFetchComputedStyle, callbackWrapper);
                return;
            }

            if (this._node === node && computedStyle)
                this._innerRefreshUpdate(node, computedStyle, editedSection);

            callbackWrapper();
        }

        if (this._computedStylePane.isShowing() || forceFetchComputedStyle) {
            this._refreshUpdateInProgress = true;
            this._target.cssModel.getComputedStyleAsync(node.id, computedStyleCallback.bind(this));
        } else {
            this._innerRefreshUpdate(node, null, editedSection);
            callbackWrapper();
        }
    },

    _rebuildUpdate: function()
    {
        if (this._rebuildUpdateInProgress) {
            this._lastNodeForInnerRebuild = this._node;
            return;
        }

        var node = this._validateNode();
        if (!node)
            return;

        this._rebuildUpdateInProgress = true;

        var resultStyles = {};

        /**
         * @param {?*} matchedResult
         * @this {WebInspector.StylesSidebarPane}
         */
        function stylesCallback(matchedResult)
        {
            delete this._rebuildUpdateInProgress;

            var lastNodeForRebuild = this._lastNodeForInnerRebuild;
            if (lastNodeForRebuild) {
                delete this._lastNodeForInnerRebuild;
                if (lastNodeForRebuild !== this._node) {
                    this._rebuildUpdate();
                    return;
                }
            }

            if (matchedResult && this._node === node) {
                resultStyles.matchedCSSRules = matchedResult.matchedCSSRules;
                resultStyles.pseudoElements = matchedResult.pseudoElements;
                resultStyles.inherited = matchedResult.inherited;
                this._innerRebuildUpdate(node, resultStyles);
            }

            if (lastNodeForRebuild) {
                // lastNodeForRebuild is the same as this.node - another rebuild has been requested.
                this._rebuildUpdate();
                return;
            }
        }

        /**
         * @param {?WebInspector.CSSStyleDeclaration} inlineStyle
         * @param {?WebInspector.CSSStyleDeclaration} attributesStyle
         */
        function inlineCallback(inlineStyle, attributesStyle)
        {
            resultStyles.inlineStyle = inlineStyle;
            resultStyles.attributesStyle = attributesStyle;
        }

        /**
         * @param {?WebInspector.CSSStyleDeclaration} computedStyle
         */
        function computedCallback(computedStyle)
        {
            resultStyles.computedStyle = computedStyle;
        }

        if (this._computedStylePane.isShowing())
            this._target.cssModel.getComputedStyleAsync(node.id, computedCallback);
        this._target.cssModel.getInlineStylesAsync(node.id, inlineCallback);
        this._target.cssModel.getMatchedStylesAsync(node.id, false, false, stylesCallback.bind(this));
    },

    /**
     * @param {function()=} userCallback
     */
    _validateNode: function(userCallback)
    {
        if (!this._node) {
            this._sectionsContainer.removeChildren();
            this._computedStylePane.bodyElement.removeChildren();
            this.sections = {};
            if (userCallback)
                userCallback();
            return null;
        }
        return this._node;
    },

    _styleSheetOrMediaQueryResultChanged: function()
    {
        if (this._userOperation || this._isEditingStyle)
            return;

        this._rebuildUpdate();
    },

    _frameResized: function()
    {
        /**
         * @this {WebInspector.StylesSidebarPane}
         */
        function refreshContents()
        {
            this._styleSheetOrMediaQueryResultChanged();
            delete this._activeTimer;
        }

        if (this._activeTimer)
            clearTimeout(this._activeTimer);

        this._activeTimer = setTimeout(refreshContents.bind(this), 100);
    },

    _attributeChanged: function(event)
    {
        // Any attribute removal or modification can affect the styles of "related" nodes.
        // Do not touch the styles if they are being edited.
        if (this._isEditingStyle || this._userOperation)
            return;

        if (!this._canAffectCurrentStyles(event.data.node))
            return;

        this._rebuildUpdate();
    },

    _canAffectCurrentStyles: function(node)
    {
        return this._node && (this._node === node || node.parentNode === this._node.parentNode || node.isAncestor(this._node));
    },

    _innerRefreshUpdate: function(node, computedStyle, editedSection)
    {
        for (var pseudoId in this.sections) {
            var styleRules = this._refreshStyleRules(this.sections[pseudoId], computedStyle);
            var usedProperties = {};
            this._markUsedProperties(styleRules, usedProperties);
            this._refreshSectionsForStyleRules(styleRules, usedProperties, editedSection);
        }
        if (computedStyle)
            this.sections[0][0].rebuildComputedTrace(this.sections[0]);

        this._nodeStylesUpdatedForTest(node, false);
    },

    _innerRebuildUpdate: function(node, styles)
    {
        this._sectionsContainer.removeChildren();
        this._computedStylePane.bodyElement.removeChildren();
        this._linkifier.reset();

        var styleRules = this._rebuildStyleRules(node, styles);
        var usedProperties = {};
        this._markUsedProperties(styleRules, usedProperties);
        this.sections[0] = this._rebuildSectionsForStyleRules(styleRules, usedProperties, null);
        var anchorElement = this.sections[0].inheritedPropertiesSeparatorElement;

        if (styles.computedStyle)
            this.sections[0][0].rebuildComputedTrace(this.sections[0]);

        for (var i = 0; i < styles.pseudoElements.length; ++i) {
            var pseudoElementCSSRules = styles.pseudoElements[i];

            styleRules = [];
            var pseudoId = pseudoElementCSSRules.pseudoId;

            var entry = { isStyleSeparator: true, pseudoId: pseudoId };
            styleRules.push(entry);

            // Add rules in reverse order to match the cascade order.
            for (var j = pseudoElementCSSRules.rules.length - 1; j >= 0; --j) {
                var rule = pseudoElementCSSRules.rules[j];
                styleRules.push({ style: rule.style, selectorText: rule.selectorText, media: rule.media, rule: rule, editable: !!(rule.style && rule.style.styleSheetId) });
            }
            usedProperties = {};
            this._markUsedProperties(styleRules, usedProperties);
            this.sections[pseudoId] = this._rebuildSectionsForStyleRules(styleRules, usedProperties, anchorElement);
        }

        if (this._filterRegex)
            this._updateFilter(false);
        this._nodeStylesUpdatedForTest(node, true);
    },

    _nodeStylesUpdatedForTest: function(node, rebuild)
    {
        // Tests override this method.
    },

    _refreshStyleRules: function(sections, computedStyle)
    {
        var nodeComputedStyle = computedStyle;
        var styleRules = [];
        for (var i = 0; sections && i < sections.length; ++i) {
            var section = sections[i];
            if (section.isBlank)
                continue;
            if (section.computedStyle)
                section.styleRule.style = nodeComputedStyle;
            var styleRule = { section: section, style: section.styleRule.style, computedStyle: section.computedStyle, rule: section.rule, editable: !!(section.styleRule.style && section.styleRule.style.styleSheetId),
                isAttribute: section.styleRule.isAttribute, isInherited: section.styleRule.isInherited, parentNode: section.styleRule.parentNode };
            styleRules.push(styleRule);
        }
        return styleRules;
    },

    _rebuildStyleRules: function(node, styles)
    {
        var nodeComputedStyle = styles.computedStyle;
        this.sections = {};

        var styleRules = [];

        function addAttributesStyle()
        {
            if (!styles.attributesStyle)
                return;
            var attrStyle = { style: styles.attributesStyle, editable: false };
            attrStyle.selectorText = node.nodeNameInCorrectCase() + "[" + WebInspector.UIString("Attributes Style") + "]";
            styleRules.push(attrStyle);
        }

        styleRules.push({ computedStyle: true, selectorText: "", style: nodeComputedStyle, editable: false });

        if (!!node.pseudoType())
            styleRules.push({ isStyleSeparator: true, isPlaceholder: true });

        // Inline style has the greatest specificity.
        if (styles.inlineStyle && node.nodeType() === Node.ELEMENT_NODE) {
            var inlineStyle = { selectorText: "element.style", style: styles.inlineStyle, isAttribute: true };
            styleRules.push(inlineStyle);
        }

        // Add rules in reverse order to match the cascade order.
        var addedAttributesStyle;
        for (var i = styles.matchedCSSRules.length - 1; i >= 0; --i) {
            var rule = styles.matchedCSSRules[i];
            if ((rule.isUser || rule.isUserAgent) && !addedAttributesStyle) {
                // Show element's Style Attributes after all author rules.
                addedAttributesStyle = true;
                addAttributesStyle();
            }
            styleRules.push({ style: rule.style, selectorText: rule.selectorText, media: rule.media, rule: rule, editable: !!(rule.style && rule.style.styleSheetId) });
        }

        if (!addedAttributesStyle)
            addAttributesStyle();

        // Walk the node structure and identify styles with inherited properties.
        var parentNode = node.parentNode;
        function insertInheritedNodeSeparator(node)
        {
            var entry = {};
            entry.isStyleSeparator = true;
            entry.node = node;
            styleRules.push(entry);
        }

        for (var parentOrdinal = 0; parentOrdinal < styles.inherited.length; ++parentOrdinal) {
            var parentStyles = styles.inherited[parentOrdinal];
            var separatorInserted = false;
            if (parentStyles.inlineStyle) {
                if (this._containsInherited(parentStyles.inlineStyle)) {
                    var inlineStyle = { selectorText: WebInspector.UIString("Style Attribute"), style: parentStyles.inlineStyle, isAttribute: true, isInherited: true, parentNode: parentNode };
                    if (!separatorInserted) {
                        insertInheritedNodeSeparator(parentNode);
                        separatorInserted = true;
                    }
                    styleRules.push(inlineStyle);
                }
            }

            for (var i = parentStyles.matchedCSSRules.length - 1; i >= 0; --i) {
                var rulePayload = parentStyles.matchedCSSRules[i];
                if (!this._containsInherited(rulePayload.style))
                    continue;
                var rule = rulePayload;

                if (!separatorInserted) {
                    insertInheritedNodeSeparator(parentNode);
                    separatorInserted = true;
                }
                styleRules.push({ style: rule.style, selectorText: rule.selectorText, media: rule.media, rule: rule, isInherited: true, parentNode: parentNode, editable: !!(rule.style && rule.style.styleSheetId) });
            }
            parentNode = parentNode.parentNode;
        }
        return styleRules;
    },

    _markUsedProperties: function(styleRules, usedProperties)
    {
        var foundImportantProperties = {};
        var propertyToEffectiveRule = {};
        var inheritedPropertyToNode = {};
        for (var i = 0; i < styleRules.length; ++i) {
            var styleRule = styleRules[i];
            if (styleRule.computedStyle || styleRule.isStyleSeparator)
                continue;
            if (styleRule.section && styleRule.section.noAffect)
                continue;

            styleRule.usedProperties = {};

            var style = styleRule.style;
            var allProperties = style.allProperties;
            for (var j = 0; j < allProperties.length; ++j) {
                var property = allProperties[j];
                if (!property.isLive || !property.parsedOk)
                    continue;

                // Do not pick non-inherited properties from inherited styles.
                if (styleRule.isInherited && !WebInspector.CSSMetadata.isPropertyInherited(property.name))
                    continue;

                var canonicalName = WebInspector.CSSMetadata.canonicalPropertyName(property.name);
                if (foundImportantProperties.hasOwnProperty(canonicalName))
                    continue;

                if (!property.important && usedProperties.hasOwnProperty(canonicalName))
                    continue;

                var isKnownProperty = propertyToEffectiveRule.hasOwnProperty(canonicalName);
                if (!isKnownProperty && styleRule.isInherited && !inheritedPropertyToNode[canonicalName])
                    inheritedPropertyToNode[canonicalName] = styleRule.parentNode;

                if (property.important) {
                    if (styleRule.isInherited && isKnownProperty && styleRule.parentNode !== inheritedPropertyToNode[canonicalName])
                        continue;

                    foundImportantProperties[canonicalName] = true;
                    if (isKnownProperty)
                        delete propertyToEffectiveRule[canonicalName].usedProperties[canonicalName];
                }

                styleRule.usedProperties[canonicalName] = true;
                usedProperties[canonicalName] = true;
                propertyToEffectiveRule[canonicalName] = styleRule;
            }
        }
    },

    _refreshSectionsForStyleRules: function(styleRules, usedProperties, editedSection)
    {
        // Walk the style rules and update the sections with new overloaded and used properties.
        for (var i = 0; i < styleRules.length; ++i) {
            var styleRule = styleRules[i];
            var section = styleRule.section;
            if (styleRule.computedStyle) {
                section._usedProperties = usedProperties;
                section.update();
            } else {
                section._usedProperties = styleRule.usedProperties;
                section.update(section === editedSection);
            }
        }
    },

    /**
     * @param {!Array.<!Object>} styleRules
     * @param {!Object.<string, boolean>} usedProperties
     * @param {?Element} anchorElement
     */
    _rebuildSectionsForStyleRules: function(styleRules, usedProperties, anchorElement)
    {
        // Make a property section for each style rule.
        var sections = [];
        for (var i = 0; i < styleRules.length; ++i) {
            var styleRule = styleRules[i];
            if (styleRule.isStyleSeparator) {
                var separatorElement = document.createElement("div");
                if (styleRule.isPlaceholder) {
                    separatorElement.className = "styles-sidebar-placeholder";
                    this._sectionsContainer.insertBefore(separatorElement, anchorElement);
                    continue;
                }
                separatorElement.className = "sidebar-separator";
                if (styleRule.node) {
                    var link = WebInspector.DOMPresentationUtils.linkifyNodeReference(styleRule.node);
                    separatorElement.appendChild(document.createTextNode(WebInspector.UIString("Inherited from") + " "));
                    separatorElement.appendChild(link);
                    if (!sections.inheritedPropertiesSeparatorElement)
                        sections.inheritedPropertiesSeparatorElement = separatorElement;
                } else if ("pseudoId" in styleRule) {
                    var pseudoName = WebInspector.StylesSidebarPane.PseudoIdNames[styleRule.pseudoId];
                    if (pseudoName)
                        separatorElement.textContent = WebInspector.UIString("Pseudo ::%s element", pseudoName);
                    else
                        separatorElement.textContent = WebInspector.UIString("Pseudo element");
                } else
                    separatorElement.textContent = styleRule.text;
                this._sectionsContainer.insertBefore(separatorElement, anchorElement);
                continue;
            }
            var computedStyle = styleRule.computedStyle;

            // Default editable to true if it was omitted.
            var editable = styleRule.editable;
            if (typeof editable === "undefined")
                editable = true;

            if (computedStyle)
                var section = new WebInspector.ComputedStylePropertiesSection(this, styleRule, usedProperties);
            else {
                var section = new WebInspector.StylePropertiesSection(this, styleRule, editable, styleRule.isInherited);
                section._markSelectorMatches();
            }
            section.expanded = true;

            if (computedStyle)
                this._computedStylePane.bodyElement.appendChild(section.element);
            else
                this._sectionsContainer.insertBefore(section.element, anchorElement);
            sections.push(section);
        }
        return sections;
    },

    _containsInherited: function(style)
    {
        var properties = style.allProperties;
        for (var i = 0; i < properties.length; ++i) {
            var property = properties[i];
            // Does this style contain non-overridden inherited property?
            if (property.isLive && WebInspector.CSSMetadata.isPropertyInherited(property.name))
                return true;
        }
        return false;
    },

    _colorFormatSettingChanged: function(event)
    {
        for (var pseudoId in this.sections) {
            var sections = this.sections[pseudoId];
            for (var i = 0; i < sections.length; ++i)
                sections[i].update(true);
        }
    },

    /**
     * @param {?Event} event
     */
    _createNewRuleInViaInspectorStyleSheet: function(event)
    {
        event.consume();
        var cssModel = this._target.cssModel;
        cssModel.requestViaInspectorStylesheet(this._node, viaInspectorCallback.bind(this));

        /**
         * @param {?WebInspector.CSSStyleSheetHeader} styleSheetHeader
         * @this {WebInspector.StylesSidebarPane}
         */
        function viaInspectorCallback(styleSheetHeader)
        {
            if (!styleSheetHeader)
                return;
            styleSheetHeader.requestContent(onViaInspectorContent.bind(this, styleSheetHeader.id));
        }

        /**
         * @param {string} styleSheetId
         * @param {string} text
         * @this {WebInspector.StylesSidebarPane}
         */
        function onViaInspectorContent(styleSheetId, text)
        {
            var lines = text.split("\n");
            var range = WebInspector.TextRange.createFromLocation(lines.length - 1, lines[lines.length - 1].length);
            this._addBlankSection(this.sections[0][1], styleSheetId, range);
        }
    },

    /**
     * @param {!WebInspector.StylePropertiesSection} insertAfterSection
     * @param {string} styleSheetId
     * @param {!WebInspector.TextRange} ruleLocation
     */
    _addBlankSection: function(insertAfterSection, styleSheetId, ruleLocation)
    {
        this.expand();
        var blankSection = new WebInspector.BlankStylePropertiesSection(this, this._node ? WebInspector.DOMPresentationUtils.simpleSelector(this._node) : "", styleSheetId, ruleLocation, insertAfterSection.rule);

        this._sectionsContainer.insertBefore(blankSection.element, insertAfterSection.element.nextSibling);

        var index = this.sections[0].indexOf(insertAfterSection);
        this.sections[0].splice(index + 1, 0, blankSection);
        blankSection.startEditingSelector();
    },

    removeSection: function(section)
    {
        for (var pseudoId in this.sections) {
            var sections = this.sections[pseudoId];
            var index = sections.indexOf(section);
            if (index === -1)
                continue;
            sections.splice(index, 1);
            section.element.remove();
        }
    },

    _toggleElementStatePane: function(event)
    {
        event.consume();

        var buttonToggled = !this._elementStateButton.classList.contains("toggled");
        if (buttonToggled)
            this.expand();
        this._elementStateButton.classList.toggle("toggled", buttonToggled);
        this._elementStatePane.classList.toggle("expanded", buttonToggled);
    },

    _createElementStatePane: function()
    {
        this._elementStatePane = document.createElement("div");
        this._elementStatePane.className = "styles-element-state-pane source-code";
        var table = document.createElement("table");

        var inputs = [];
        this._elementStatePane.inputs = inputs;

        /**
         * @param {!Event} event
         * @this {WebInspector.StylesSidebarPane}
         */
        function clickListener(event)
        {
            var node = this._validateNode();
            if (!node)
                return;
            this._setPseudoClassCallback(node, event.target.state, event.target.checked);
        }

        /**
         * @param {string} state
         * @return {!Element}
         * @this {WebInspector.StylesSidebarPane}
         */
        function createCheckbox(state)
        {
            var td = document.createElement("td");
            var label = document.createElement("label");
            var input = document.createElement("input");
            input.type = "checkbox";
            input.state = state;
            input.addEventListener("click", clickListener.bind(this), false);
            inputs.push(input);
            label.appendChild(input);
            label.appendChild(document.createTextNode(":" + state));
            td.appendChild(label);
            return td;
        }

        var tr = table.createChild("tr");
        tr.appendChild(createCheckbox.call(this, "active"));
        tr.appendChild(createCheckbox.call(this, "hover"));

        tr = table.createChild("tr");
        tr.appendChild(createCheckbox.call(this, "focus"));
        tr.appendChild(createCheckbox.call(this, "visited"));

        this._elementStatePane.appendChild(table);
    },

    /**
     * @return {?RegExp}
     */
    filterRegex: function()
    {
        return this._filterRegex;
    },

    /**
     * @param {boolean} isComputedStyleFilter
     * @return {!Element}
     * @param {function(?RegExp)} filterCallback
     */
    _createPropertyFilterElement: function(isComputedStyleFilter, filterCallback)
    {
        var input = document.createElement("input");
        input.type = "text";
        input.placeholder = isComputedStyleFilter ? WebInspector.UIString("Filter") : WebInspector.UIString("Find in Styles");
        var boundSearchHandler = searchHandler.bind(this);

        /**
         * @this {WebInspector.StylesSidebarPane}
         */
        function searchHandler()
        {
            var regex = input.value ? new RegExp(input.value.escapeForRegExp(), "i") : null;
            filterCallback(regex);
            input.parentNode.classList.toggle("styles-filter-engaged", !!input.value);
            this._updateFilter(isComputedStyleFilter);
        }
        input.addEventListener("input", boundSearchHandler, false);

        /**
         * @param {!Event} event
         */
        function keydownHandler(event)
        {
            var Esc = "U+001B";
            if (event.keyIdentifier !== Esc || !input.value)
                return;
            event.consume(true);
            input.value = "";
            boundSearchHandler();
        }
        input.addEventListener("keydown", keydownHandler, false);

        return input;
    },

    /**
     * @param {boolean} isComputedStyleFilter
     */
    _updateFilter: function(isComputedStyleFilter)
    {
        for (var pseudoId in this.sections) {
            var sections = this.sections[pseudoId];
            for (var i = 0; i < sections.length; ++i) {
                var section = sections[i];
                if (isComputedStyleFilter !== !!section.computedStyle)
                    continue;
                section._updateFilter();
            }
        }
    },

    /**
     * @param {!WebInspector.Event} event
     */
    _showUserAgentStylesSettingChanged: function(event)
    {
        var showStyles = /** @type {boolean} */ (event.data);
        this.element.classList.toggle("show-user-styles", showStyles);
    },

    willHide: function()
    {
        this._spectrumHelper.hide();
        this._discardElementUnderMouse();
    },

    _discardElementUnderMouse: function()
    {
        if (this._elementUnderMouse)
            this._elementUnderMouse.classList.remove("styles-panel-hovered");
        delete this._elementUnderMouse;
    },

    _mouseMovedOverElement: function(e)
    {
        if (this._elementUnderMouse && e.target !== this._elementUnderMouse)
            this._discardElementUnderMouse();
        this._elementUnderMouse = e.target;
        if (WebInspector.KeyboardShortcut.eventHasCtrlOrMeta(e))
            this._elementUnderMouse.classList.add("styles-panel-hovered");
    },

    _keyDown: function(e)
    {
        if ((!WebInspector.isMac() && e.keyCode === WebInspector.KeyboardShortcut.Keys.Ctrl.code) ||
            (WebInspector.isMac() && e.keyCode === WebInspector.KeyboardShortcut.Keys.Meta.code)) {
            if (this._elementUnderMouse)
                this._elementUnderMouse.classList.add("styles-panel-hovered");
        }
    },

    _keyUp: function(e)
    {
        if ((!WebInspector.isMac() && e.keyCode === WebInspector.KeyboardShortcut.Keys.Ctrl.code) ||
            (WebInspector.isMac() && e.keyCode === WebInspector.KeyboardShortcut.Keys.Meta.code)) {
            this._discardElementUnderMouse();
        }
    },

    __proto__: WebInspector.SidebarPane.prototype
}

/**
 * @constructor
 * @extends {WebInspector.SidebarPane}
 */
WebInspector.ComputedStyleSidebarPane = function()
{
    WebInspector.SidebarPane.call(this, WebInspector.UIString("Computed Style"));
}

WebInspector.ComputedStyleSidebarPane.prototype = {
    /**
     * @param {!WebInspector.StylesSidebarPane} pane
     */
    setHostingPane: function(pane)
    {
        this._stylesSidebarPane = pane;
    },

    setFilterBoxContainer: function(element)
    {
        element.appendChild(this._stylesSidebarPane._createPropertyFilterElement(true, filterCallback.bind(this)));

        /**
         * @param {?RegExp} regex
         * @this {WebInspector.ComputedStyleSidebarPane}
         */
        function filterCallback(regex)
        {
            this._filterRegex = regex;
        }
    },

    wasShown: function()
    {
        WebInspector.SidebarPane.prototype.wasShown.call(this);
        if (!this._hasFreshContent)
            this.prepareContent();
    },

    /**
     * @param {function()=} callback
     */
    prepareContent: function(callback)
    {
        /**
         * @this {WebInspector.ComputedStyleSidebarPane}
         */
        function wrappedCallback() {
            this._hasFreshContent = true;
            if (callback)
                callback();
            delete this._hasFreshContent;
        }
        this._stylesSidebarPane._refreshUpdate(null, true, wrappedCallback.bind(this));
    },

    /**
     * @return {?RegExp}
     */
    filterRegex: function()
    {
        return this._filterRegex;
    },

    __proto__: WebInspector.SidebarPane.prototype
}

/**
 * @constructor
 * @extends {WebInspector.PropertiesSection}
 * @param {!WebInspector.StylesSidebarPane} parentPane
 * @param {!Object} styleRule
 * @param {boolean} editable
 * @param {boolean} isInherited
 */
WebInspector.StylePropertiesSection = function(parentPane, styleRule, editable, isInherited)
{
    WebInspector.PropertiesSection.call(this, "");

    this._parentPane = parentPane;
    this.styleRule = styleRule;
    this.rule = this.styleRule.rule;
    this.editable = editable;
    this.isInherited = isInherited;

    var extraClasses = (this.rule && (this.rule.isUser || this.rule.isUserAgent) ? " user-rule" : "");
    this.element.className = "styles-section matched-styles monospace" + extraClasses;
    // We don't really use properties' disclosure.
    this.propertiesElement.classList.remove("properties-tree");

    var selectorContainer = document.createElement("div");
    this._selectorElement = document.createElement("span");
    this._selectorElement.textContent = styleRule.selectorText;
    selectorContainer.appendChild(this._selectorElement);

    var openBrace = document.createElement("span");
    openBrace.textContent = " {";
    selectorContainer.appendChild(openBrace);
    selectorContainer.addEventListener("mousedown", this._handleEmptySpaceMouseDown.bind(this), false);
    selectorContainer.addEventListener("click", this._handleSelectorContainerClick.bind(this), false);

    var closeBrace = document.createElement("div");
    closeBrace.textContent = "}";
    this.element.appendChild(closeBrace);

    if (this.editable && this.rule) {
        var newRuleButton = closeBrace.createChild("div", "sidebar-pane-button-new-rule");
        newRuleButton.title = WebInspector.UIString("Insert Style Rule");
        newRuleButton.addEventListener("click", this._onNewRuleClick.bind(this), false);
    }

    this._selectorElement.addEventListener("click", this._handleSelectorClick.bind(this), false);
    this.element.addEventListener("mousedown", this._handleEmptySpaceMouseDown.bind(this), false);
    this.element.addEventListener("click", this._handleEmptySpaceClick.bind(this), false);

    if (this.rule) {
        // Prevent editing the user agent and user rules.
        if (this.rule.isUserAgent || this.rule.isUser)
            this.editable = false;
        else {
            // Check this is a real CSSRule, not a bogus object coming from WebInspector.BlankStylePropertiesSection.
            if (this.rule.styleSheetId)
                this.navigable = !!this.rule.resourceURL();
        }
        this.titleElement.classList.add("styles-selector");
    }

    this._usedProperties = styleRule.usedProperties;

    this._selectorRefElement = document.createElement("div");
    this._selectorRefElement.className = "subtitle";
    this._mediaListElement = this.titleElement.createChild("div", "media-list");
    this._updateMediaList();
    this._updateRuleOrigin();
    selectorContainer.insertBefore(this._selectorRefElement, selectorContainer.firstChild);
    this.titleElement.appendChild(selectorContainer);
    this._selectorContainer = selectorContainer;

    if (isInherited)
        this.element.classList.add("styles-show-inherited"); // This one is related to inherited rules, not computed style.

    if (this.navigable)
        this.element.classList.add("navigable");

    if (!this.editable)
        this.element.classList.add("read-only");
}

WebInspector.StylePropertiesSection.prototype = {
    /**
     * @param {?Event} event
     */
    _onNewRuleClick: function(event)
    {
        event.consume();
        var range = WebInspector.TextRange.createFromLocation(this.rule.style.range.endLine, this.rule.style.range.endColumn + 1);
        this._parentPane._addBlankSection(this, this.rule.styleSheetId, range);
    },

    /**
     * @param {!WebInspector.CSSRule} editedRule
     * @param {!WebInspector.TextRange} oldRange
     * @param {!WebInspector.TextRange} newRange
     */
    _styleSheetRuleEdited: function(editedRule, oldRange, newRange)
    {
        if (!this.rule || !this.rule.styleSheetId)
            return;
        if (this.rule !== editedRule)
            this.rule.sourceStyleSheetEdited(editedRule.styleSheetId, oldRange, newRange);
        this._updateMediaList();
        this._updateRuleOrigin();
    },

    /**
     * @param {!Object} styleRule
     */
    _createMediaList: function(styleRule)
    {
        if (!styleRule.media)
            return;
        for (var i = styleRule.media.length - 1; i >= 0; --i) {
            var media = styleRule.media[i];
            var mediaDataElement = this._mediaListElement.createChild("div", "media");
            var mediaText;
            switch (media.source) {
            case WebInspector.CSSMedia.Source.LINKED_SHEET:
            case WebInspector.CSSMedia.Source.INLINE_SHEET:
                mediaText = "media=\"" + media.text + "\"";
                break;
            case WebInspector.CSSMedia.Source.MEDIA_RULE:
                mediaText = "@media " + media.text;
                break;
            case WebInspector.CSSMedia.Source.IMPORT_RULE:
                mediaText = "@import " + media.text;
                break;
            }

            if (media.sourceURL) {
                var refElement = mediaDataElement.createChild("div", "subtitle");
                var anchor = this._parentPane._linkifier.linkifyMedia(media);
                anchor.style.float = "right";
                refElement.appendChild(anchor);
            }

            var mediaTextElement = mediaDataElement.createChild("span");
            mediaTextElement.textContent = mediaText;
            mediaTextElement.title = media.text;
        }
    },

    _updateMediaList: function()
    {
        this._mediaListElement.removeChildren();
        this._createMediaList(this.styleRule);
    },

    collapse: function()
    {
        // Overriding with empty body.
    },

    handleClick: function()
    {
        // Avoid consuming events.
    },

    /**
     * @param {string} propertyName
     * @return {boolean}
     */
    isPropertyInherited: function(propertyName)
    {
        if (this.isInherited) {
            // While rendering inherited stylesheet, reverse meaning of this property.
            // Render truly inherited properties with black, i.e. return them as non-inherited.
            return !WebInspector.CSSMetadata.isPropertyInherited(propertyName);
        }
        return false;
    },

    /**
     * @param {string} propertyName
     * @param {boolean=} isShorthand
     * @return {boolean}
     */
    isPropertyOverloaded: function(propertyName, isShorthand)
    {
        if (!this._usedProperties || this.noAffect)
            return false;

        if (this.isInherited && !WebInspector.CSSMetadata.isPropertyInherited(propertyName)) {
            // In the inherited sections, only show overrides for the potentially inherited properties.
            return false;
        }

        var canonicalName = WebInspector.CSSMetadata.canonicalPropertyName(propertyName);
        var used = (canonicalName in this._usedProperties);
        if (used || !isShorthand)
            return !used;

        // Find out if any of the individual longhand properties of the shorthand
        // are used, if none are then the shorthand is overloaded too.
        var longhandProperties = this.styleRule.style.longhandProperties(propertyName);
        for (var j = 0; j < longhandProperties.length; ++j) {
            var individualProperty = longhandProperties[j];
            if (WebInspector.CSSMetadata.canonicalPropertyName(individualProperty.name) in this._usedProperties)
                return false;
        }

        return true;
    },

    /**
     * @return {?WebInspector.StylePropertiesSection}
     */
    nextEditableSibling: function()
    {
        var curSection = this;
        do {
            curSection = curSection.nextSibling;
        } while (curSection && !curSection.editable);

        if (!curSection) {
            curSection = this.firstSibling;
            while (curSection && !curSection.editable)
                curSection = curSection.nextSibling;
        }

        return (curSection && curSection.editable) ? curSection : null;
    },

    /**
     * @return {?WebInspector.StylePropertiesSection}
     */
    previousEditableSibling: function()
    {
        var curSection = this;
        do {
            curSection = curSection.previousSibling;
        } while (curSection && !curSection.editable);

        if (!curSection) {
            curSection = this.lastSibling;
            while (curSection && !curSection.editable)
                curSection = curSection.previousSibling;
        }

        return (curSection && curSection.editable) ? curSection : null;
    },

    update: function(full)
    {
        if (this.styleRule.selectorText)
            this._selectorElement.textContent = this.styleRule.selectorText;
        this._markSelectorMatches();
        if (full) {
            this.propertiesTreeOutline.removeChildren();
            this.populated = false;
        } else {
            var child = this.propertiesTreeOutline.children[0];
            while (child) {
                child.overloaded = this.isPropertyOverloaded(child.name, child.isShorthand);
                child = child.traverseNextTreeElement(false, null, true);
            }
        }
        this.afterUpdate();
    },

    afterUpdate: function()
    {
        if (this._afterUpdate) {
            this._afterUpdate(this);
            delete this._afterUpdate;
        }
    },

    onpopulate: function()
    {
        var style = this.styleRule.style;
        var allProperties = style.allProperties;
        this.uniqueProperties = [];

        var styleHasEditableSource = this.editable && !!style.range;
        if (styleHasEditableSource) {
            for (var i = 0; i < allProperties.length; ++i) {
                var property = allProperties[i];
                this.uniqueProperties.push(property);
                if (property.styleBased)
                    continue;

                var isShorthand = !!WebInspector.CSSMetadata.cssPropertiesMetainfo.longhands(property.name);
                var inherited = this.isPropertyInherited(property.name);
                var overloaded = property.inactive || this.isPropertyOverloaded(property.name);
                var item = new WebInspector.StylePropertyTreeElement(this._parentPane, this.styleRule, style, property, isShorthand, inherited, overloaded);
                this.propertiesTreeOutline.appendChild(item);
            }
            return;
        }

        var generatedShorthands = {};
        // For style-based properties, generate shorthands with values when possible.
        for (var i = 0; i < allProperties.length; ++i) {
            var property = allProperties[i];
            this.uniqueProperties.push(property);
            var isShorthand = !!WebInspector.CSSMetadata.cssPropertiesMetainfo.longhands(property.name);

            // For style-based properties, try generating shorthands.
            var shorthands = isShorthand ? null : WebInspector.CSSMetadata.cssPropertiesMetainfo.shorthands(property.name);
            var shorthandPropertyAvailable = false;
            for (var j = 0; shorthands && !shorthandPropertyAvailable && j < shorthands.length; ++j) {
                var shorthand = shorthands[j];
                if (shorthand in generatedShorthands) {
                    shorthandPropertyAvailable = true;
                    continue;  // There already is a shorthand this longhands falls under.
                }
                if (style.getLiveProperty(shorthand)) {
                    shorthandPropertyAvailable = true;
                    continue;  // There is an explict shorthand property this longhands falls under.
                }
                if (!style.shorthandValue(shorthand)) {
                    shorthandPropertyAvailable = false;
                    continue;  // Never generate synthetic shorthands when no value is available.
                }

                // Generate synthetic shorthand we have a value for.
                var shorthandProperty = new WebInspector.CSSProperty(style, style.allProperties.length, shorthand, style.shorthandValue(shorthand), false, false, true, true);
                var overloaded = property.inactive || this.isPropertyOverloaded(property.name, true);
                var item = new WebInspector.StylePropertyTreeElement(this._parentPane, this.styleRule, style, shorthandProperty,  /* isShorthand */ true, /* inherited */ false, overloaded);
                this.propertiesTreeOutline.appendChild(item);
                generatedShorthands[shorthand] = shorthandProperty;
                shorthandPropertyAvailable = true;
            }
            if (shorthandPropertyAvailable)
                continue;  // Shorthand for the property found.

            var inherited = this.isPropertyInherited(property.name);
            var overloaded = property.inactive || this.isPropertyOverloaded(property.name, isShorthand);
            var item = new WebInspector.StylePropertyTreeElement(this._parentPane, this.styleRule, style, property, isShorthand, inherited, overloaded);
            this.propertiesTreeOutline.appendChild(item);
        }
    },

    _updateFilter: function()
    {
        if (this.styleRule.isAttribute)
            return;
        var regex = this._parentPane.filterRegex();
        var hideRule = regex && !regex.test(this.element.textContent);
        this.element.classList.toggle("hidden", hideRule);
        if (hideRule)
            return;

        var children = this.propertiesTreeOutline.children;
        for (var i = 0; i < children.length; ++i)
            children[i]._updateFilter();

        if (this.styleRule.rule)
            this._markSelectorHighlights();
    },

    _markSelectorMatches: function()
    {
        var rule = this.styleRule.rule;
        if (!rule)
            return;

        var matchingSelectors = rule.matchingSelectors;
        // .selector is rendered as non-affecting selector by default.
        if (this.noAffect || matchingSelectors)
            this._selectorElement.className = "selector";
        if (!matchingSelectors)
            return;

        var selectors = rule.selectors;
        var fragment = document.createDocumentFragment();
        var currentMatch = 0;
        for (var i = 0; i < selectors.length ; ++i) {
            if (i)
                fragment.appendChild(document.createTextNode(", "));
            var isSelectorMatching = matchingSelectors[currentMatch] === i;
            if (isSelectorMatching)
                ++currentMatch;
            var matchingSelectorClass = isSelectorMatching ? " selector-matches" : "";
            var selectorElement = document.createElement("span");
            selectorElement.className = "simple-selector" + matchingSelectorClass;
            if (rule.styleSheetId)
                selectorElement._selectorIndex = i;
            selectorElement.textContent = selectors[i].value;

            fragment.appendChild(selectorElement);
        }

        this._selectorElement.removeChildren();
        this._selectorElement.appendChild(fragment);
        this._markSelectorHighlights();
    },

    _markSelectorHighlights: function()
    {
        var selectors = this._selectorElement.getElementsByClassName("simple-selector");
        var regex = this._parentPane.filterRegex();
        for (var i = 0; i < selectors.length; ++i) {
            var selectorMatchesFilter = regex && regex.test(selectors[i].textContent);
            selectors[i].classList.toggle("filter-match", selectorMatchesFilter);
        }
    },

    _checkWillCancelEditing: function()
    {
        var willCauseCancelEditing = this._willCauseCancelEditing;
        delete this._willCauseCancelEditing;
        return willCauseCancelEditing;
    },

    _handleSelectorContainerClick: function(event)
    {
        if (this._checkWillCancelEditing() || !this.editable)
            return;
        if (event.target === this._selectorContainer) {
            this.addNewBlankProperty(0).startEditing();
            event.consume(true);
        }
    },

    /**
     * @param {number=} index
     * @return {!WebInspector.StylePropertyTreeElement}
     */
    addNewBlankProperty: function(index)
    {
        var style = this.styleRule.style;
        var property = style.newBlankProperty(index);
        var item = new WebInspector.StylePropertyTreeElement(this._parentPane, this.styleRule, style, property, false, false, false);
        index = property.index;
        this.propertiesTreeOutline.insertChild(item, index);
        item.listItemElement.textContent = "";
        item._newProperty = true;
        item.updateTitle();
        return item;
    },

    /**
     * @param {?WebInspector.CSSRule} rule
     * @param {!WebInspector.TextRange=} ruleLocation
     * @return {!Node}
     */
    _createRuleOriginNode: function(rule, ruleLocation)
    {
        /**
         * @param {string} url
         * @param {number} line
         */
        function linkifyUncopyable(url, line)
        {
            var link = WebInspector.linkifyResourceAsNode(url, line, "", url + ":" + (line + 1));
            link.classList.add("webkit-html-resource-link");
            link.setAttribute("data-uncopyable", link.textContent);
            link.textContent = "";
            return link;
        }

        if (!rule)
            return document.createTextNode("");

        if (!ruleLocation) {
            var firstMatchingIndex = rule.matchingSelectors && rule.matchingSelectors.length ? rule.matchingSelectors[0] : 0;
            ruleLocation = rule.selectors[firstMatchingIndex].range;
        }

        var sourceURL = rule.resourceURL();
        if (sourceURL && ruleLocation && rule.styleSheetId) {
            var styleSheetHeader = this._parentPane._target.cssModel.styleSheetHeaderForId(rule.styleSheetId);
            var lineNumber = styleSheetHeader.lineNumberInSource(ruleLocation.startLine);
            var columnNumber = styleSheetHeader.columnNumberInSource(ruleLocation.startLine, ruleLocation.startColumn);
            var matchingSelectorLocation = new WebInspector.CSSLocation(this._parentPane._target, rule.styleSheetId, sourceURL, lineNumber, columnNumber);
            return this._parentPane._linkifier.linkifyCSSLocation(matchingSelectorLocation) || linkifyUncopyable(sourceURL, 0);
        }

        if (rule.isUserAgent)
            return document.createTextNode(WebInspector.UIString("user agent stylesheet"));
        if (rule.isUser)
            return document.createTextNode(WebInspector.UIString("user stylesheet"));
        if (rule.isViaInspector)
            return this._createRuleViaInspectorOriginNode();
        return document.createTextNode("");
    },

    /**
     * @return {!Node}
     */
    _createRuleViaInspectorOriginNode: function()
    {
        return document.createTextNode(WebInspector.UIString("via inspector"));
    },

    _handleEmptySpaceMouseDown: function()
    {
        this._willCauseCancelEditing = this._parentPane._isEditingStyle;
    },

    _handleEmptySpaceClick: function(event)
    {
        if (!this.editable)
            return;

        if (!window.getSelection().isCollapsed)
            return;

        if (this._checkWillCancelEditing())
            return;

        if (event.target.classList.contains("header") || this.element.classList.contains("read-only") || event.target.enclosingNodeOrSelfWithClass("media")) {
            event.consume();
            return;
        }
        this.expand();
        this.addNewBlankProperty().startEditing();
        event.consume(true)
    },

    _handleSelectorClick: function(event)
    {
        if (WebInspector.KeyboardShortcut.eventHasCtrlOrMeta(event) && this.navigable && event.target.classList.contains("simple-selector")) {
            var index = event.target._selectorIndex;
            var target = this._parentPane._target;
            var rawLocation = new WebInspector.CSSLocation(target, this.rule.styleSheetId, this.rule.sourceURL, this.rule.lineNumberInSource(index), this.rule.columnNumberInSource(index));
            var uiLocation = WebInspector.cssWorkspaceBinding.rawLocationToUILocation(rawLocation);
            WebInspector.Revealer.reveal(uiLocation);
            event.consume(true);
            return;
        }
        this._startEditingOnMouseEvent();
        event.consume(true);
    },

    _startEditingOnMouseEvent: function()
    {
        if (!this.editable)
            return;

        if (!this.rule && this.propertiesTreeOutline.children.length === 0) {
            this.expand();
            this.addNewBlankProperty().startEditing();
            return;
        }

        if (!this.rule)
            return;

        this.startEditingSelector();
    },

    startEditingSelector: function()
    {
        var element = this._selectorElement;
        if (WebInspector.isBeingEdited(element))
            return;

        element.scrollIntoViewIfNeeded(false);
        element.textContent = element.textContent; // Reset selector marks in group.

        var config = new WebInspector.InplaceEditor.Config(this.editingSelectorCommitted.bind(this), this.editingSelectorCancelled.bind(this));
        WebInspector.InplaceEditor.startEditing(this._selectorElement, config);

        window.getSelection().setBaseAndExtent(element, 0, element, 1);
        this._parentPane._isEditingStyle = true;
    },

    _moveEditorFromSelector: function(moveDirection)
    {
        this._markSelectorMatches();

        if (!moveDirection)
            return;

        if (moveDirection === "forward") {
            this.expand();
            var firstChild = this.propertiesTreeOutline.children[0];
            while (firstChild && firstChild.inherited)
                firstChild = firstChild.nextSibling;
            if (!firstChild)
                this.addNewBlankProperty().startEditing();
            else
                firstChild.startEditing(firstChild.nameElement);
        } else {
            var previousSection = this.previousEditableSibling();
            if (!previousSection)
                return;

            previousSection.expand();
            previousSection.addNewBlankProperty().startEditing();
        }
    },

    editingSelectorCommitted: function(element, newContent, oldContent, context, moveDirection)
    {
        this._editingSelectorEnded();
        if (newContent)
            newContent = newContent.trim();
        if (newContent === oldContent) {
            // Revert to a trimmed version of the selector if need be.
            this._selectorElement.textContent = newContent;
            this._moveEditorFromSelector(moveDirection);
            return;
        }

        var selectedNode = this._parentPane._node;

        /**
         * @param {!WebInspector.CSSRule} newRule
         * @this {WebInspector.StylePropertiesSection}
         */
        function successCallback(newRule)
        {
            var doesAffectSelectedNode = newRule.matchingSelectors.length > 0;
            if (!doesAffectSelectedNode) {
                this.noAffect = true;
                this.element.classList.add("no-affect");
            } else {
                delete this.noAffect;
                this.element.classList.remove("no-affect");
            }

            var oldSelectorRange = this.rule.selectorRange;
            this.rule = newRule;
            this.styleRule = { section: this, style: newRule.style, selectorText: newRule.selectorText, media: newRule.media, rule: newRule };

            this._parentPane.update(selectedNode);
            this._parentPane._styleSheetRuleEdited(this.rule, oldSelectorRange, this.rule.selectorRange);

            finishOperationAndMoveEditor.call(this, moveDirection);
        }

        /**
         * @this {WebInspector.StylePropertiesSection}
         */
        function finishOperationAndMoveEditor(direction)
        {
            delete this._parentPane._userOperation;
            this._moveEditorFromSelector(direction);
        }

        // This gets deleted in finishOperationAndMoveEditor(), which is called both on success and failure.
        this._parentPane._userOperation = true;
        this._parentPane._target.cssModel.setRuleSelector(this.rule, selectedNode ? selectedNode.id : 0, newContent, successCallback.bind(this), finishOperationAndMoveEditor.bind(this, moveDirection));
    },

    _updateRuleOrigin: function()
    {
        this._selectorRefElement.removeChildren();
        this._selectorRefElement.appendChild(this._createRuleOriginNode(this.rule));
    },

    _editingSelectorEnded: function()
    {
        delete this._parentPane._isEditingStyle;
    },

    editingSelectorCancelled: function()
    {
        this._editingSelectorEnded();

        // Mark the selectors in group if necessary.
        // This is overridden by BlankStylePropertiesSection.
        this._markSelectorMatches();
    },

    __proto__: WebInspector.PropertiesSection.prototype
}

/**
 * @constructor
 * @extends {WebInspector.PropertiesSection}
 * @param {!WebInspector.StylesSidebarPane} stylesPane
 * @param {!Object} styleRule
 * @param {!Object.<string, boolean>} usedProperties
 */
WebInspector.ComputedStylePropertiesSection = function(stylesPane, styleRule, usedProperties)
{
    WebInspector.PropertiesSection.call(this, "");
    this._hasFreshContent = false;
    this.element.className = "styles-section monospace read-only computed-style";

    var showInheritedCheckbox = WebInspector.SettingsUI.createSettingCheckbox(WebInspector.UIString("Show inherited properties"), WebInspector.settings.showInheritedComputedStyleProperties, true);
    showInheritedCheckbox.classList.add("checkbox-with-label");
    this.headerElement.appendChild(showInheritedCheckbox);
    WebInspector.settings.showInheritedComputedStyleProperties.addChangeListener(showInheritedChanged.bind(this));
    showInheritedChanged.call(this);

    /**
     * @this {WebInspector.ComputedStylePropertiesSection}
     */
    function showInheritedChanged()
    {
        this.element.classList.toggle("styles-show-inherited", WebInspector.settings.showInheritedComputedStyleProperties.get());
    }

    this._stylesPane = stylesPane;
    this.styleRule = styleRule;
    this._usedProperties = usedProperties;
    this._alwaysShowComputedProperties = { "display": true, "height": true, "width": true };
    this.computedStyle = true;
    this._propertyTreeElements = {};
    this._expandedPropertyNames = {};
}

WebInspector.ComputedStylePropertiesSection.prototype = {
    collapse: function(dontRememberState)
    {
        // Overriding with empty body.
    },

    _isPropertyInherited: function(propertyName)
    {
        var canonicalName = WebInspector.CSSMetadata.canonicalPropertyName(propertyName);
        return !(canonicalName in this._usedProperties) && !(canonicalName in this._alwaysShowComputedProperties);
    },

    update: function()
    {
        this._expandedPropertyNames = {};
        for (var name in this._propertyTreeElements) {
            if (this._propertyTreeElements[name].expanded)
                this._expandedPropertyNames[name] = true;
        }
        this._propertyTreeElements = {};
        this.propertiesTreeOutline.removeChildren();
        this.populated = false;
    },

    _updateFilter: function()
    {
        var children = this.propertiesTreeOutline.children;
        for (var i = 0; i < children.length; ++i)
            children[i]._updateFilter();
    },

    onpopulate: function()
    {
        function sorter(a, b)
        {
            return a.name.compareTo(b.name);
        }

        var style = this.styleRule.style;
        if (!style)
            return;

        var uniqueProperties = [];
        var allProperties = style.allProperties;
        for (var i = 0; i < allProperties.length; ++i)
            uniqueProperties.push(allProperties[i]);
        uniqueProperties.sort(sorter);

        this._propertyTreeElements = {};
        for (var i = 0; i < uniqueProperties.length; ++i) {
            var property = uniqueProperties[i];
            var inherited = this._isPropertyInherited(property.name);
            var item = new WebInspector.ComputedStylePropertyTreeElement(this._stylesPane, this.styleRule, style, property, inherited);
            this.propertiesTreeOutline.appendChild(item);
            this._propertyTreeElements[property.name] = item;
        }
    },

    rebuildComputedTrace: function(sections)
    {
        for (var i = 0; i < sections.length; ++i) {
            var section = sections[i];
            if (section.computedStyle || section.isBlank)
                continue;

            for (var j = 0; j < section.uniqueProperties.length; ++j) {
                var property = section.uniqueProperties[j];
                if (property.disabled)
                    continue;
                if (section.isInherited && !WebInspector.CSSMetadata.isPropertyInherited(property.name))
                    continue;

                var treeElement = this._propertyTreeElements[property.name.toLowerCase()];
                if (treeElement) {
                    var fragment = document.createDocumentFragment();
                    var selector = fragment.createChild("span");
                    selector.style.color = "gray";
                    selector.textContent = section.styleRule.selectorText;
                    fragment.appendChild(document.createTextNode(" - " + property.value + " "));
                    var subtitle = fragment.createChild("span");
                    subtitle.style.float = "right";
                    subtitle.appendChild(section._createRuleOriginNode(section.rule));
                    var childElement = new TreeElement(fragment, null, false);
                    treeElement.appendChild(childElement);
                    if (property.inactive || section.isPropertyOverloaded(property.name))
                        childElement.listItemElement.classList.add("overloaded");
                    if (!property.parsedOk) {
                        childElement.listItemElement.classList.add("not-parsed-ok");
                        childElement.listItemElement.insertBefore(WebInspector.StylesSidebarPane.createExclamationMark(property), childElement.listItemElement.firstChild);
                        if (WebInspector.StylesSidebarPane._ignoreErrorsForProperty(property))
                            childElement.listItemElement.classList.add("has-ignorable-error");
                    }
                }
            }
        }

        // Restore expanded state after update.
        for (var name in this._expandedPropertyNames) {
            if (name in this._propertyTreeElements)
                this._propertyTreeElements[name].expand();
        }
    },

    __proto__: WebInspector.PropertiesSection.prototype
}

/**
 * @constructor
 * @extends {WebInspector.StylePropertiesSection}
 * @param {!WebInspector.StylesSidebarPane} stylesPane
 * @param {string} defaultSelectorText
 * @param {string} styleSheetId
 * @param {!WebInspector.TextRange} ruleLocation
 * @param {!WebInspector.CSSRule=} insertAfterRule
 */
WebInspector.BlankStylePropertiesSection = function(stylesPane, defaultSelectorText, styleSheetId, ruleLocation, insertAfterRule)
{
    var styleSheetHeader = WebInspector.cssModel.styleSheetHeaderForId(styleSheetId);
    WebInspector.StylePropertiesSection.call(this, stylesPane, { selectorText: defaultSelectorText }, true, false);
    this._ruleLocation = ruleLocation;
    this._styleSheetId = styleSheetId;
    this._selectorRefElement.removeChildren();
    if (insertAfterRule) {
        this._selectorRefElement.appendChild(this._createRuleOriginNode(insertAfterRule, this._actualRuleLocation()));
        this._createMediaList(insertAfterRule);
    } else {
        this._selectorRefElement.appendChild(this._createRuleViaInspectorOriginNode());
    }
    this.element.classList.add("blank-section");
}

WebInspector.BlankStylePropertiesSection.prototype = {
    /**
     * @return {!WebInspector.TextRange}
     */
    _actualRuleLocation: function()
    {
        var prefix = this._rulePrefix();
        var lines = prefix.split("\n");
        var editRange = new WebInspector.TextRange(0, 0, lines.length - 1, lines.peekLast().length);
        return this._ruleLocation.rebaseAfterTextEdit(WebInspector.TextRange.createFromLocation(0, 0), editRange);
    },

    /**
     * @return {string}
     */
    _rulePrefix: function()
    {
        return this._ruleLocation.startLine === 0 && this._ruleLocation.startColumn === 0 ? "" : "\n\n";
    },

    get isBlank()
    {
        return !this._normal;
    },

    expand: function()
    {
        if (!this.isBlank)
            WebInspector.StylePropertiesSection.prototype.expand.call(this);
    },

    editingSelectorCommitted: function(element, newContent, oldContent, context, moveDirection)
    {
        if (!this.isBlank) {
            WebInspector.StylePropertiesSection.prototype.editingSelectorCommitted.call(this, element, newContent, oldContent, context, moveDirection);
            return;
        }

        /**
         * @param {!WebInspector.CSSRule} newRule
         * @this {WebInspector.StylePropertiesSection}
         */
        function successCallback(newRule)
        {
            var doesSelectorAffectSelectedNode = newRule.matchingSelectors.length > 0;
            var styleRule = { media: newRule.media, section: this, style: newRule.style, selectorText: newRule.selectorText, rule: newRule };
            this._makeNormal(styleRule);

            if (!doesSelectorAffectSelectedNode) {
                this.noAffect = true;
                this.element.classList.add("no-affect");
            }

            var ruleTextLines = ruleText.split("\n");
            var startLine = this._ruleLocation.startLine;
            var startColumn = this._ruleLocation.startColumn;
            var newRange = new WebInspector.TextRange(startLine, startColumn, startLine + ruleTextLines.length - 1, startColumn + ruleTextLines[ruleTextLines.length - 1].length);
            this._parentPane._styleSheetRuleEdited(newRule, this._ruleLocation, newRange);

            this._updateRuleOrigin();
            this.expand();
            if (this.element.parentElement) // Might have been detached already.
                this._moveEditorFromSelector(moveDirection);

            delete this._parentPane._userOperation;
            this._editingSelectorEnded();
            this._markSelectorMatches();
        }

        if (newContent)
            newContent = newContent.trim();
        this._parentPane._userOperation = true;

        var cssModel = this._parentPane._target.cssModel;
        var ruleText = this._rulePrefix() + newContent + " {}";
        cssModel.addRule(this._styleSheetId, this._parentPane._node, ruleText, this._ruleLocation, successCallback.bind(this), this.editingSelectorCancelled.bind(this));
    },

    editingSelectorCancelled: function()
    {
        delete this._parentPane._userOperation;
        if (!this.isBlank) {
            WebInspector.StylePropertiesSection.prototype.editingSelectorCancelled.call(this);
            return;
        }

        this._editingSelectorEnded();
        this._parentPane.removeSection(this);
    },

    _makeNormal: function(styleRule)
    {
        this.element.classList.remove("blank-section");
        this.styleRule = styleRule;
        this.rule = styleRule.rule;

        // FIXME: replace this instance by a normal WebInspector.StylePropertiesSection.
        this._normal = true;
    },

    __proto__: WebInspector.StylePropertiesSection.prototype
}

/**
 * @constructor
 * @extends {TreeElement}
 * @param {!Object} styleRule
 * @param {!WebInspector.CSSStyleDeclaration} style
 * @param {!WebInspector.CSSProperty} property
 * @param {boolean} inherited
 * @param {boolean} overloaded
 * @param {boolean} hasChildren
 */
WebInspector.StylePropertyTreeElementBase = function(styleRule, style, property, inherited, overloaded, hasChildren)
{
    this._styleRule = styleRule;
    this.style = style;
    this.property = property;
    this._inherited = inherited;
    this._overloaded = overloaded;

    // Pass an empty title, the title gets made later in onattach.
    TreeElement.call(this, "", null, hasChildren);

    this.selectable = false;
}

WebInspector.StylePropertyTreeElementBase.prototype = {
    /**
     * @return {?WebInspector.DOMNode}
     */
    node: function()
    {
        return null;  // Overridden by ancestors.
    },

    /**
     * @return {?WebInspector.StylesSidebarPane}
     */
    editablePane: function()
    {
        return null;  // Overridden by ancestors.
    },

    /**
     * @return {!WebInspector.StylesSidebarPane|!WebInspector.ComputedStyleSidebarPane}
     */
    parentPane: function()
    {
        throw "Not implemented";
    },

    get inherited()
    {
        return this._inherited;
    },

    /**
     * @return {boolean}
     */
    hasIgnorableError: function()
    {
        return !this.parsedOk && WebInspector.StylesSidebarPane._ignoreErrorsForProperty(this.property);
    },

    set inherited(x)
    {
        if (x === this._inherited)
            return;
        this._inherited = x;
        this.updateState();
    },

    get overloaded()
    {
        return this._overloaded;
    },

    set overloaded(x)
    {
        if (x === this._overloaded)
            return;
        this._overloaded = x;
        this.updateState();
    },

    get disabled()
    {
        return this.property.disabled;
    },

    get name()
    {
        if (!this.disabled || !this.property.text)
            return this.property.name;

        var text = this.property.text;
        var index = text.indexOf(":");
        if (index < 1)
            return this.property.name;

        text = text.substring(0, index).trim();
        if (text.startsWith("/*"))
            text = text.substring(2).trim();
        return text;
    },

    get value()
    {
        if (!this.disabled || !this.property.text)
            return this.property.value;

        var match = this.property.text.match(/(.*);\s*/);
        if (!match || !match[1])
            return this.property.value;

        var text = match[1];
        var index = text.indexOf(":");
        if (index < 1)
            return this.property.value;

        return text.substring(index + 1).trim();
    },

    get parsedOk()
    {
        return this.property.parsedOk;
    },

    onattach: function()
    {
        this.updateTitle();
    },

    updateTitle: function()
    {
        var value = this.value;

        this.updateState();

        var nameElement = document.createElement("span");
        nameElement.className = "webkit-css-property";
        nameElement.textContent = this.name;
        nameElement.title = this.property.propertyText;
        this.nameElement = nameElement;

        this._expandElement = document.createElement("span");
        this._expandElement.className = "expand-element";

        var valueElement = document.createElement("span");
        valueElement.className = "value";
        this.valueElement = valueElement;

        /**
         * @param {!RegExp} regex
         * @param {function(string):!Node} processor
         * @param {?function(string):!Node} nextProcessor
         * @param {string} valueText
         * @return {!DocumentFragment}
         */
        function processValue(regex, processor, nextProcessor, valueText)
        {
            var container = document.createDocumentFragment();

            var items = valueText.replace(regex, "\0$1\0").split("\0");
            for (var i = 0; i < items.length; ++i) {
                if ((i % 2) === 0) {
                    if (nextProcessor)
                        container.appendChild(nextProcessor(items[i]));
                    else
                        container.appendChild(document.createTextNode(items[i]));
                } else {
                    var processedNode = processor(items[i]);
                    if (processedNode)
                        container.appendChild(processedNode);
                }
            }

            return container;
        }

        /**
         * @param {string} url
         * @return {!Node}
         * @this {WebInspector.StylePropertyTreeElementBase}
         */
        function linkifyURL(url)
        {
            var hrefUrl = url;
            var match = hrefUrl.match(/['"]?([^'"]+)/);
            if (match)
                hrefUrl = match[1];
            var container = document.createDocumentFragment();
            container.appendChild(document.createTextNode("url("));
            if (this._styleRule.rule && this._styleRule.rule.resourceURL())
                hrefUrl = WebInspector.ParsedURL.completeURL(this._styleRule.rule.resourceURL(), hrefUrl);
            else if (this.node())
                hrefUrl = this.node().resolveURL(hrefUrl);
            var hasResource = hrefUrl && !!WebInspector.resourceForURL(hrefUrl);
            // FIXME: WebInspector.linkifyURLAsNode() should really use baseURI.
            container.appendChild(WebInspector.linkifyURLAsNode(hrefUrl || url, url, undefined, !hasResource));
            container.appendChild(document.createTextNode(")"));
            return container;
        }

        if (value) {
            var colorProcessor = processValue.bind(null, WebInspector.StylesSidebarPane._colorRegex, this._processColor.bind(this, nameElement, valueElement), null);
            valueElement.appendChild(processValue(/url\(\s*([^)]+)\s*\)/g, linkifyURL.bind(this), WebInspector.CSSMetadata.isColorAwareProperty(this.name) && this.parsedOk ? colorProcessor : null, value));
        }

        this.listItemElement.removeChildren();
        nameElement.normalize();
        valueElement.normalize();

        if (!this.treeOutline)
            return;

        if (this.disabled)
            this.listItemElement.createChild("span", "styles-clipboard-only").createTextChild("/* ");
        this.listItemElement.appendChild(nameElement);
        this.listItemElement.appendChild(document.createTextNode(": "));
        this.listItemElement.appendChild(this._expandElement);
        this.listItemElement.appendChild(valueElement);
        this.listItemElement.appendChild(document.createTextNode(";"));
        if (this.disabled)
            this.listItemElement.createChild("span", "styles-clipboard-only").createTextChild(" */");

        if (!this.parsedOk) {
            // Avoid having longhands under an invalid shorthand.
            this.hasChildren = false;
            this.listItemElement.classList.add("not-parsed-ok");

            // Add a separate exclamation mark IMG element with a tooltip.
            this.listItemElement.insertBefore(WebInspector.StylesSidebarPane.createExclamationMark(this.property), this.listItemElement.firstChild);
        }
        if (this.property.inactive)
            this.listItemElement.classList.add("inactive");
        this._updateFilter();
    },

    _updateFilter: function()
    {
        var regEx = this.parentPane().filterRegex();
        this.listItemElement.classList.toggle("filter-match", !!regEx && (regEx.test(this.property.name) || regEx.test(this.property.value)));
    },

    /**
     * @param {!Element} nameElement
     * @param {!Element} valueElement
     * @param {string} text
     * @return {!Node}
     */
    _processColor: function(nameElement, valueElement, text)
    {
        var color = WebInspector.Color.parse(text);

        // We can be called with valid non-color values of |text| (like 'none' from border style)
        if (!color)
            return document.createTextNode(text);

        var format = WebInspector.StylesSidebarPane._colorFormat(color);
        var spectrumHelper = this.editablePane() && this.editablePane()._spectrumHelper;
        var spectrum = spectrumHelper ? spectrumHelper.spectrum() : null;

        var isEditable = !!(this._styleRule && this._styleRule.editable !== false); // |editable| is true by default.
        var colorSwatch = new WebInspector.ColorSwatch(!isEditable);
        colorSwatch.setColorString(text);
        colorSwatch.element.addEventListener("click", swatchClick.bind(this), false);

        var scrollerElement;
        var boundSpectrumChanged = spectrumChanged.bind(this);
        var boundSpectrumHidden = spectrumHidden.bind(this);

        /**
         * @param {!WebInspector.Event} e
         * @this {WebInspector.StylePropertyTreeElementBase}
         */
        function spectrumChanged(e)
        {
            var colorString = /** @type {string} */ (e.data);
            spectrum.displayText = colorString;
            colorValueElement.textContent = colorString;
            colorSwatch.setColorString(colorString);
            this.applyStyleText(nameElement.textContent + ": " + valueElement.textContent, false, false, false);
        }

        /**
         * @param {!WebInspector.Event} event
         * @this {WebInspector.StylePropertyTreeElementBase}
         */
        function spectrumHidden(event)
        {
            if (scrollerElement)
                scrollerElement.removeEventListener("scroll", repositionSpectrum, false);
            var commitEdit = event.data;
            var propertyText = !commitEdit && this.originalPropertyText ? this.originalPropertyText : (nameElement.textContent + ": " + valueElement.textContent);
            this.applyStyleText(propertyText, true, true, false);
            spectrum.removeEventListener(WebInspector.Spectrum.Events.ColorChanged, boundSpectrumChanged);
            spectrumHelper.removeEventListener(WebInspector.SpectrumPopupHelper.Events.Hidden, boundSpectrumHidden);

            delete this.editablePane()._isEditingStyle;
            delete this.originalPropertyText;
        }

        function repositionSpectrum()
        {
            spectrumHelper.reposition(colorSwatch.element);
        }

        /**
         * @param {!Event} e
         * @this {WebInspector.StylePropertyTreeElementBase}
         */
        function swatchClick(e)
        {
            e.consume(true);

            // Shift + click toggles color formats.
            // Click opens colorpicker, only if the element is not in computed styles section.
            if (!spectrumHelper || e.shiftKey) {
                changeColorDisplay();
                return;
            }

            if (!isEditable)
                return;

            var visible = spectrumHelper.toggle(colorSwatch.element, color, format);
            if (visible) {
                spectrum.displayText = color.toString(format);
                this.originalPropertyText = this.property.propertyText;
                this.editablePane()._isEditingStyle = true;
                spectrum.addEventListener(WebInspector.Spectrum.Events.ColorChanged, boundSpectrumChanged);
                spectrumHelper.addEventListener(WebInspector.SpectrumPopupHelper.Events.Hidden, boundSpectrumHidden);

                scrollerElement = colorSwatch.element.enclosingNodeOrSelfWithClass("style-panes-wrapper");
                if (scrollerElement)
                    scrollerElement.addEventListener("scroll", repositionSpectrum, false);
                else
                    console.error("Unable to handle color picker scrolling");
            }
        }

        var colorValueElement = document.createElement("span");
        if (format === WebInspector.Color.Format.Original)
            colorValueElement.textContent = text;
        else
            colorValueElement.textContent = color.toString(format);

        /**
         * @param {string} curFormat
         */
        function nextFormat(curFormat)
        {
            // The format loop is as follows:
            // * original
            // * rgb(a)
            // * hsl(a)
            // * nickname (if the color has a nickname)
            // * if the color is simple:
            //   - shorthex (if has short hex)
            //   - hex
            var cf = WebInspector.Color.Format;

            switch (curFormat) {
                case cf.Original:
                    return !color.hasAlpha() ? cf.RGB : cf.RGBA;

                case cf.RGB:
                case cf.RGBA:
                    return !color.hasAlpha() ? cf.HSL : cf.HSLA;

                case cf.HSL:
                case cf.HSLA:
                    if (color.nickname())
                        return cf.Nickname;
                    if (!color.hasAlpha())
                        return color.canBeShortHex() ? cf.ShortHEX : cf.HEX;
                    else
                        return cf.Original;

                case cf.ShortHEX:
                    return cf.HEX;

                case cf.HEX:
                    return cf.Original;

                case cf.Nickname:
                    if (!color.hasAlpha())
                        return color.canBeShortHex() ? cf.ShortHEX : cf.HEX;
                    else
                        return cf.Original;

                default:
                    return cf.RGBA;
            }
        }

        function changeColorDisplay()
        {
            do {
                format = nextFormat(format);
                var currentValue = color.toString(format);
            } while (currentValue === colorValueElement.textContent);
            colorValueElement.textContent = currentValue;
        }

        var container = document.createElement("nobr");
        container.appendChild(colorSwatch.element);
        container.appendChild(colorValueElement);
        return container;
    },

    updateState: function()
    {
        if (!this.listItemElement)
            return;

        if (this.style.isPropertyImplicit(this.name))
            this.listItemElement.classList.add("implicit");
        else
            this.listItemElement.classList.remove("implicit");

        if (this.hasIgnorableError())
            this.listItemElement.classList.add("has-ignorable-error");
        else
            this.listItemElement.classList.remove("has-ignorable-error");

        if (this.inherited)
            this.listItemElement.classList.add("inherited");
        else
            this.listItemElement.classList.remove("inherited");

        if (this.overloaded)
            this.listItemElement.classList.add("overloaded");
        else
            this.listItemElement.classList.remove("overloaded");

        if (this.disabled)
            this.listItemElement.classList.add("disabled");
        else
            this.listItemElement.classList.remove("disabled");
    },

    __proto__: TreeElement.prototype
}

/**
 * @constructor
 * @extends {WebInspector.StylePropertyTreeElementBase}
 * @param {!WebInspector.StylesSidebarPane} stylesPane
 * @param {!Object} styleRule
 * @param {!WebInspector.CSSStyleDeclaration} style
 * @param {!WebInspector.CSSProperty} property
 * @param {boolean} inherited
 */
WebInspector.ComputedStylePropertyTreeElement = function(stylesPane, styleRule, style, property, inherited)
{
    WebInspector.StylePropertyTreeElementBase.call(this, styleRule, style, property, inherited, false, false);
    this._stylesPane = stylesPane;
}

WebInspector.ComputedStylePropertyTreeElement.prototype = {
    /**
     * @return {?WebInspector.DOMNode}
     */
    node: function()
    {
        return this._stylesPane._node;
    },

    /**
     * @return {?WebInspector.StylesSidebarPane}
     */
    editablePane: function()
    {
        return null;
    },

    /**
     * @return {!WebInspector.ComputedStyleSidebarPane}
     */
    parentPane: function()
    {
        return this._stylesPane._computedStylePane;
    },

    _updateFilter: function()
    {
        var regEx = this.parentPane().filterRegex();
        this.listItemElement.classList.toggle("hidden", !!regEx && (!regEx.test(this.property.name) && !regEx.test(this.property.value)));
    },

    __proto__: WebInspector.StylePropertyTreeElementBase.prototype
}

/**
 * @constructor
 * @extends {WebInspector.StylePropertyTreeElementBase}
 * @param {!WebInspector.StylesSidebarPane} stylesPane
 * @param {!Object} styleRule
 * @param {!WebInspector.CSSStyleDeclaration} style
 * @param {!WebInspector.CSSProperty} property
 * @param {boolean} isShorthand
 * @param {boolean} inherited
 * @param {boolean} overloaded
 */
WebInspector.StylePropertyTreeElement = function(stylesPane, styleRule, style, property, isShorthand, inherited, overloaded)
{
    WebInspector.StylePropertyTreeElementBase.call(this, styleRule, style, property, inherited, overloaded, isShorthand);
    this._parentPane = stylesPane;
    this.isShorthand = isShorthand;
}

WebInspector.StylePropertyTreeElement.prototype = {
    /**
     * @return {?WebInspector.DOMNode}
     */
    node: function()
    {
        return this._parentPane._node;
    },

    /**
     * @return {?WebInspector.StylesSidebarPane}
     */
    editablePane: function()
    {
        return this._parentPane;
    },

    /**
     * @return {!WebInspector.StylesSidebarPane}
     */
    parentPane: function()
    {
        return this._parentPane;
    },

    /**
     * @return {?WebInspector.StylePropertiesSection}
     */
    section: function()
    {
        return this.treeOutline && this.treeOutline.section;
    },

    /**
     * @param {function()=} userCallback
     */
    _updatePane: function(userCallback)
    {
        var section = this.section();
        if (section && section._parentPane)
            section._parentPane._refreshUpdate(section, false, userCallback);
        else  {
            if (userCallback)
                userCallback();
        }
    },

    /**
     * @param {!WebInspector.CSSStyleDeclaration} newStyle
     */
    _applyNewStyle: function(newStyle)
    {
        newStyle.parentRule = this.style.parentRule;
        var oldStyleRange = /** @type {!WebInspector.TextRange} */ (this.style.range);
        var newStyleRange = /** @type {!WebInspector.TextRange} */ (newStyle.range);
        this.style = newStyle;
        this._styleRule.style = newStyle;
        if (this.style.parentRule) {
            this.style.parentRule.style = this.style;
            this._parentPane._styleSheetRuleEdited(this.style.parentRule, oldStyleRange, newStyleRange);
        }
    },

    /**
     * @param {!Event} event
     */
    toggleEnabled: function(event)
    {
        var disabled = !event.target.checked;

        /**
         * @param {?WebInspector.CSSStyleDeclaration} newStyle
         * @this {WebInspector.StylePropertyTreeElement}
         */
        function callback(newStyle)
        {
            delete this._parentPane._userOperation;

            if (!newStyle)
                return;
            this._applyNewStyle(newStyle);

            var section = this.section();
            if (section && section._parentPane)
                section._parentPane.dispatchEventToListeners("style property toggled");

            this._updatePane();
        }

        this._parentPane._userOperation = true;
        this.property.setDisabled(disabled, callback.bind(this));
        event.consume();
    },

    onpopulate: function()
    {
        // Only populate once and if this property is a shorthand.
        if (this.children.length || !this.isShorthand)
            return;

        var longhandProperties = this.style.longhandProperties(this.name);
        for (var i = 0; i < longhandProperties.length; ++i) {
            var name = longhandProperties[i].name;
            var inherited = false;
            var overloaded = false;

            var section = this.section();
            if (section) {
                inherited = section.isPropertyInherited(name);
                overloaded = section.isPropertyOverloaded(name);
            }

            var liveProperty = this.style.getLiveProperty(name);
            if (!liveProperty)
                continue;

            var item = new WebInspector.StylePropertyTreeElement(this._parentPane, this._styleRule, this.style, liveProperty, false, inherited, overloaded);
            this.appendChild(item);
        }
    },

    onattach: function()
    {
        WebInspector.StylePropertyTreeElementBase.prototype.onattach.call(this);

        this.listItemElement.addEventListener("mousedown", this._mouseDown.bind(this));
        this.listItemElement.addEventListener("mouseup", this._resetMouseDownElement.bind(this));
        this.listItemElement.addEventListener("click", this._mouseClick.bind(this));
    },

    _mouseDown: function(event)
    {
        if (this._parentPane) {
            this._parentPane._mouseDownTreeElement = this;
            this._parentPane._mouseDownTreeElementIsName = this._isNameElement(event.target);
            this._parentPane._mouseDownTreeElementIsValue = this._isValueElement(event.target);
        }
    },

    _resetMouseDownElement: function()
    {
        if (this._parentPane) {
            delete this._parentPane._mouseDownTreeElement;
            delete this._parentPane._mouseDownTreeElementIsName;
            delete this._parentPane._mouseDownTreeElementIsValue;
        }
    },

    updateTitle: function()
    {
        WebInspector.StylePropertyTreeElementBase.prototype.updateTitle.call(this);

        if (this.parsedOk && this.section() && this.parent.root) {
            var enabledCheckboxElement = document.createElement("input");
            enabledCheckboxElement.className = "enabled-button";
            enabledCheckboxElement.type = "checkbox";
            enabledCheckboxElement.checked = !this.disabled;
            enabledCheckboxElement.addEventListener("click", this.toggleEnabled.bind(this), false);
            this.listItemElement.insertBefore(enabledCheckboxElement, this.listItemElement.firstChild);
        }
    },

    _mouseClick: function(event)
    {
        if (!window.getSelection().isCollapsed)
            return;

        event.consume(true);

        if (event.target === this.listItemElement) {
            var section = this.section();
            if (!section || !section.editable)
                return;

            if (section._checkWillCancelEditing())
                return;
            section.addNewBlankProperty(this.property.index + 1).startEditing();
            return;
        }

        if (WebInspector.KeyboardShortcut.eventHasCtrlOrMeta(event) && this.section().navigable) {
            this._navigateToSource(event.target);
            return;
        }

        this.startEditing(event.target);
    },

    /**
     * @param {!Element} element
     */
    _navigateToSource: function(element)
    {
        console.assert(this.section().navigable);
        var propertyNameClicked = element === this.nameElement;
        WebInspector.Revealer.reveal(WebInspector.cssWorkspaceBinding.propertyUILocation(this.property, propertyNameClicked));
    },

    /**
     * @param {!Element} element
     */
    _isNameElement: function(element)
    {
        return element.enclosingNodeOrSelfWithClass("webkit-css-property") === this.nameElement;
    },

    /**
     * @param {!Element} element
     */
    _isValueElement: function(element)
    {
        return !!element.enclosingNodeOrSelfWithClass("value");
    },

    /**
     * @param {?Element=} selectElement
     */
    startEditing: function(selectElement)
    {
        // FIXME: we don't allow editing of longhand properties under a shorthand right now.
        if (this.parent.isShorthand)
            return;

        if (selectElement === this._expandElement)
            return;

        var section = this.section();
        if (section && !section.editable)
            return;

        if (!selectElement)
            selectElement = this.nameElement; // No arguments passed in - edit the name element by default.
        else
            selectElement = selectElement.enclosingNodeOrSelfWithClass("webkit-css-property") || selectElement.enclosingNodeOrSelfWithClass("value");

        if (WebInspector.isBeingEdited(selectElement))
            return;

        var isEditingName = selectElement === this.nameElement;
        if (!isEditingName)
            this.valueElement.textContent = restoreURLs(this.valueElement.textContent, this.value);

        /**
         * @param {string} fieldValue
         * @param {string} modelValue
         * @return {string}
         */
        function restoreURLs(fieldValue, modelValue)
        {
            const urlRegex = /\b(url\([^)]*\))/g;
            var splitFieldValue = fieldValue.split(urlRegex);
            if (splitFieldValue.length === 1)
                return fieldValue;
            var modelUrlRegex = new RegExp(urlRegex);
            for (var i = 1; i < splitFieldValue.length; i += 2) {
                var match = modelUrlRegex.exec(modelValue);
                if (match)
                    splitFieldValue[i] = match[0];
            }
            return splitFieldValue.join("");
        }

        var context = {
            expanded: this.expanded,
            hasChildren: this.hasChildren,
            isEditingName: isEditingName,
            previousContent: selectElement.textContent
        };

        // Lie about our children to prevent expanding on double click and to collapse shorthands.
        this.hasChildren = false;

        if (selectElement.parentElement)
            selectElement.parentElement.classList.add("child-editing");
        selectElement.textContent = selectElement.textContent; // remove color swatch and the like

        /**
         * @this {WebInspector.StylePropertyTreeElement}
         */
        function pasteHandler(context, event)
        {
            var data = event.clipboardData.getData("Text");
            if (!data)
                return;
            var colonIdx = data.indexOf(":");
            if (colonIdx < 0)
                return;
            var name = data.substring(0, colonIdx).trim();
            var value = data.substring(colonIdx + 1).trim();

            event.preventDefault();

            if (!("originalName" in context)) {
                context.originalName = this.nameElement.textContent;
                context.originalValue = this.valueElement.textContent;
            }
            this.property.name = name;
            this.property.value = value;
            this.nameElement.textContent = name;
            this.valueElement.textContent = value;
            this.nameElement.normalize();
            this.valueElement.normalize();

            this.editingCommitted(event.target.textContent, context, "forward");
        }

        /**
         * @this {WebInspector.StylePropertyTreeElement}
         */
        function blurListener(context, event)
        {
            var treeElement = this._parentPane._mouseDownTreeElement;
            var moveDirection = "";
            if (treeElement === this) {
                if (isEditingName && this._parentPane._mouseDownTreeElementIsValue)
                    moveDirection = "forward";
                if (!isEditingName && this._parentPane._mouseDownTreeElementIsName)
                    moveDirection = "backward";
            }
            this.editingCommitted(event.target.textContent, context, moveDirection);
        }

        delete this.originalPropertyText;

        this._parentPane._isEditingStyle = true;
        if (selectElement.parentElement)
            selectElement.parentElement.scrollIntoViewIfNeeded(false);

        var applyItemCallback = !isEditingName ? this._applyFreeFlowStyleTextEdit.bind(this, true) : undefined;
        this._prompt = new WebInspector.StylesSidebarPane.CSSPropertyPrompt(isEditingName ? WebInspector.CSSMetadata.cssPropertiesMetainfo : WebInspector.CSSMetadata.keywordsForProperty(this.nameElement.textContent), this, isEditingName);
        if (applyItemCallback) {
            this._prompt.addEventListener(WebInspector.TextPrompt.Events.ItemApplied, applyItemCallback, this);
            this._prompt.addEventListener(WebInspector.TextPrompt.Events.ItemAccepted, applyItemCallback, this);
        }
        var proxyElement = this._prompt.attachAndStartEditing(selectElement, blurListener.bind(this, context));

        proxyElement.addEventListener("keydown", this.editingNameValueKeyDown.bind(this, context), false);
        proxyElement.addEventListener("keypress", this.editingNameValueKeyPress.bind(this, context), false);
        if (isEditingName)
            proxyElement.addEventListener("paste", pasteHandler.bind(this, context), false);

        window.getSelection().setBaseAndExtent(selectElement, 0, selectElement, 1);
    },

    editingNameValueKeyDown: function(context, event)
    {
        if (event.handled)
            return;

        var isEditingName = context.isEditingName;
        var result;

        if (isEnterKey(event)) {
            event.preventDefault();
            result = "forward";
        } else if (event.keyCode === WebInspector.KeyboardShortcut.Keys.Esc.code || event.keyIdentifier === "U+001B")
            result = "cancel";
        else if (!isEditingName && this._newProperty && event.keyCode === WebInspector.KeyboardShortcut.Keys.Backspace.code) {
            // For a new property, when Backspace is pressed at the beginning of new property value, move back to the property name.
            var selection = window.getSelection();
            if (selection.isCollapsed && !selection.focusOffset) {
                event.preventDefault();
                result = "backward";
            }
        } else if (event.keyIdentifier === "U+0009") { // Tab key.
            result = event.shiftKey ? "backward" : "forward";
            event.preventDefault();
        }

        if (result) {
            switch (result) {
            case "cancel":
                this.editingCancelled(null, context);
                break;
            case "forward":
            case "backward":
                this.editingCommitted(event.target.textContent, context, result);
                break;
            }

            event.consume();
            return;
        }

        if (!isEditingName)
            this._applyFreeFlowStyleTextEdit(false);
    },

    editingNameValueKeyPress: function(context, event)
    {
        function shouldCommitValueSemicolon(text, cursorPosition)
        {
            // FIXME: should this account for semicolons inside comments?
            var openQuote = "";
            for (var i = 0; i < cursorPosition; ++i) {
                var ch = text[i];
                if (ch === "\\" && openQuote !== "")
                    ++i; // skip next character inside string
                else if (!openQuote && (ch === "\"" || ch === "'"))
                    openQuote = ch;
                else if (openQuote === ch)
                    openQuote = "";
            }
            return !openQuote;
        }

        var keyChar = String.fromCharCode(event.charCode);
        var isFieldInputTerminated = (context.isEditingName ? keyChar === ":" : keyChar === ";" && shouldCommitValueSemicolon(event.target.textContent, event.target.selectionLeftOffset()));
        if (isFieldInputTerminated) {
            // Enter or colon (for name)/semicolon outside of string (for value).
            event.consume(true);
            this.editingCommitted(event.target.textContent, context, "forward");
            return;
        }
    },

    _applyFreeFlowStyleTextEdit: function(now)
    {
        if (this._applyFreeFlowStyleTextEditTimer)
            clearTimeout(this._applyFreeFlowStyleTextEditTimer);

        /**
         * @this {WebInspector.StylePropertyTreeElement}
         */
        function apply()
        {
            var valueText = this.valueElement.textContent;
            if (valueText.indexOf(";") === -1)
                this.applyStyleText(this.nameElement.textContent + ": " + valueText, false, false, false);
        }
        if (now)
            apply.call(this);
        else
            this._applyFreeFlowStyleTextEditTimer = setTimeout(apply.bind(this), 100);
    },

    kickFreeFlowStyleEditForTest: function()
    {
        this._applyFreeFlowStyleTextEdit(true);
    },

    editingEnded: function(context)
    {
        this._resetMouseDownElement();
        if (this._applyFreeFlowStyleTextEditTimer)
            clearTimeout(this._applyFreeFlowStyleTextEditTimer);

        this.hasChildren = context.hasChildren;
        if (context.expanded)
            this.expand();
        var editedElement = context.isEditingName ? this.nameElement : this.valueElement;
        // The proxyElement has been deleted, no need to remove listener.
        if (editedElement.parentElement)
            editedElement.parentElement.classList.remove("child-editing");

        delete this._parentPane._isEditingStyle;
    },

    editingCancelled: function(element, context)
    {
        this._removePrompt();
        this._revertStyleUponEditingCanceled(this.originalPropertyText);
        // This should happen last, as it clears the info necessary to restore the property value after [Page]Up/Down changes.
        this.editingEnded(context);
    },

    _revertStyleUponEditingCanceled: function(originalPropertyText)
    {
        if (typeof originalPropertyText === "string") {
            delete this.originalPropertyText;
            this.applyStyleText(originalPropertyText, true, false, true);
        } else {
            if (this._newProperty)
                this.treeOutline.removeChild(this);
            else
                this.updateTitle();
        }
    },

    _findSibling: function(moveDirection)
    {
        var target = this;
        do {
            target = (moveDirection === "forward" ? target.nextSibling : target.previousSibling);
        } while(target && target.inherited);

        return target;
    },

    /**
     * @param {string} userInput
     * @param {!Object} context
     * @param {string} moveDirection
     */
    editingCommitted: function(userInput, context, moveDirection)
    {
        this._removePrompt();
        this.editingEnded(context);
        var isEditingName = context.isEditingName;

        // Determine where to move to before making changes
        var createNewProperty, moveToPropertyName, moveToSelector;
        var isDataPasted = "originalName" in context;
        var isDirtyViaPaste = isDataPasted && (this.nameElement.textContent !== context.originalName || this.valueElement.textContent !== context.originalValue);
        var isPropertySplitPaste = isDataPasted && isEditingName && this.valueElement.textContent !== context.originalValue;
        var moveTo = this;
        var moveToOther = (isEditingName ^ (moveDirection === "forward"));
        var abandonNewProperty = this._newProperty && !userInput && (moveToOther || isEditingName);
        if (moveDirection === "forward" && (!isEditingName || isPropertySplitPaste) || moveDirection === "backward" && isEditingName) {
            moveTo = moveTo._findSibling(moveDirection);
            if (moveTo)
                moveToPropertyName = moveTo.name;
            else if (moveDirection === "forward" && (!this._newProperty || userInput))
                createNewProperty = true;
            else if (moveDirection === "backward")
                moveToSelector = true;
        }

        // Make the Changes and trigger the moveToNextCallback after updating.
        var moveToIndex = moveTo && this.treeOutline ? this.treeOutline.children.indexOf(moveTo) : -1;
        var blankInput = /^\s*$/.test(userInput);
        var shouldCommitNewProperty = this._newProperty && (isPropertySplitPaste || moveToOther || (!moveDirection && !isEditingName) || (isEditingName && blankInput));
        var section = this.section();
        if (((userInput !== context.previousContent || isDirtyViaPaste) && !this._newProperty) || shouldCommitNewProperty) {
            section._afterUpdate = moveToNextCallback.bind(this, this._newProperty, !blankInput, section);
            var propertyText;
            if (blankInput || (this._newProperty && /^\s*$/.test(this.valueElement.textContent)))
                propertyText = "";
            else {
                if (isEditingName)
                    propertyText = userInput + ": " + this.property.value;
                else
                    propertyText = this.property.name + ": " + userInput;
            }
            this.applyStyleText(propertyText, true, true, false);
        } else {
            if (isEditingName)
                this.property.name = userInput;
            else
                this.property.value = userInput;
            if (!isDataPasted && !this._newProperty)
                this.updateTitle();
            moveToNextCallback.call(this, this._newProperty, false, section);
        }

        /**
         * The Callback to start editing the next/previous property/selector.
         * @this {WebInspector.StylePropertyTreeElement}
         */
        function moveToNextCallback(alreadyNew, valueChanged, section)
        {
            if (!moveDirection)
                return;

            // User just tabbed through without changes.
            if (moveTo && moveTo.parent) {
                moveTo.startEditing(!isEditingName ? moveTo.nameElement : moveTo.valueElement);
                return;
            }

            // User has made a change then tabbed, wiping all the original treeElements.
            // Recalculate the new treeElement for the same property we were going to edit next.
            if (moveTo && !moveTo.parent) {
                var propertyElements = section.propertiesTreeOutline.children;
                if (moveDirection === "forward" && blankInput && !isEditingName)
                    --moveToIndex;
                if (moveToIndex >= propertyElements.length && !this._newProperty)
                    createNewProperty = true;
                else {
                    var treeElement = moveToIndex >= 0 ? propertyElements[moveToIndex] : null;
                    if (treeElement) {
                        var elementToEdit = !isEditingName || isPropertySplitPaste ? treeElement.nameElement : treeElement.valueElement;
                        if (alreadyNew && blankInput)
                            elementToEdit = moveDirection === "forward" ? treeElement.nameElement : treeElement.valueElement;
                        treeElement.startEditing(elementToEdit);
                        return;
                    } else if (!alreadyNew)
                        moveToSelector = true;
                }
            }

            // Create a new attribute in this section (or move to next editable selector if possible).
            if (createNewProperty) {
                if (alreadyNew && !valueChanged && (isEditingName ^ (moveDirection === "backward")))
                    return;

                section.addNewBlankProperty().startEditing();
                return;
            }

            if (abandonNewProperty) {
                moveTo = this._findSibling(moveDirection);
                var sectionToEdit = (moveTo || moveDirection === "backward") ? section : section.nextEditableSibling();
                if (sectionToEdit) {
                    if (sectionToEdit.rule)
                        sectionToEdit.startEditingSelector();
                    else
                        sectionToEdit._moveEditorFromSelector(moveDirection);
                }
                return;
            }

            if (moveToSelector) {
                if (section.rule)
                    section.startEditingSelector();
                else
                    section._moveEditorFromSelector(moveDirection);
            }
        }
    },

    _removePrompt: function()
    {
        // BUG 53242. This cannot go into editingEnded(), as it should always happen first for any editing outcome.
        if (this._prompt) {
            this._prompt.detach();
            delete this._prompt;
        }
    },

    _hasBeenModifiedIncrementally: function()
    {
        // New properties applied via up/down or live editing have an originalPropertyText and will be deleted later
        // on, if cancelled, when the empty string gets applied as their style text.
        return typeof this.originalPropertyText === "string" || (!!this.property.propertyText && this._newProperty);
    },

    styleTextAppliedForTest: function()
    {
    },

    applyStyleText: function(styleText, updateInterface, majorChange, isRevert)
    {
        function userOperationFinishedCallback(parentPane, updateInterface)
        {
            if (updateInterface)
                delete parentPane._userOperation;
        }

        // Leave a way to cancel editing after incremental changes.
        if (!isRevert && !updateInterface && !this._hasBeenModifiedIncrementally()) {
            // Remember the rule's original CSS text on [Page](Up|Down), so it can be restored
            // if the editing is canceled.
            this.originalPropertyText = this.property.propertyText;
        }

        if (!this.treeOutline)
            return;

        var section = this.section();
        styleText = styleText.replace(/\s/g, " ").trim(); // Replace &nbsp; with whitespace.
        var styleTextLength = styleText.length;
        if (!styleTextLength && updateInterface && !isRevert && this._newProperty && !this._hasBeenModifiedIncrementally()) {
            // The user deleted everything and never applied a new property value via Up/Down scrolling/live editing, so remove the tree element and update.
            this.parent.removeChild(this);
            section.afterUpdate();
            return;
        }

        var currentNode = this._parentPane._node;
        if (updateInterface)
            this._parentPane._userOperation = true;

        /**
         * @param {function()} userCallback
         * @param {string} originalPropertyText
         * @param {?WebInspector.CSSStyleDeclaration} newStyle
         * @this {WebInspector.StylePropertyTreeElement}
         */
        function callback(userCallback, originalPropertyText, newStyle)
        {
            if (!newStyle) {
                if (updateInterface) {
                    // It did not apply, cancel editing.
                    this._revertStyleUponEditingCanceled(originalPropertyText);
                }
                userCallback();
                return;
            }
            this._applyNewStyle(newStyle);

            if (this._newProperty)
                this._newPropertyInStyle = true;

            this.property = newStyle.propertyAt(this.property.index);
            if (section && section._parentPane)
                section._parentPane.dispatchEventToListeners("style edited");

            if (updateInterface && currentNode === this.node()) {
                this._updatePane(userCallback);
                this.styleTextAppliedForTest();
                return;
            }

            userCallback();
            this.styleTextAppliedForTest();
        }

        // Append a ";" if the new text does not end in ";".
        // FIXME: this does not handle trailing comments.
        if (styleText.length && !/;\s*$/.test(styleText))
            styleText += ";";
        var overwriteProperty = !!(!this._newProperty || this._newPropertyInStyle);
        this.property.setText(styleText, majorChange, overwriteProperty, callback.bind(this, userOperationFinishedCallback.bind(null, this._parentPane, updateInterface), this.originalPropertyText));
    },

    /**
     * @return {boolean}
     */
    ondblclick: function()
    {
        return true; // handled
    },

    /**
     * @param {!Event} event
     * @return {boolean}
     */
    isEventWithinDisclosureTriangle: function(event)
    {
        return event.target === this._expandElement;
    },

    __proto__: WebInspector.StylePropertyTreeElementBase.prototype
}

/**
 * @constructor
 * @extends {WebInspector.TextPrompt}
 * @param {!WebInspector.CSSMetadata} cssCompletions
 * @param {!WebInspector.StylePropertyTreeElement} sidebarPane
 * @param {boolean} isEditingName
 */
WebInspector.StylesSidebarPane.CSSPropertyPrompt = function(cssCompletions, sidebarPane, isEditingName)
{
    // Use the same callback both for applyItemCallback and acceptItemCallback.
    WebInspector.TextPrompt.call(this, this._buildPropertyCompletions.bind(this), WebInspector.StyleValueDelimiters);
    this.setSuggestBoxEnabled(true);
    this._cssCompletions = cssCompletions;
    this._sidebarPane = sidebarPane;
    this._isEditingName = isEditingName;

    if (!isEditingName)
        this.disableDefaultSuggestionForEmptyInput();
}

WebInspector.StylesSidebarPane.CSSPropertyPrompt.prototype = {
    /**
     * @param {!Event} event
     */
    onKeyDown: function(event)
    {
        switch (event.keyIdentifier) {
        case "Up":
        case "Down":
        case "PageUp":
        case "PageDown":
            if (this._handleNameOrValueUpDown(event)) {
                event.preventDefault();
                return;
            }
            break;
        case "Enter":
            if (this.autoCompleteElement && !this.autoCompleteElement.textContent.length) {
                this.tabKeyPressed();
                return;
            }
            break;
        }

        WebInspector.TextPrompt.prototype.onKeyDown.call(this, event);
    },

    onMouseWheel: function(event)
    {
        if (this._handleNameOrValueUpDown(event)) {
            event.consume(true);
            return;
        }
        WebInspector.TextPrompt.prototype.onMouseWheel.call(this, event);
    },

    /**
     * @override
     * @return {boolean}
     */
    tabKeyPressed: function()
    {
        this.acceptAutoComplete();

        // Always tab to the next field.
        return false;
    },

    /**
     * @param {!Event} event
     * @return {boolean}
     */
    _handleNameOrValueUpDown: function(event)
    {
        /**
         * @param {string} originalValue
         * @param {string} replacementString
         * @this {WebInspector.StylesSidebarPane.CSSPropertyPrompt}
         */
        function finishHandler(originalValue, replacementString)
        {
            // Synthesize property text disregarding any comments, custom whitespace etc.
            this._sidebarPane.applyStyleText(this._sidebarPane.nameElement.textContent + ": " + this._sidebarPane.valueElement.textContent, false, false, false);
        }

        /**
         * @param {string} prefix
         * @param {number} number
         * @param {string} suffix
         * @return {string}
         * @this {WebInspector.StylesSidebarPane.CSSPropertyPrompt}
         */
        function customNumberHandler(prefix, number, suffix)
        {
            if (number !== 0 && !suffix.length && WebInspector.CSSMetadata.isLengthProperty(this._sidebarPane.property.name))
                suffix = "px";
            return prefix + number + suffix;
        }

        // Handle numeric value increment/decrement only at this point.
        if (!this._isEditingName && WebInspector.handleElementValueModifications(event, this._sidebarPane.valueElement, finishHandler.bind(this), this._isValueSuggestion.bind(this), customNumberHandler.bind(this)))
            return true;

        return false;
    },

    /**
     * @param {string} word
     * @return {boolean}
     */
    _isValueSuggestion: function(word)
    {
        if (!word)
            return false;
        word = word.toLowerCase();
        return this._cssCompletions.keySet().hasOwnProperty(word);
    },

    /**
     * @param {!Element} proxyElement
     * @param {!Range} wordRange
     * @param {boolean} force
     * @param {function(!Array.<string>, number=)} completionsReadyCallback
     */
    _buildPropertyCompletions: function(proxyElement, wordRange, force, completionsReadyCallback)
    {
        var prefix = wordRange.toString().toLowerCase();
        if (!prefix && !force && (this._isEditingName || proxyElement.textContent.length)) {
            completionsReadyCallback([]);
            return;
        }

        var results = this._cssCompletions.startsWith(prefix);
        var userEnteredText = wordRange.toString().replace("-", "");
        if (userEnteredText && (userEnteredText === userEnteredText.toUpperCase())) {
            for (var i = 0; i < results.length; ++i)
                results[i] = results[i].toUpperCase();
        }
        var selectedIndex = this._cssCompletions.mostUsedOf(results);
        completionsReadyCallback(results, selectedIndex);
    },

    __proto__: WebInspector.TextPrompt.prototype
}
