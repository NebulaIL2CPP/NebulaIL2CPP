#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "Nebula/Mods/IMod.h"

namespace Nebula {

class ModManager final {
public:
    static ModManager& Get();

    void Register(std::unique_ptr<IMod> mod);
    void LoadAll();
    void UpdateAll();
    void DrawAll();

private:
    ModManager() = default;

    std::mutex mutex_;
    std::vector<std::unique_ptr<IMod>> mods_;
    bool loaded_ = false;
};

} // namespace Nebula
