#include "config.h"

#include "V8File.h"
#include "bindings/v8/V8Binding.h"
#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/frame/Frame.h"

namespace WebCore {
void V8File::constructorCustom(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    ExecutionContext* context = getExecutionContext();
    if (context && context->isDocument()) {
        Document* document = toDocument(context);
        if (document->frame()->isNwDisabledChildFrame()) {
            throwTypeError("File constructor cannot be called in nwdisabled frame.", args.GetIsolate());
            return;
        }
    }
    if (args.Length() < 2) {
        throwTypeError(ExceptionMessages::failedToConstruct("File", ExceptionMessages::notEnoughArguments(1, args.Length())), args.GetIsolate());
        return;
    }
    V8TRYCATCH_FOR_V8STRINGRESOURCE_VOID(V8StringResource<>, path, args[0]);
    V8TRYCATCH_FOR_V8STRINGRESOURCE_VOID(V8StringResource<>, name, args[1]);

    RefPtr<File> impl = File::create(path, name);
    v8::Handle<v8::Object> wrapper = args.Holder();

    V8DOMWrapper::associateObjectWithWrapper<V8File>(impl.release(), &wrapperTypeInfo, wrapper, args.GetIsolate(), WrapperConfiguration::Dependent);
    args.GetReturnValue().Set(wrapper);
}

} // namespace WebCore
