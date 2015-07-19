/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "bindings/core/v8/V8Initializer.h"

#include "bindings/core/v8/DOMWrapperWorld.h"
#include "bindings/core/v8/ScriptCallStackFactory.h"
#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/ScriptProfiler.h"
#include "bindings/core/v8/ScriptValue.h"
#include "bindings/core/v8/V8Binding.h"
#include "bindings/core/v8/V8DOMException.h"
#include "bindings/core/v8/V8ErrorEvent.h"
#include "bindings/core/v8/V8ErrorHandler.h"
#include "bindings/core/v8/V8GCController.h"
#include "bindings/core/v8/V8History.h"
#include "bindings/core/v8/V8Location.h"
#include "bindings/core/v8/V8PerContextData.h"
#include "bindings/core/v8/V8Window.h"
#include "core/dom/Document.h"
#include "core/dom/ExceptionCode.h"
#include "core/frame/ConsoleTypes.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/inspector/ScriptArguments.h"
#include "core/inspector/ScriptCallStack.h"
#include "platform/EventDispatchForbiddenScope.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/TraceEvent.h"
#include "platform/heap/AddressSanitizer.h"
#include "platform/scheduler/Scheduler.h"
#include "public/platform/Platform.h"
#include "wtf/RefPtr.h"
#include "wtf/ThreadSpecific.h"
#include "wtf/text/WTFString.h"
#include <v8-debug.h>

#include "third_party/node/src/node_webkit.h"

