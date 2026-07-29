#include "Nebula/Hook/Hook.h"

#include <dobby.h>

#include "Nebula/Core/Log.h"

namespace Nebula::Hook {

bool HookFunction(uintptr_t address, void* replace, void** original) {
    if (address == 0 || replace == nullptr || original == nullptr) {
        NEBULA_LOGE("HookFunction rejected invalid arguments");
        return false;
    }

    const int status =
        DobbyHook(reinterpret_cast<void*>(address), replace, original);
    if (status != 0) {
        NEBULA_LOGE("DobbyHook failed for %p (status=%d)",
                    reinterpret_cast<void*>(address), status);
        return false;
    }

    NEBULA_LOGI("Hook installed at %p", reinterpret_cast<void*>(address));
    return true;
}

} // namespace Nebula::Hook
