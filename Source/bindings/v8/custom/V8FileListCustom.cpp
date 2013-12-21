#include "config.h"

#include "V8FileList.h"

#include "V8File.h"
#include "bindings/v8/V8Binding.h"
#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/frame/Frame.h"

namespace WebCore {
void V8FileList::constructorCustom(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    ExecutionContext* context = getExecutionContext();
    if (context && context->isDocument()) {
        Document* document = toDocument(context);
        if (document->frame()->isNwDisabledChildFrame()) {
            throwTypeError("FileList constructor cannot be called in nwdisabled frame.", args.GetIsolate());
            return;
        }
    }

    RefPtr<FileList> impl = FileList::create();
    v8::Handle<v8::Object> wrapper = args.Holder();

    V8DOMWrapper::associateObjectWithWrapper<V8FileList>(impl.release(), &wrapperTypeInfo, wrapper, args.GetIsolate(), WrapperConfiguration::Dependent);
    args.GetReturnValue().Set(wrapper);
}

} // namespace WebCore
