#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Nebula/Mods/IMod.h"

struct MethodInfo;

namespace Nebula {

class ExampleMod final : public IMod {
public:
    void OnLoad() override;
    void OnUpdate() override;
    void OnGUI() override;

private:
    using TakeDamageFn = void (*)(
        void* player, int32_t damage, const MethodInfo* method);

    static void TakeDamageHook(
        void* player, int32_t damage, const MethodInfo* method);

    static inline TakeDamageFn originalTakeDamage_ = nullptr;
    static inline std::atomic<void*> lastPlayer_ = nullptr;
    static inline std::atomic<bool> infiniteHealth_ = false;
    static inline ptrdiff_t healthOffset_ = -1;

    int damageMultiplier_ = 10;
};

} // namespace Nebula
