#include <sys/types.h>
#include <ruby.h>
#include "Callback.h"
#include "Types.h"
#include "rbffi.h"



static void callback_mark(CallbackInfo *);
static void callback_free(CallbackInfo *);

static VALUE classCallback = Qnil;
static VALUE classNativeCallback = Qnil;

//static VALUE classCallbackImpl = Qnil;
VALUE rb_FFI_Callback_class = Qnil;

static VALUE
callback_new(VALUE self, VALUE rbReturnType, VALUE rbParamTypes)
{
    CallbackInfo *cbInfo = ALLOC(CallbackInfo);
    int paramCount = RARRAY_LEN(rbParamTypes);
    ffi_status status;
    int i;

    cbInfo->parameterTypes = calloc(paramCount, sizeof(NativeType));
    cbInfo->ffiParameterTypes = calloc(paramCount, sizeof(ffi_type *));
    if (cbInfo->parameterTypes == NULL || cbInfo->ffiParameterTypes == NULL) {
        callback_free(cbInfo);
        rb_raise(rb_eNoMemError, "Failed to allocate native memory");
    }
    for (i = 0; i < paramCount; ++i) {
        cbInfo->parameterTypes[i] = FIX2INT(rb_ary_entry(rbParamTypes, i));
        cbInfo->ffiParameterTypes[i] = rb_FFI_NativeTypeToFFI(cbInfo->parameterTypes[i]);
        if (cbInfo->ffiParameterTypes[i] == NULL) {
            callback_free(cbInfo);
            rb_raise(rb_eArgError, "Unknown argument type: %#x", cbInfo->parameterTypes[i]);
        }
    }
    cbInfo->returnType = FIX2INT(rbReturnType);
    cbInfo->ffiReturnType = rb_FFI_NativeTypeToFFI(cbInfo->returnType);
    if (cbInfo->ffiReturnType == NULL) {
        callback_free(cbInfo);
        rb_raise(rb_eArgError, "Unknown return type: %#x", cbInfo->returnType);
    }
#ifdef _WIN32
    cbInfo->abi = (flags & STDCALL) ? FFI_STDCALL : FFI_DEFAULT_ABI;
#else
    cbInfo->abi = FFI_DEFAULT_ABI;
#endif
    status = ffi_prep_cif(&cbInfo->ffi_cif, cbInfo->abi, cbInfo->parameterCount, 
            cbInfo->ffiReturnType, cbInfo->ffiParameterTypes);
    switch (status) {
        case FFI_BAD_ABI:
            callback_free(cbInfo);
            rb_raise(rb_eArgError, "Invalid ABI specified");
        case FFI_BAD_TYPEDEF:
            callback_free(cbInfo);
            rb_raise(rb_eArgError, "Invalid argument type specified");
        case FFI_OK:
            break;
        default:
            callback_free(cbInfo);
            rb_raise(rb_eArgError, "Unknown FFI error");
    }
    return Data_Wrap_Struct(classCallback, callback_mark, callback_free, cbInfo);
}

static void
callback_mark(CallbackInfo* cbinfo)
{
}

static void
callback_free(CallbackInfo* cbInfo)
{
    if (cbInfo != NULL) {
        if (cbInfo->parameterTypes != NULL) {
            free(cbInfo->parameterTypes);
        }
        if (cbInfo->ffiParameterTypes != NULL) {
            free(cbInfo->ffiParameterTypes);
        }
        xfree(cbInfo);
    }
}

static void
native_callback_free(NativeCallback* cb)
{
    if (cb != NULL) {
        if (cb->ffi_closure != NULL) {
            ffi_closure_free(cb->ffi_closure);
        }
    }
}

static void
native_callback_mark(NativeCallback* cb)
{
    rb_gc_mark(cb->rbCallbackInfo);
    rb_gc_mark(cb->rbProc);
}

static void
native_callback_invoke(ffi_cif* cif, void* retval, void** parameters, void* user_data)
{
    //NativeCallback* cb = (NativeCallback *) user_data;
    //rb_gc_mark(cb->rbProc);
}

VALUE
rb_FFI_NativeCallback_new(VALUE rbCallbackInfo, VALUE rbProc)
{
    NativeCallback* closure = NULL;
    CallbackInfo* cbInfo = (CallbackInfo *) DATA_PTR(rbCallbackInfo);
    ffi_status status;
    
    closure = ALLOC(NativeCallback);
    closure->ffi_closure = ffi_closure_alloc(sizeof(*closure->ffi_closure), &closure->code);
    if (closure->ffi_closure == NULL) {
        xfree(closure);
        rb_raise(rb_eNoMemError, "Failed to allocate FFI native closure");
    }
    closure->cbInfo = cbInfo;
    closure->rbProc = rbProc;
    closure->rbCallbackInfo = rbCallbackInfo;
    status = ffi_prep_closure_loc(closure->ffi_closure, &cbInfo->ffi_cif,
            native_callback_invoke, closure, closure->code);
    if (status != FFI_OK) {
        ffi_closure_free(closure->ffi_closure);
        xfree(closure);
        rb_raise(rb_eArgError, "ffi_prep_closure_loc failed");
    }
    return Data_Wrap_Struct(classNativeCallback, native_callback_mark, native_callback_free, closure);
}

void
rb_FFI_Callback_Init()
{
    VALUE moduleFFI = rb_define_module("FFI");
    rb_FFI_Callback_class = classCallback = rb_define_class_under(moduleFFI, "Callback", rb_cObject);
    rb_define_singleton_method(classCallback, "new", callback_new, 2);
    classNativeCallback = rb_define_class_under(moduleFFI, "NativeCallback", rb_cObject);
}