namespace blink {

static Frame* findFrame(v8::Local<v8::Object> host, v8::Local<v8::Value> data, v8::Isolate* isolate)
{
    const WrapperTypeInfo* type = WrapperTypeInfo::unwrap(data);

    if (V8Window::wrapperTypeInfo.equals(type)) {
        v8::Handle<v8::Object> windowWrapper = V8Window::findInstanceInPrototypeChain(host, isolate);
        if (windowWrapper.IsEmpty())
            return 0;
        return V8Window::toImpl(windowWrapper)->frame();
    }

    if (V8History::wrapperTypeInfo.equals(type))
        return V8History::toImpl(host)->frame();

    if (V8Location::wrapperTypeInfo.equals(type))
        return V8Location::toImpl(host)->frame();

    // This function can handle only those types listed above.
    ASSERT_NOT_REACHED();
    return 0;
}

static void reportFatalErrorInMainThread(const char* location, const char* message)
{
    int memoryUsageMB = blink::Platform::current()->actualMemoryUsageMB();
    printf("V8 error: %s (%s).  Current memory usage: %d MB\n", message, location, memoryUsageMB);
    CRASH();
}

static PassRefPtrWillBeRawPtr<ScriptCallStack> extractCallStack(v8::Isolate* isolate, v8::Handle<v8::Message> message, int* const scriptId)
{
    v8::Handle<v8::StackTrace> stackTrace = message->GetStackTrace();
    RefPtrWillBeRawPtr<ScriptCallStack> callStack = nullptr;
    *scriptId = message->GetScriptOrigin().ScriptID()->Value();
    // Currently stack trace is only collected when inspector is open.
    if (!stackTrace.IsEmpty() && stackTrace->GetFrameCount() > 0) {
        callStack = createScriptCallStack(isolate, stackTrace, ScriptCallStack::maxCallStackSizeToCapture);
        bool success = false;
        int topScriptId = callStack->at(0).scriptId().toInt(&success);
        if (success && topScriptId == *scriptId)
            *scriptId = 0;
    } else {
        Vector<ScriptCallFrame> callFrames;
        callStack = ScriptCallStack::create(callFrames);
    }
    return callStack.release();
}

static String extractResourceName(v8::Handle<v8::Message> message, const Document* document)
{
    v8::Handle<v8::Value> resourceName = message->GetScriptOrigin().ResourceName();
    bool shouldUseDocumentURL = document && (resourceName.IsEmpty() || !resourceName->IsString());
    return shouldUseDocumentURL ? document->url() : toCoreString(resourceName.As<v8::String>());
}

static String extractMessageForConsole(v8::Handle<v8::Value> data)
{
    if (V8DOMWrapper::isDOMWrapper(data)) {
        v8::Handle<v8::Object> obj = v8::Handle<v8::Object>::Cast(data);
        const WrapperTypeInfo* type = toWrapperTypeInfo(obj);
        if (V8DOMException::wrapperTypeInfo.isSubclass(type)) {
            DOMException* exception = V8DOMException::toImpl(obj);
            if (exception && !exception->messageForConsole().isEmpty())
                return exception->toStringForConsole();
        }
    }
    return emptyString();
}

static void messageHandlerInMainThread(v8::Handle<v8::Message> message, v8::Handle<v8::Value> data)
{
    ASSERT(isMainThread());

    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handleScope(isolate);

    // It's possible that messageHandlerInMainThread() is invoked while we're initializing a window.
    // In that half-baked situation, we don't have a valid context nor a valid world,
    // so just return immediately.
    if (DOMWrapperWorld::windowIsBeingInitialized()) {
            v8::Local<v8::Context> node_context =
                v8::Local<v8::Context>::New(isolate, node::g_context);
            node_context->Enter();
            node::OnMessage(message, data);
            node_context->Exit();
            return;
    }
    // If called during context initialization, there will be no entered window.
    LocalDOMWindow* enteredWindow = enteredDOMWindow(isolate);
    // pass messages to node only if the object is created from node context
    if (data->IsObject() && data->ToObject()->CreationContext() == node::g_context) {
        if (enteredWindow)  {
            Frame* frame = enteredWindow->document()->frame();
            if (frame && frame->isNodeJS()) {
                v8::Local<v8::Context> node_context =
                    v8::Local<v8::Context>::New(isolate, node::g_context);
                node_context->Enter();
                node::OnMessage(message, data);
                node_context->Exit();
            }
        } else {
            v8::Local<v8::Context> node_context =
                v8::Local<v8::Context>::New(isolate, node::g_context);
            node_context->Enter();
            node::OnMessage(message, data);
            node_context->Exit();
        }
    }

    if (!enteredWindow || !enteredWindow->isCurrentlyDisplayedInFrame())
        return;

    int scriptId = 0;
    RefPtrWillBeRawPtr<ScriptCallStack> callStack = extractCallStack(isolate, message, &scriptId);
    String resourceName = extractResourceName(message, enteredWindow->document());
    AccessControlStatus corsStatus = message->IsSharedCrossOrigin() ? SharableCrossOrigin : NotSharableCrossOrigin;

    ScriptState* scriptState = ScriptState::current(isolate);
    String errorMessage = toCoreString(message->Get());
    RefPtrWillBeRawPtr<ErrorEvent> event = ErrorEvent::create(errorMessage, resourceName, message->GetLineNumber(), message->GetStartColumn() + 1, &scriptState->world());

    String messageForConsole = extractMessageForConsole(data);
    if (!messageForConsole.isEmpty())
        event->setUnsanitizedMessage("Uncaught " + messageForConsole);

    // This method might be called while we're creating a new context. In this case, we
    // avoid storing the exception object, as we can't create a wrapper during context creation.
    // FIXME: Can we even get here during initialization now that we bail out when GetEntered returns an empty handle?
    LocalFrame* frame = enteredWindow->document()->frame();
    if (frame && frame->script().existingWindowProxy(scriptState->world())) {
        V8ErrorHandler::storeExceptionOnErrorEventWrapper(isolate, event.get(), data, scriptState->context()->Global());
    }

    if (scriptState->world().isPrivateScriptIsolatedWorld()) {
        // We allow a private script to dispatch error events even in a EventDispatchForbiddenScope scope.
        // Without having this ability, it's hard to debug the private script because syntax errors
        // in the private script are not reported to console (the private script just crashes silently).
        // Allowing error events in private scripts is safe because error events don't propagate to
        // other isolated worlds (which means that the error events won't fire any event listeners
        // in user's scripts).
        EventDispatchForbiddenScope::AllowUserAgentEvents allowUserAgentEvents;
        enteredWindow->document()->reportException(event.release(), scriptId, callStack, corsStatus);
    } else {
        enteredWindow->document()->reportException(event.release(), scriptId, callStack, corsStatus);
    }
}

namespace {

class PromiseRejectMessage {
    ALLOW_ONLY_INLINE_ALLOCATION();
public:
    PromiseRejectMessage(const ScriptValue& promise, const ScriptValue& exception, const String& errorMessage, const String& resourceName, int scriptId, int lineNumber, int columnNumber, PassRefPtrWillBeRawPtr<ScriptCallStack> callStack)
        : m_promise(promise)
        , m_exception(exception)
        , m_errorMessage(errorMessage)
        , m_resourceName(resourceName)
        , m_scriptId(scriptId)
        , m_lineNumber(lineNumber)
        , m_columnNumber(columnNumber)
        , m_callStack(callStack)
    {
    }

    void trace(Visitor* visitor)
    {
        visitor->trace(m_callStack);
    }

