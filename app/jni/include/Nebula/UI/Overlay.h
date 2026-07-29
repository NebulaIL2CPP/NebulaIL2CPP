#pragma once

#include <cstdint>

namespace Nebula {

class Overlay final {
public:
    static Overlay& Get();

    // Hooks EGL presentation and Android input. Safe to call repeatedly.
    bool Install();

    // Shared ImGui frame/content entry points used by the GLES and Vulkan
    // renderers. They must be called on the graphics thread.
    static void PrepareVulkanFrame(uint32_t width, uint32_t height);
    static void DrawContents();

    // Compatibility renderer used by the transparent Java GLSurfaceView.
    // This path owns an independent GLES surface and does not depend on the
    // renderer selected by Unity.
    static void UseExternalSurface();
    static bool IsExternalSurfaceActive();
    static void SetVisible(bool visible);
    static void ToggleVisible();
    static bool IsVisible();
    static void InitializeExternalOpenGL();
    static void RenderExternalOpenGL(uint32_t width, uint32_t height);
    static bool SubmitExternalTouch(
        int32_t action, float x, float y);

private:
    Overlay() = default;
};

} // namespace Nebula
