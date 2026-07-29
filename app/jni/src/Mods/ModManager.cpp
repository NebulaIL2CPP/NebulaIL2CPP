#include "Nebula/Mods/ModManager.h"

#include "Nebula/Core/Log.h"

namespace Nebula {

ModManager& ModManager::Get() {
    static ModManager instance;
    return instance;
}

void ModManager::Register(std::unique_ptr<IMod> mod) {
    if (mod == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (loaded_) {
        NEBULA_LOGW("Ignoring mod registered after LoadAll");
        return;
    }
    mods_.push_back(std::move(mod));
}

void ModManager::LoadAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loaded_) {
        return;
    }
    for (const auto& mod : mods_) {
        mod->OnLoad();
    }
    loaded_ = true;
    NEBULA_LOGI("Loaded %zu mod(s)", mods_.size());
}

void ModManager::UpdateAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_) {
        return;
    }
    for (const auto& mod : mods_) {
        mod->OnUpdate();
    }
}

void ModManager::DrawAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_) {
        return;
    }
    for (const auto& mod : mods_) {
        mod->OnGUI();
    }
}

} // namespace Nebula