    const ScriptValue m_promise;
    const ScriptValue m_exception;
    const String m_errorMessage;
    const String m_resourceName;
    const int m_scriptId;
    const int m_lineNumber;
    const int m_columnNumber;
    const RefPtrWillBeMember<ScriptCallStack> m_callStack;
};

} // namespace

typedef Deque<PromiseRejectMessage> PromiseRejectMessageQueue;

static PromiseRejectMessageQueue& promiseRejectMessageQueue()
{
    AtomicallyInitializedStatic(ThreadSpecific<PromiseRejectMessageQueue>*, queue = new ThreadSpecific<PromiseRejectMessageQueue>);
    return **queue;
}

void V8Initializer::reportRejectedPromises()
{
    PromiseRejectMessageQueue& queue = promiseRejectMessageQueue();
    while (!queue.isEmpty()) {
        PromiseRejectMessage message = queue.takeFirst();
        ScriptState* scriptState = message.m_promise.scriptState();
        if (!scriptState->contextIsValid())
            continue;
        // If execution termination has been triggered, quietly bail out.
        if (v8::V8::IsExecutionTerminating(scriptState->isolate()))
            continue;
        ExecutionContext* executionContext = scriptState->executionContext();
        if (!executionContext)
            continue;

        ScriptState::Scope scope(scriptState);

        ASSERT(!message.m_promise.isEmpty());
        v8::Handle<v8::Value> value = message.m_promise.v8Value();
        ASSERT(!value.IsEmpty() && value->IsPromise());
        if (v8::Handle<v8::Promise>::Cast(value)->HasHandler())
            continue;

        const String errorMessage = "Uncaught (in promise)";
        Vector<ScriptValue> args;
        args.append(ScriptValue(scriptState, v8String(scriptState->isolate(), errorMessage)));
        args.append(message.m_exception);
        RefPtrWillBeRawPtr<ScriptArguments> arguments = ScriptArguments::create(scriptState, args);

        String embedderErrorMessage = message.m_errorMessage;
        if (embedderErrorMessage.isEmpty()) {
            embedderErrorMessage = errorMessage;
        } else {
            if (embedderErrorMessage.startsWith("Uncaught "))
                embedderErrorMessage.insert(" (in promise)", 8);
        }

        RefPtrWillBeRawPtr<ConsoleMessage> consoleMessage = ConsoleMessage::create(JSMessageSource, ErrorMessageLevel, embedderErrorMessage, message.m_resourceName, message.m_lineNumber, message.m_columnNumber);
        consoleMessage->setScriptArguments(arguments);
        consoleMessage->setCallStack(message.m_callStack);
        consoleMessage->setScriptId(message.m_scriptId);
        executionContext->addConsoleMessage(consoleMessage.release());
    }
}

static void promiseRejectHandlerInMainThread(v8::PromiseRejectMessage data)
{
    ASSERT(isMainThread());

    if (data.GetEvent() != v8::kPromiseRejectWithNoHandler)
        return;

    // It's possible that promiseRejectHandlerInMainThread() is invoked while we're initializing a window.
    // In that half-baked situation, we don't have a valid context nor a valid world,
    // so just return immediately.
    if (DOMWrapperWorld::windowIsBeingInitialized())
        return;

    v8::Handle<v8::Promise> promise = data.GetPromise();

    // Bail out if called during context initialization.
    v8::Isolate* isolate = promise->GetIsolate();
    v8::Handle<v8::Context> context = isolate->GetCurrentContext();
    if (context.IsEmpty())
        return;
    v8::Handle<v8::Value> global = V8Window::findInstanceInPrototypeChain(context->Global(), context->GetIsolate());
    if (global.IsEmpty())
        return;

    // There is no entered window during microtask callbacks from V8,
    // thus we call toDOMWindow() instead of enteredDOMWindow().
    LocalDOMWindow* window = toLocalDOMWindow(toDOMWindow(context));
    if (!window || !window->isCurrentlyDisplayedInFrame())
        return;

    v8::Handle<v8::Value> exception = data.GetValue();
    if (V8DOMWrapper::isDOMWrapper(exception)) {
        // Try to get the stack & location from a wrapped exception object (e.g. DOMException).
        ASSERT(exception->IsObject());
        v8::Handle<v8::Object> obj = v8::Handle<v8::Object>::Cast(exception);
        v8::Handle<v8::Value> error = V8HiddenValue::getHiddenValue(isolate, obj, V8HiddenValue::error(isolate));
        if (!error.IsEmpty())
            exception = error;
    }

    int scriptId = 0;
    int lineNumber = 0;
    int columnNumber = 0;
    String resourceName;
    String errorMessage;
    RefPtrWillBeRawPtr<ScriptCallStack> callStack = nullptr;

    v8::Handle<v8::Message> message = v8::Exception::CreateMessage(exception);
    if (!message.IsEmpty()) {
        lineNumber = message->GetLineNumber();
        columnNumber = message->GetStartColumn() + 1;
        resourceName = extractResourceName(message, window->document());
        errorMessage = toCoreString(message->Get());
        callStack = extractCallStack(isolate, message, &scriptId);
    } else if (!exception.IsEmpty() && exception->IsInt32()) {
        // For Smi's the message would be empty.
        errorMessage = "Uncaught " + String::number(exception.As<v8::Integer>()->Value());
    }

    String messageForConsole = extractMessageForConsole(data.GetValue());
    if (!messageForConsole.isEmpty())
        errorMessage = "Uncaught " + messageForConsole;

    ScriptState* scriptState = ScriptState::from(context);
    promiseRejectMessageQueue().append(PromiseRejectMessage(ScriptValue(scriptState, promise), ScriptValue(scriptState, data.GetValue()), errorMessage, resourceName, scriptId, lineNumber, columnNumber, callStack));
}

static void promiseRejectHandlerInWorker(v8::PromiseRejectMessage data)
{
    if (data.GetEvent() != v8::kPromiseRejectWithNoHandler)
        return;

    v8::Handle<v8::Promise> promise = data.GetPromise();

    // Bail out if called during context initialization.
    v8::Isolate* isolate = promise->GetIsolate();
    v8::Handle<v8::Context> context = isolate->GetCurrentContext();
    if (context.IsEmpty())
        return;

    int scriptId = 0;
    int lineNumber = 0;
    int columnNumber = 0;
    String resourceName;
    String errorMessage;

    v8::Handle<v8::Message> message = v8::Exception::CreateMessage(data.GetValue());
    if (!message.IsEmpty()) {
        TOSTRING_VOID(V8StringResource<>, resourceName, message->GetScriptOrigin().ResourceName());
        scriptId = message->GetScriptOrigin().ScriptID()->Value();
        lineNumber = message->GetLineNumber();
        columnNumber = message->GetStartColumn() + 1;
        errorMessage = toCoreString(message->Get());
    }

    ScriptState* scriptState = ScriptState::from(context);
    promiseRejectMessageQueue().append(PromiseRejectMessage(ScriptValue(scriptState, promise), ScriptValue(scriptState, data.GetValue()), errorMessage, resourceName, scriptId, lineNumber, columnNumber, nullptr));
}

static void failedAccessCheckCallbackInMainThread(v8::Local<v8::Object> host, v8::AccessType type, v8::Local<v8::Value> data)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    Frame* target = findFrame(host, data, isolate);
    if (!target)
        return;
    DOMWindow* targetWindow = target->domWindow();

