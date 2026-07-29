#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Nebula/Mods/IMod.h"

struct FieldInfo;

namespace Nebula {

// Resolves Board.Instance every frame and writes Board.theSun directly.
class SunMod final : public IMod {
public:
    void OnLoad() override;
    void OnUpdate() override;
    void OnGUI() override;

private:
    static inline std::atomic<bool> enabled_{true};
    static inline bool resolved_ = false;
    static inline FieldInfo* instanceField_ = nullptr;
    static inline ptrdiff_t sunOffset_ = -1;
};

} // namespace Nebula
