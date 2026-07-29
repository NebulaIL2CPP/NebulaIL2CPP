#pragma once

#include <cstdint>

namespace Nebula::Hook {

// Installs an ARM64 inline hook. `original` receives a callable trampoline.
bool HookFunction(uintptr_t address, void* replace, void** original);

// Convenience overload for a raw function pointer.
inline bool HookFunction(void* address, void* replace, void** original) {
    return HookFunction(reinterpret_cast<uintptr_t>(address), replace, original);
}

} // namespace Nebula::Hook
