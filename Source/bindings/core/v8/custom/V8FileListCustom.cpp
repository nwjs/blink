#include "config.h"

#include "V8FileList.h"

#include "V8File.h"
#include "bindings/v8/V8Binding.h"
#include "core/dom/Document.h"
#include "core/dom/ScriptExecutionContext.h"
#include "core/page/Frame.h"

namespace blink {
void V8FileList::constructorCustom(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    ScriptExecutionContext* context = getScriptExecutionContext();
    if (context && context->isDocument()) {
        Document* document = toDocument(context);
        if (document->frame()->isNwDisabledChildFrame()) {
            throwTypeError("FileList constructor cannot be called in nwdisabled frame.", args.GetIsolate());
            return;
        }
    }

    RefPtr<FileList> impl = FileList::create();
    v8::Handle<v8::Object> wrapper = args.Holder();

    V8DOMWrapper::associateObjectWithWrapper(impl.release(), &V8FileList::info, wrapper, args.GetIsolate(), WrapperConfiguration::Dependent);
    args.GetReturnValue().Set(wrapper);
}

} // namespace blink
