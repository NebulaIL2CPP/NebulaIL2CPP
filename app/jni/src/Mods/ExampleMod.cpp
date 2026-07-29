#include "Nebula/Mods/ExampleMod.h"

#include <imgui.h>

#include "Nebula/Config/Config.h"
#include "Nebula/Core/Log.h"
#include "Nebula/Hook/Hook.h"
#include "Nebula/Il2Cpp/Il2Cpp.h"

namespace Nebula {

void ExampleMod::OnLoad() {
    auto& config = Config::Get();
    infiniteHealth_.store(
        config.GetBool("example.infinite_health", false),
        std::memory_order_relaxed);
    damageMultiplier_ =
        config.GetInt("example.damage_multiplier", 10);

    auto& il2cpp = Il2Cpp::Get();
    Il2CppClass* player =
        il2cpp.GetClass("Assembly-CSharp", "", "Player");
    healthOffset_ = il2cpp.GetFieldOffset(player, "health");

    const uintptr_t target =
        il2cpp.GetMethodAddress(player == nullptr
                                    ? nullptr
                                    : il2cpp.GetMethod(
                                          player, "TakeDamage", 1));
    if (target == 0) {
        NEBULA_LOGW(
            "ExampleMod: Player.TakeDamage(int) not found; "
            "edit assembly/namespace/type names for the target game");
        return;
    }

    Hook::HookFunction(
        target,
        reinterpret_cast<void*>(&ExampleMod::TakeDamageHook),
        reinterpret_cast<void**>(&originalTakeDamage_));
}

void ExampleMod::OnUpdate() {
    if (!infiniteHealth_.load(std::memory_order_relaxed) ||
        healthOffset_ < 0) {
        return;
    }
    void* player = lastPlayer_.load(std::memory_order_relaxed);
    if (player != nullptr) {
        Il2Cpp::Get().WriteField<int32_t>(
            player, healthOffset_, 999999);
    }
}

void ExampleMod::OnGUI() {
    if (ImGui::BeginTabItem("Player")) {
        bool enabled =
            infiniteHealth_.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Infinite HP", &enabled)) {
            infiniteHealth_.store(enabled, std::memory_order_relaxed);
            Config::Get().SetBool(
                "example.infinite_health", enabled);
        }
        if (healthOffset_ < 0) {
            ImGui::TextDisabled("Player.health not resolved");
        } else {
            ImGui::Text("health offset: 0x%zx",
                        static_cast<size_t>(healthOffset_));
        }
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Weapon")) {
        if (ImGui::SliderInt(
                "Damage multiplier", &damageMultiplier_, 1, 20)) {
            Config::Get().SetInt(
                "example.damage_multiplier", damageMultiplier_);
        }
        if (ImGui::Button("Save config")) {
            Config::Get().Save();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Example: x%d", damageMultiplier_);
        ImGui::EndTabItem();
    }
}

void ExampleMod::TakeDamageHook(
    void* player, int32_t damage, const MethodInfo* method) {
    lastPlayer_.store(player, std::memory_order_relaxed);
    if (infiniteHealth_.load(std::memory_order_relaxed)) {
        damage = 0;
        if (healthOffset_ >= 0 && player != nullptr) {
            Il2Cpp::Get().WriteField<int32_t>(
                player, healthOffset_, 999999);
        }
    }
    if (originalTakeDamage_ != nullptr) {
        originalTakeDamage_(player, damage, method);
    }
}

} // namespace Nebula