    // FIXME: We should modify V8 to pass in more contextual information (context, property, and object).
    ExceptionState exceptionState(ExceptionState::UnknownContext, 0, 0, isolate->GetCurrentContext()->Global(), isolate);
    exceptionState.throwSecurityError(targetWindow->sanitizedCrossDomainAccessErrorMessage(callingDOMWindow(isolate)), targetWindow->crossDomainAccessErrorMessage(callingDOMWindow(isolate)));
    exceptionState.throwIfNeeded();
}

static bool codeGenerationCheckCallbackInMainThread(v8::Local<v8::Context> context)
{
    if (ExecutionContext* executionContext = toExecutionContext(context)) {
        if (ContentSecurityPolicy* policy = toDocument(executionContext)->contentSecurityPolicy())
            return policy->allowEval(ScriptState::from(context));
    }
    return false;
}

static void idleGCTaskInMainThread(double deadlineSeconds);

static void postIdleGCTaskMainThread()
{
    if (RuntimeEnabledFeatures::v8IdleTasksEnabled()) {
        Scheduler* scheduler = Scheduler::shared();
        if (scheduler)
            scheduler->postIdleTask(FROM_HERE, WTF::bind<double>(idleGCTaskInMainThread));
    }
}

static void idleGCTaskInMainThread(double deadlineSeconds)
{
    ASSERT(isMainThread());
    ASSERT(RuntimeEnabledFeatures::v8IdleTasksEnabled());
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    // FIXME: Change V8's API to take a deadline - http://crbug.com/417668
    double idleTimeInSeconds = deadlineSeconds - Platform::current()->monotonicallyIncreasingTime();
    int idleTimeInMillis = static_cast<int>(idleTimeInSeconds * 1000);
    if (idleTimeInMillis > 0)
        isolate->IdleNotification(idleTimeInMillis);
    // FIXME: only repost if there is more work to do.
    postIdleGCTaskMainThread();
}

