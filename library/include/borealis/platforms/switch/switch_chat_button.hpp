#pragma once

#ifdef __SWITCH2__
#include <switch.h>

namespace brls
{

// AcquireCButtonEventHandle uses the same PID/Aruid/copy-handle ABI as Home.
// The current libounce SDK does not export this Switch 2 system-button wrapper.
inline Result acquireSwitchChatButtonEvent(::Event* event, bool autoclear)
{
    const u64 appletId  = appletGetAppletResourceUserId();
    Handle handle       = INVALID_HANDLE;
    const Result result = serviceDispatchIn(hidsysGetServiceSession(), 171, appletId,
        .in_send_pid      = true,
        .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
        .out_handles      = &handle);
    if (R_SUCCEEDED(result))
        eventLoadRemote(event, handle, autoclear);
    return result;
}

} // namespace brls
#endif
