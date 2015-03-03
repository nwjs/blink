/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "config.h"
#include "core/inspector/InspectorConsoleAgent.h"

#include "bindings/core/v8/ScriptCallStackFactory.h"
#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/ScriptProfiler.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/UseCounter.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/inspector/InjectedScriptHost.h"
#include "core/inspector/InjectedScriptManager.h"
#include "core/inspector/InspectorConsoleMessage.h"
#include "core/inspector/InspectorState.h"
#include "core/inspector/InspectorTimelineAgent.h"
#include "core/inspector/InspectorTracingAgent.h"
#include "core/inspector/InstrumentingAgents.h"
#include "core/inspector/ScriptArguments.h"
#include "core/inspector/ScriptCallFrame.h"
#include "core/inspector/ScriptCallStack.h"
#include "core/loader/DocumentLoader.h"
#include "core/page/Page.h"
#include "core/workers/WorkerGlobalScopeProxy.h"
#include "platform/network/ResourceError.h"
#include "platform/network/ResourceResponse.h"
#include "wtf/CurrentTime.h"
#include "wtf/OwnPtr.h"
#include "wtf/PassOwnPtr.h"
#include "wtf/text/StringBuilder.h"
#include "wtf/text/WTFString.h"

namespace blink {

static const unsigned maximumConsoleMessages = 1000;
static const int expireConsoleMessagesStep = 100;

namespace ConsoleAgentState {
static const char monitoringXHR[] = "monitoringXHR";
static const char consoleMessagesEnabled[] = "consoleMessagesEnabled";
static const char tracingBasedTimeline[] = "tracingBasedTimeline";
}

int InspectorConsoleAgent::s_enabledAgentCount = 0;

InspectorConsoleAgent::InspectorConsoleAgent(InspectorTimelineAgent* timelineAgent, InspectorTracingAgent* tracingAgent, InjectedScriptManager* injectedScriptManager)
    : InspectorBaseAgent<InspectorConsoleAgent>("Console")
    , m_timelineAgent(timelineAgent)
    , m_tracingAgent(tracingAgent)
    , m_injectedScriptManager(injectedScriptManager)
    , m_frontend(0)
    , m_expiredConsoleMessageCount(0)
    , m_enabled(false)
{
}

InspectorConsoleAgent::~InspectorConsoleAgent()
{
#if !ENABLE(OILPAN)
    m_instrumentingAgents->setInspectorConsoleAgent(0);
#endif
}

void InspectorConsoleAgent::trace(Visitor* visitor)
{
    visitor->trace(m_timelineAgent);
    visitor->trace(m_tracingAgent);
    visitor->trace(m_injectedScriptManager);
    InspectorBaseAgent::trace(visitor);
}

void InspectorConsoleAgent::init()
{
    m_instrumentingAgents->setInspectorConsoleAgent(this);
}

void InspectorConsoleAgent::enable(ErrorString*)
{
    if (m_enabled)
        return;
    m_enabled = true;
    if (!s_enabledAgentCount)
        ScriptController::setCaptureCallStackForUncaughtExceptions(true);
    ++s_enabledAgentCount;

    m_state->setBoolean(ConsoleAgentState::consoleMessagesEnabled, true);

    if (m_expiredConsoleMessageCount) {
        InspectorConsoleMessage expiredMessage(OtherMessageSource, LogMessageType, WarningMessageLevel, String::format("%d console messages are not shown.", m_expiredConsoleMessageCount));
        expiredMessage.setTimestamp(0);
        expiredMessage.addToFrontend(m_frontend, m_injectedScriptManager, false);
    }

    size_t messageCount = m_consoleMessages.size();
    for (size_t i = 0; i < messageCount; ++i)
        m_consoleMessages[i]->addToFrontend(m_frontend, m_injectedScriptManager, false);
}

void InspectorConsoleAgent::disable(ErrorString*)
{
    if (!m_enabled)
        return;
    m_enabled = false;
    if (!(--s_enabledAgentCount))
        ScriptController::setCaptureCallStackForUncaughtExceptions(false);
    m_state->setBoolean(ConsoleAgentState::consoleMessagesEnabled, false);
    m_state->setBoolean(ConsoleAgentState::tracingBasedTimeline, false);
}

void InspectorConsoleAgent::clearMessages(ErrorString*)
{
    m_consoleMessages.clear();
    m_expiredConsoleMessageCount = 0;
    m_injectedScriptManager->releaseObjectGroup("console");
    if (m_frontend && m_enabled)
        m_frontend->messagesCleared();
}

void InspectorConsoleAgent::reset()
{
    ErrorString error;
    clearMessages(&error);
    m_times.clear();
    m_counts.clear();
}

void InspectorConsoleAgent::restore()
{
    if (m_state->getBoolean(ConsoleAgentState::consoleMessagesEnabled)) {
        m_frontend->messagesCleared();
        ErrorString error;
        enable(&error);
    }
}

void InspectorConsoleAgent::setFrontend(InspectorFrontend* frontend)
{
    m_frontend = frontend->console();
}

void InspectorConsoleAgent::clearFrontend()
{
    m_frontend = 0;
    String errorString;
    disable(&errorString);
}

void InspectorConsoleAgent::addMessageToConsole(ConsoleMessage* consoleMessage)
{
    InspectorConsoleMessage* message;
    if (consoleMessage->callStack()) {
        message = new InspectorConsoleMessage(consoleMessage->source(), LogMessageType, consoleMessage->level(), consoleMessage->message(), consoleMessage->callStack(), consoleMessage->requestIdentifier());
    } else {
        bool shouldGenerateCallStack = m_frontend;
        message = new InspectorConsoleMessage(shouldGenerateCallStack, consoleMessage->source(), LogMessageType, consoleMessage->level(), consoleMessage->message(), consoleMessage->url(), consoleMessage->lineNumber(), consoleMessage->columnNumber(), consoleMessage->scriptState(), consoleMessage->requestIdentifier());
    }
    message->setWorkerGlobalScopeProxy(consoleMessage->workerId());
    addConsoleMessage(adoptPtr(message));
}

void InspectorConsoleAgent::adoptWorkerConsoleMessages(WorkerGlobalScopeProxy* proxy)
{
    for (size_t i = 0; i < m_consoleMessages.size(); i++) {
        if (m_consoleMessages[i]->workerGlobalScopeProxy() == proxy)
            m_consoleMessages[i]->setWorkerGlobalScopeProxy(nullptr);
    }
}

void InspectorConsoleAgent::addConsoleAPIMessageToConsole(MessageType type, MessageLevel level, const String& message, ScriptState* scriptState, PassRefPtrWillBeRawPtr<ScriptArguments> arguments, unsigned long requestIdentifier)
{
    if (type == ClearMessageType) {
        ErrorString error;
        clearMessages(&error);
    }

    addConsoleMessage(adoptPtr(new InspectorConsoleMessage(ConsoleAPIMessageSource, type, level, message, arguments, scriptState, requestIdentifier)));
}

Vector<unsigned> InspectorConsoleAgent::consoleMessageArgumentCounts()
{
    Vector<unsigned> result(m_consoleMessages.size());
    for (size_t i = 0; i < m_consoleMessages.size(); i++)
        result[i] = m_consoleMessages[i]->argumentCount();
    return result;
}

void InspectorConsoleAgent::consoleTime(ExecutionContext*, const String& title)
{
    // Follow Firebug's behavior of requiring a title that is not null or
    // undefined for timing functions
    if (title.isNull())
        return;

    m_times.add(title, monotonicallyIncreasingTime());
}

void InspectorConsoleAgent::consoleTimeEnd(ExecutionContext*, const String& title, ScriptState* scriptState)
{
    // Follow Firebug's behavior of requiring a title that is not null or
    // undefined for timing functions
    if (title.isNull())
        return;

    HashMap<String, double>::iterator it = m_times.find(title);
    if (it == m_times.end())
        return;

    double startTime = it->value;
    m_times.remove(it);

    double elapsed = monotonicallyIncreasingTime() - startTime;
    String message = title + String::format(": %.3fms", elapsed * 1000);
    addConsoleAPIMessageToConsole(LogMessageType, DebugMessageLevel, message, scriptState, nullptr);
}

void InspectorConsoleAgent::setTracingBasedTimeline(ErrorString*, bool enabled)
{
    m_state->setBoolean(ConsoleAgentState::tracingBasedTimeline, enabled);
}

void InspectorConsoleAgent::consoleTimeline(ExecutionContext* context, const String& title, ScriptState* scriptState)
{
    UseCounter::count(context, UseCounter::DevToolsConsoleTimeline);
    if (m_tracingAgent && m_state->getBoolean(ConsoleAgentState::tracingBasedTimeline))
        m_tracingAgent->consoleTimeline(title);
    else
        m_timelineAgent->consoleTimeline(context, title, scriptState);
}

void InspectorConsoleAgent::consoleTimelineEnd(ExecutionContext* context, const String& title, ScriptState* scriptState)
{
    if (m_state->getBoolean(ConsoleAgentState::tracingBasedTimeline))
        m_tracingAgent->consoleTimelineEnd(title);
    else
        m_timelineAgent->consoleTimelineEnd(context, title, scriptState);
}

void InspectorConsoleAgent::consoleCount(ScriptState* scriptState, PassRefPtrWillBeRawPtr<ScriptArguments> arguments)
{
    RefPtrWillBeRawPtr<ScriptCallStack> callStack(createScriptCallStack(1));
    const ScriptCallFrame& lastCaller = callStack->at(0);
    // Follow Firebug's behavior of counting with null and undefined title in
    // the same bucket as no argument
    String title;
    arguments->getFirstArgumentAsString(title);
    String identifier = title.isEmpty() ? String(lastCaller.sourceURL() + ':' + String::number(lastCaller.lineNumber()))
                                        : String(title + '@');

    HashCountedSet<String>::AddResult result = m_counts.add(identifier);
    String message = title + ": " + String::number(result.storedValue->value);
    addConsoleAPIMessageToConsole(LogMessageType, DebugMessageLevel, message, scriptState, nullptr);
}

void InspectorConsoleAgent::frameWindowDiscarded(LocalDOMWindow* window)
{
    size_t messageCount = m_consoleMessages.size();
    for (size_t i = 0; i < messageCount; ++i)
        m_consoleMessages[i]->windowCleared(window);
    m_injectedScriptManager->discardInjectedScriptsFor(window);
}

void InspectorConsoleAgent::didCommitLoad(LocalFrame* frame, DocumentLoader* loader)
{
    if (loader->frame() != frame->page()->mainFrame())
        return;
    reset();
}

void InspectorConsoleAgent::didFinishXHRLoading(XMLHttpRequest*, ThreadableLoaderClient*, unsigned long requestIdentifier, ScriptString, const AtomicString& method, const String& url, const String& sendURL, unsigned sendLineNumber)
{
    if (m_frontend && m_state->getBoolean(ConsoleAgentState::monitoringXHR)) {
        String message = "XHR finished loading: " + method + " \"" + url + "\".";
        RefPtrWillBeRawPtr<ConsoleMessage> consoleMessage = ConsoleMessage::create(NetworkMessageSource, DebugMessageLevel, message, sendURL, sendLineNumber);
        consoleMessage->setRequestIdentifier(requestIdentifier);
        addMessageToConsole(consoleMessage.get());
    }
}

void InspectorConsoleAgent::didReceiveResourceResponse(LocalFrame*, unsigned long requestIdentifier, DocumentLoader* loader, const ResourceResponse& response, ResourceLoader* resourceLoader)
{
    if (!loader)
        return;
    if (response.httpStatusCode() >= 400) {
        String message = "Failed to load resource: the server responded with a status of " + String::number(response.httpStatusCode()) + " (" + response.httpStatusText() + ')';
        RefPtrWillBeRawPtr<ConsoleMessage> consoleMessage = ConsoleMessage::create(NetworkMessageSource, ErrorMessageLevel, message, response.url().string());
        consoleMessage->setRequestIdentifier(requestIdentifier);
        addMessageToConsole(consoleMessage.get());
    }
}

void InspectorConsoleAgent::didFailLoading(unsigned long requestIdentifier, const ResourceError& error, bool isInternalRequest)
{
    if (isInternalRequest)
        return;
    
    if (error.isCancellation()) // Report failures only.
        return;
    StringBuilder message;
    message.appendLiteral("Failed to load resource");
    if (!error.localizedDescription().isEmpty()) {
        message.appendLiteral(": ");
        message.append(error.localizedDescription());
    }
    RefPtrWillBeRawPtr<ConsoleMessage> consoleMessage = ConsoleMessage::create(NetworkMessageSource, ErrorMessageLevel, message.toString(), error.failingURL());
    consoleMessage->setRequestIdentifier(requestIdentifier);
    addMessageToConsole(consoleMessage.get());
}

void InspectorConsoleAgent::setMonitoringXHREnabled(ErrorString*, bool enabled)
{
    m_state->setBoolean(ConsoleAgentState::monitoringXHR, enabled);
}

void InspectorConsoleAgent::addConsoleMessage(PassOwnPtr<InspectorConsoleMessage> consoleMessage)
{
    ASSERT_ARG(consoleMessage, consoleMessage);

    if (m_frontend && m_enabled)
        consoleMessage->addToFrontend(m_frontend, m_injectedScriptManager, true);

    m_consoleMessages.append(consoleMessage);

    if (!m_frontend && m_consoleMessages.size() >= maximumConsoleMessages) {
        m_expiredConsoleMessageCount += expireConsoleMessagesStep;
        m_consoleMessages.remove(0, expireConsoleMessagesStep);
    }
}

class InspectableHeapObject FINAL : public InjectedScriptHost::InspectableObject {
public:
    explicit InspectableHeapObject(int heapObjectId) : m_heapObjectId(heapObjectId) { }
    virtual ScriptValue get(ScriptState*) OVERRIDE
    {
        return ScriptProfiler::objectByHeapObjectId(m_heapObjectId);
    }
private:
    int m_heapObjectId;
};

void InspectorConsoleAgent::addInspectedHeapObject(ErrorString*, int inspectedHeapObjectId)
{
    m_injectedScriptManager->injectedScriptHost()->addInspectedObject(adoptPtr(new InspectableHeapObject(inspectedHeapObjectId)));
}

} // namespace blink
