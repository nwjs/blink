#include "config.h"

#include "V8FileList.h"

#include "V8File.h"
#include "bindings/v8/V8Binding.h"
#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/frame/LocalFrame.h"

namespace blink {
void V8FileList::constructorCustom(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    ExecutionContext* context = currentExecutionContext(args.GetIsolate());
    if (context && context->isDocument()) {
        Document* document = toDocument(context);
        if (document->frame()->isNwDisabledChildFrame()) {
            throwTypeError("FileList constructor cannot be called in nwdisabled frame.", args.GetIsolate());
            return;
        }
    }

    RefPtr<FileList> impl = FileList::create();
    v8SetReturnValue(args, impl.release());
}

} // namespace blink
