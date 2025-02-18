/*
 * Copyright (C) 2007-2009 Google Inc. All rights reserved.
 * Copyright (C) 2010 Torch Mobile (Beijing) Co. Ltd. All rights reserved.
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
#include "bindings/core/v8/V8HTMLCanvasElement.h"

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/V8Binding.h"
#include "bindings/core/v8/V8CanvasRenderingContext2D.h"
#include "bindings/core/v8/V8Node.h"
#include "bindings/core/v8/V8WebGLRenderingContext.h"
#include "core/html/HTMLCanvasElement.h"
#include "core/html/canvas/Canvas2DContextAttributes.h"
#include "core/html/canvas/CanvasRenderingContext.h"
#include "core/html/canvas/WebGLContextAttributes.h"
#include "core/inspector/InspectorCanvasInstrumentation.h"
#include "wtf/MathExtras.h"
#include "wtf/text/WTFString.h"

namespace blink {

void V8HTMLCanvasElement::toDataURLMethodCustom(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Handle<v8::Object> holder = info.Holder();
    HTMLCanvasElement* canvas = V8HTMLCanvasElement::toImpl(holder);
    ExceptionState exceptionState(ExceptionState::ExecutionContext, "toDataURL", "HTMLCanvasElement", info.Holder(), info.GetIsolate());

    TOSTRING_VOID(V8StringResource<>, type, info[0]);
    double quality;
    double* qualityPtr = 0;
    if (info.Length() > 1 && info[1]->IsNumber()) {
        quality = info[1]->NumberValue();
        qualityPtr = &quality;
    }

    String result = canvas->toDataURL(type, qualityPtr, exceptionState);
    exceptionState.throwIfNeeded();
    v8SetReturnValueStringOrUndefined(info, result, info.GetIsolate());
}

} // namespace blink
