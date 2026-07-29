#include "Nebula/Mods/SunMod.h"

#include <imgui.h>

#include "Nebula/Config/Config.h"
#include "Nebula/Core/Log.h"
#include "Nebula/Il2Cpp/Il2Cpp.h"

namespace Nebula {

namespace {
constexpr int32_t kForcedSun = 9999;
}

void SunMod::OnLoad() {
    enabled_.store(
        Config::Get().GetBool("sun_mod.enabled", true),
        std::memory_order_relaxed);

    auto& il2cpp = Il2Cpp::Get();
    Il2CppClass* board =
        il2cpp.GetClass("Assembly-CSharp", "", "Board");
    if (board == nullptr) {
        NEBULA_LOGW(
            "SunMod: Assembly-CSharp::Board was not found");
        return;
    }
    instanceField_ = il2cpp.GetField(board, "Instance");
    sunOffset_ = il2cpp.GetFieldOffset(board, "theSun");
    resolved_ = instanceField_ != nullptr && sunOffset_ >= 0;
    if (!resolved_) {
        NEBULA_LOGW(
            "SunMod: Board.Instance or Board.theSun was not found");
        return;
    }
    NEBULA_LOGI(
        "SunMod: Board.theSun resolved at offset 0x%zx; "
        "direct write value=%d",
        static_cast<size_t>(sunOffset_), kForcedSun);
}

void SunMod::OnUpdate() {
    if (!enabled_.load(std::memory_order_relaxed) || !resolved_) {
        return;
    }
    // OnUpdate executes on the overlay render thread, not the bootstrap
    // thread. Attach this thread before calling an IL2CPP Runtime API.
    static thread_local Il2CppThread* attachedThread = nullptr;
    if (attachedThread == nullptr) {
        attachedThread = Il2Cpp::Get().AttachCurrentThread();
        if (attachedThread == nullptr) {
            return;
        }
    }
    void* board = nullptr;
    if (!Il2Cpp::Get().GetStaticFieldValue(
            instanceField_, &board)) {
        return;
    }
    if (board != nullptr) {
        Il2Cpp::Get().WriteField<int32_t>(
            board, sunOffset_, kForcedSun);
    }
}

void SunMod::OnGUI() {
    if (!ImGui::BeginTabItem("Sun")) {
        return;
    }

    bool enabled = enabled_.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Lock sun to 9999", &enabled)) {
        enabled_.store(enabled, std::memory_order_relaxed);
        Config::Get().SetBool("sun_mod.enabled", enabled);
    }

    if (resolved_) {
        ImGui::TextColored(
            ImVec4(0.2F, 1.0F, 0.35F, 1.0F),
            "Board.theSun direct write ready");
    } else {
        ImGui::TextDisabled(
            "Board.Instance/theSun was not resolved");
    }
    ImGui::Text("Forced sun: %d", kForcedSun);
    ImGui::EndTabItem();
}

} // namespace Nebula