static void timerTraceProfilerInMainThread(const char* name, int status)
{
    if (!status) {
        TRACE_EVENT_BEGIN0("v8", name);
    } else {
        TRACE_EVENT_END0("v8", name);
    }
}

static void initializeV8Common(v8::Isolate* isolate)
{
    v8::V8::AddGCPrologueCallback(V8GCController::gcPrologue);
    v8::V8::AddGCEpilogueCallback(V8GCController::gcEpilogue);

    v8::Debug::SetLiveEditEnabled(isolate, false);

    isolate->SetAutorunMicrotasks(false);
}

void V8Initializer::initializeMainThreadIfNeeded()
{
    ASSERT(isMainThread());

    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    gin::IsolateHolder::Initialize(gin::IsolateHolder::kNonStrictMode, v8ArrayBufferAllocator());

    v8::Isolate* isolate = V8PerIsolateData::initialize();

    initializeV8Common(isolate);

    v8::V8::SetFatalErrorHandler(reportFatalErrorInMainThread);
    v8::V8::AddMessageListener(messageHandlerInMainThread);
    v8::V8::SetFailedAccessCheckCallbackFunction(failedAccessCheckCallbackInMainThread);
    v8::V8::SetAllowCodeGenerationFromStringsCallback(codeGenerationCheckCallbackInMainThread);

    postIdleGCTaskMainThread();

    isolate->SetEventLogger(timerTraceProfilerInMainThread);
    isolate->SetPromiseRejectCallback(promiseRejectHandlerInMainThread);

    ScriptProfiler::initialize();
}

static void reportFatalErrorInWorker(const char* location, const char* message)
{
    // FIXME: We temporarily deal with V8 internal error situations such as out-of-memory by crashing the worker.
    CRASH();
}

static void messageHandlerInWorker(v8::Handle<v8::Message> message, v8::Handle<v8::Value> data)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    V8PerIsolateData* perIsolateData = V8PerIsolateData::from(isolate);
    // Exceptions that occur in error handler should be ignored since in that case
    // WorkerGlobalScope::reportException will send the exception to the worker object.
    if (perIsolateData->isReportingException())
        return;
    perIsolateData->setReportingException(true);

    ScriptState* scriptState = ScriptState::current(isolate);
    // During the frame teardown, there may not be a valid context.
    if (ExecutionContext* context = scriptState->executionContext()) {
        String errorMessage = toCoreString(message->Get());
        TOSTRING_VOID(V8StringResource<>, sourceURL, message->GetScriptOrigin().ResourceName());
        int scriptId = 0;
        RefPtrWillBeRawPtr<ScriptCallStack> callStack = extractCallStack(isolate, message, &scriptId);

        RefPtrWillBeRawPtr<ErrorEvent> event = ErrorEvent::create(errorMessage, sourceURL, message->GetLineNumber(), message->GetStartColumn() + 1, &DOMWrapperWorld::current(isolate));
        AccessControlStatus corsStatus = message->IsSharedCrossOrigin() ? SharableCrossOrigin : NotSharableCrossOrigin;

        // If execution termination has been triggered as part of constructing
        // the error event from the v8::Message, quietly leave.
        if (!v8::V8::IsExecutionTerminating(isolate)) {
            V8ErrorHandler::storeExceptionOnErrorEventWrapper(isolate, event.get(), data, scriptState->context()->Global());
            context->reportException(event.release(), scriptId, callStack, corsStatus);
        }
    }

    perIsolateData->setReportingException(false);
}

static const int kWorkerMaxStackSize = 500 * 1024;

// This function uses a local stack variable to determine the isolate's stack limit. AddressSanitizer may
// relocate that local variable to a fake stack, which may lead to problems during JavaScript execution.
// Therefore we disable AddressSanitizer for V8Initializer::initializeWorker().
NO_SANITIZE_ADDRESS
void V8Initializer::initializeWorker(v8::Isolate* isolate)
{
    initializeV8Common(isolate);

    v8::V8::AddMessageListener(messageHandlerInWorker);
    v8::V8::SetFatalErrorHandler(reportFatalErrorInWorker);

    uint32_t here;
    isolate->SetStackLimit(reinterpret_cast<uintptr_t>(&here - kWorkerMaxStackSize / sizeof(uint32_t*)));
    isolate->SetPromiseRejectCallback(promiseRejectHandlerInWorker);
}

} // namespace blink
