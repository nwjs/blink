/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SpeechGrammarList_h
#define SpeechGrammarList_h

#include "bindings/core/v8/ScriptWrappable.h"
#include "modules/speech/SpeechGrammar.h"
#include "platform/heap/Handle.h"

namespace blink {

class ExecutionContext;

class SpeechGrammarList final : public GarbageCollected<SpeechGrammarList>, public ScriptWrappable {
    DEFINE_WRAPPERTYPEINFO();
public:
    static SpeechGrammarList* create();

    unsigned length() const { return m_grammars.size(); }
    SpeechGrammar* item(unsigned) const;

    void addFromUri(ExecutionContext*, const String& src, double weight = 1.0);
    void addFromString(const String&, double weight = 1.0);

    void trace(Visitor*);

private:
    SpeechGrammarList();

    HeapVector<Member<SpeechGrammar> > m_grammars;
};

} // namespace blink

#endif // SpeechGrammarList_h
