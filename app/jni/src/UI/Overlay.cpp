#include "Nebula/UI/Overlay.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/input.h>
#include <android/looper.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <dlfcn.h>
#include <deque>
#include <mutex>

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include "Nebula/Config/Config.h"
#include "Nebula/Core/Log.h"
#include "Nebula/Hook/Hook.h"
#include "Nebula/Mods/ModManager.h"
#include "Nebula/UI/SampleMenu.h"

namespace {

using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
using EglSwapBuffersWithDamageFn =
    EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint*, EGLint);
using FinishInputEventFn =
    void (*)(AInputQueue*, AInputEvent*, int);

EglSwapBuffersFn g_originalSwapBuffers = nullptr;
EglSwapBuffersWithDamageFn g_originalSwapBuffersWithDamageKhr = nullptr;
EglSwapBuffersWithDamageFn g_originalSwapBuffersWithDamageExt = nullptr;
FinishInputEventFn g_originalFinishInputEvent = nullptr;
std::once_flag g_imguiOnce;
std::atomic<bool> g_firstRenderedFrame{false};
std::atomic<bool> g_firstSwapEntered{false};
std::atomic<bool> g_firstDamageSwapEntered{false};
struct TouchEvent {
    float x;
    float y;
    bool down;
};
std::mutex g_touchMutex;
std::deque<TouchEvent> g_touchEvents;
std::atomic<bool> g_externalSurface{false};
std::atomic<bool> g_visible{true};
std::atomic<float> g_menuMinX{0.0F};
std::atomic<float> g_menuMinY{0.0F};
std::atomic<float> g_menuMaxX{0.0F};
std::atomic<float> g_menuMaxY{0.0F};

void InitializeImGuiContext() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImGui::StyleColorsDark();
    // The default ImGui font is designed for desktop pixel densities. Use
    // Android's Roboto at a larger size so controls remain readable on phones.
    if (io.Fonts->AddFontFromFileTTF(
            "/system/fonts/Roboto-Regular.ttf", 30.0F) == nullptr) {
        io.FontGlobalScale = 1.8F;
    }
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0F;
    style.FrameRounding = 7.0F;
    style.GrabRounding = 7.0F;
    style.ScaleAllSizes(1.4F);

}

void InitializeImGui() {
    InitializeImGuiContext();
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        NEBULA_LOGE("ImGui OpenGL ES backend initialization failed");
    }
    NEBULA_LOGI("Dear ImGui overlay initialized");
}

void ApplyPendingTouch() {
    std::deque<TouchEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_touchMutex);
        events.swap(g_touchEvents);
    }
    ImGuiIO& io = ImGui::GetIO();
    for (const TouchEvent& event : events) {
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(event.x, event.y);
        io.AddMouseButtonEvent(0, event.down);
    }
}

void QueueTouch(float x, float y, bool down) {
    std::lock_guard<std::mutex> lock(g_touchMutex);
    if (g_touchEvents.size() >= 64) {
        g_touchEvents.pop_front();
    }
    g_touchEvents.push_back({x, y, down});
}

void DrawMenu() {
    if (!g_visible.load(std::memory_order_acquire)) {
        g_menuMinX.store(0.0F, std::memory_order_relaxed);
        g_menuMinY.store(0.0F, std::memory_order_relaxed);
        g_menuMaxX.store(0.0F, std::memory_order_relaxed);
        g_menuMaxY.store(0.0F, std::memory_order_relaxed);
        return;
    }
    static bool firstFrame = true;
    static bool showDearImGuiDemo = false;
    if (firstFrame) {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(
            ImVec2(display.x * 0.08F, display.y * 0.10F),
            ImGuiCond_Once);
        ImGui::SetNextWindowSize(
            ImVec2(display.x * 0.38F, display.y * 0.62F),
            ImGuiCond_Once);
        firstFrame = false;
    }

    bool windowOpen = g_visible.load(std::memory_order_acquire);
    if (ImGui::Begin(
            "Nebula Menu", &windowOpen,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar)) {
        const ImVec2 position = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        g_menuMinX.store(position.x, std::memory_order_relaxed);
        g_menuMinY.store(position.y, std::memory_order_relaxed);
        g_menuMaxX.store(position.x + size.x, std::memory_order_relaxed);
        g_menuMaxY.store(position.y + size.y, std::memory_order_relaxed);
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Nebula")) {
                if (ImGui::MenuItem("Save config")) {
                    Nebula::Config::Get().Save();
                }
                ImGui::MenuItem(
                    "Dear ImGui demo", nullptr, &showDearImGuiDemo);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Theme")) {
                if (ImGui::MenuItem("Dark")) {
                    ImGui::StyleColorsDark();
                }
                if (ImGui::MenuItem("Light")) {
                    ImGui::StyleColorsLight();
                }
                if (ImGui::MenuItem("Classic")) {
                    ImGui::StyleColorsClassic();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::TextDisabled("Unity IL2CPP / ARM64");
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(0.2F, 1.0F, 0.35F, 1.0F), "UI ONLINE");
        ImGui::Separator();
        if (ImGui::BeginTabBar("NebulaTabs")) {
            Nebula::SampleMenu::Draw();
            Nebula::ModManager::Get().DrawAll();
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (!windowOpen) {
        g_visible.store(false, std::memory_order_release);
    }

    if (showDearImGuiDemo) {
        ImGui::ShowDemoWindow(&showDearImGuiDemo);
    }
}

void RenderFrame(uint32_t width, uint32_t height) {
    std::call_once(g_imguiOnce, InitializeImGui);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize =
        ImVec2(static_cast<float>(width), static_cast<float>(height));

    static auto previous = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = std::chrono::duration<float>(now - previous).count();
    if (io.DeltaTime <= 0.0F || io.DeltaTime > 1.0F) {
        io.DeltaTime = 1.0F / 60.0F;
    }
    previous = now;

    ApplyPendingTouch();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    Nebula::ModManager::Get().UpdateAll();
    DrawMenu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (!g_firstRenderedFrame.exchange(true)) {
        NEBULA_LOGI(
            "First ImGui frame rendered (%ux%u)", width, height);
    }
}

void RenderOverlay(EGLDisplay display, EGLSurface surface) {
    EGLint width = 0;
    EGLint height = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &height);
    RenderFrame(
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));
}

EGLBoolean SwapBuffersHook(EGLDisplay display, EGLSurface surface) {
    if (g_externalSurface.load(std::memory_order_acquire)) {
        return g_originalSwapBuffers != nullptr
                   ? g_originalSwapBuffers(display, surface)
                   : EGL_FALSE;
    }
    if (!g_firstSwapEntered.exchange(true)) {
        NEBULA_LOGI(
            "eglSwapBuffers hook entered (display=%p, surface=%p)",
            display, surface);
    }
    RenderOverlay(display, surface);
    return g_originalSwapBuffers != nullptr
               ? g_originalSwapBuffers(display, surface)
               : EGL_FALSE;
}

EGLBoolean SwapBuffersWithDamageKhrHook(
    EGLDisplay display, EGLSurface surface,
    EGLint* rectangles, EGLint rectangleCount) {
    if (g_externalSurface.load(std::memory_order_acquire)) {
        return g_originalSwapBuffersWithDamageKhr != nullptr
                   ? g_originalSwapBuffersWithDamageKhr(
                         display, surface, rectangles, rectangleCount)
                   : EGL_FALSE;
    }
    if (!g_firstDamageSwapEntered.exchange(true)) {
        NEBULA_LOGI(
            "eglSwapBuffersWithDamageKHR hook entered "
            "(display=%p, surface=%p, rects=%d)",
            display, surface, rectangleCount);
    }
    RenderOverlay(display, surface);
    return g_originalSwapBuffersWithDamageKhr != nullptr
               ? g_originalSwapBuffersWithDamageKhr(
                     display, surface, rectangles, rectangleCount)
               : EGL_FALSE;
}

EGLBoolean SwapBuffersWithDamageExtHook(
    EGLDisplay display, EGLSurface surface,
    EGLint* rectangles, EGLint rectangleCount) {
    if (g_externalSurface.load(std::memory_order_acquire)) {
        return g_originalSwapBuffersWithDamageExt != nullptr
                   ? g_originalSwapBuffersWithDamageExt(
                         display, surface, rectangles, rectangleCount)
                   : EGL_FALSE;
    }
    if (!g_firstDamageSwapEntered.exchange(true)) {
        NEBULA_LOGI(
            "eglSwapBuffersWithDamageEXT hook entered "
            "(display=%p, surface=%p, rects=%d)",
            display, surface, rectangleCount);
    }
    RenderOverlay(display, surface);
    return g_originalSwapBuffersWithDamageExt != nullptr
               ? g_originalSwapBuffersWithDamageExt(
                     display, surface, rectangles, rectangleCount)
               : EGL_FALSE;
}

void FinishInputEventHook(
    AInputQueue* queue, AInputEvent* event, int handled) {
    if (event != nullptr &&
        AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY &&
        (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN ||
         AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP)) {
        const int32_t keyCode = AKeyEvent_getKeyCode(event);
        if (keyCode == AKEYCODE_VOLUME_UP) {
            Nebula::Overlay::SetVisible(true);
        } else if (keyCode == AKEYCODE_VOLUME_DOWN) {
            Nebula::Overlay::SetVisible(false);
        }
    }
    if (event != nullptr &&
        AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(event);
        const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
        const size_t pointerIndex = static_cast<size_t>(
            (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        const size_t count = AMotionEvent_getPointerCount(event);
        const size_t safeIndex = pointerIndex < count ? pointerIndex : 0;

        const float x = AMotionEvent_getX(event, safeIndex);
        const float y = AMotionEvent_getY(event, safeIndex);
        bool down = true;
        if (masked == AMOTION_EVENT_ACTION_DOWN ||
            masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
            down = true;
        } else if (masked == AMOTION_EVENT_ACTION_UP ||
                   masked == AMOTION_EVENT_ACTION_POINTER_UP ||
                   masked == AMOTION_EVENT_ACTION_CANCEL) {
            down = false;
        }
        QueueTouch(x, y, down);
    }

    if (g_originalFinishInputEvent != nullptr) {
        g_originalFinishInputEvent(queue, event, handled);
    }
}

} // namespace

namespace Nebula {

Overlay& Overlay::Get() {
    static Overlay instance;
    return instance;
}

void Overlay::PrepareVulkanFrame(
    uint32_t width, uint32_t height) {
    std::call_once(g_imguiOnce, InitializeImGuiContext);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<float>(width), static_cast<float>(height));
    static auto previous = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = std::chrono::duration<float>(
        now - previous).count();
    if (io.DeltaTime <= 0.0F || io.DeltaTime > 1.0F) {
        io.DeltaTime = 1.0F / 60.0F;
    }
    previous = now;

    ApplyPendingTouch();
    ImGui::NewFrame();
    ModManager::Get().UpdateAll();
}

void Overlay::DrawContents() {
    DrawMenu();
}

void Overlay::UseExternalSurface() {
    g_externalSurface.store(true, std::memory_order_release);
    NEBULA_LOGI("Compatibility GLSurfaceView renderer selected");
}

bool Overlay::IsExternalSurfaceActive() {
    return g_externalSurface.load(std::memory_order_acquire);
}

void Overlay::SetVisible(bool visible) {
    g_visible.store(visible, std::memory_order_release);
}

void Overlay::ToggleVisible() {
    bool current = g_visible.load(std::memory_order_acquire);
    while (!g_visible.compare_exchange_weak(
        current, !current, std::memory_order_acq_rel,
        std::memory_order_acquire)) {
    }
}

bool Overlay::IsVisible() {
    return g_visible.load(std::memory_order_acquire);
}

void Overlay::InitializeExternalOpenGL() {
    std::call_once(g_imguiOnce, InitializeImGui);
}

void Overlay::RenderExternalOpenGL(
    uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    glViewport(
        0, 0, static_cast<GLsizei>(width),
        static_cast<GLsizei>(height));
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    RenderFrame(width, height);
}

bool Overlay::SubmitExternalTouch(
    int32_t action, float x, float y) {
    if (!g_visible.load(std::memory_order_acquire)) {
        return false;
    }
    if (ImGui::GetCurrentContext() == nullptr) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const bool insideMenu =
        x >= g_menuMinX.load(std::memory_order_relaxed) &&
        x <= g_menuMaxX.load(std::memory_order_relaxed) &&
        y >= g_menuMinY.load(std::memory_order_relaxed) &&
        y <= g_menuMaxY.load(std::memory_order_relaxed);
    if (action == AMOTION_EVENT_ACTION_DOWN &&
        !insideMenu && !io.WantCaptureMouse) {
        return false;
    }

    bool down = true;
    if (action == AMOTION_EVENT_ACTION_DOWN ||
        action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        down = true;
    } else if (action == AMOTION_EVENT_ACTION_UP ||
               action == AMOTION_EVENT_ACTION_POINTER_UP ||
               action == AMOTION_EVENT_ACTION_CANCEL) {
        down = false;
    }
    QueueTouch(x, y, down);
    return insideMenu || io.WantCaptureMouse;
}

bool Overlay::Install() {
    static std::mutex installMutex;
    static bool renderInstalled = false;
    static bool inputAttempted = false;
    static bool waitingLogged = false;
    std::lock_guard<std::mutex> lock(installMutex);
    if (renderInstalled) {
        return true;
    }

    void* swap = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
    void* swapKhr = reinterpret_cast<void*>(
        eglGetProcAddress("eglSwapBuffersWithDamageKHR"));
    void* swapExt = reinterpret_cast<void*>(
        eglGetProcAddress("eglSwapBuffersWithDamageEXT"));

    bool anyRenderHook = false;
    if (swap != nullptr) {
        anyRenderHook = Hook::HookFunction(
            swap,
            reinterpret_cast<void*>(&SwapBuffersHook),
            reinterpret_cast<void**>(&g_originalSwapBuffers)) ||
            anyRenderHook;
    }
    if (swapKhr != nullptr && swapKhr != swap) {
        anyRenderHook = Hook::HookFunction(
            swapKhr,
            reinterpret_cast<void*>(&SwapBuffersWithDamageKhrHook),
            reinterpret_cast<void**>(
                &g_originalSwapBuffersWithDamageKhr)) ||
            anyRenderHook;
    }
    if (swapExt != nullptr && swapExt != swap &&
        swapExt != swapKhr) {
        anyRenderHook = Hook::HookFunction(
            swapExt,
            reinterpret_cast<void*>(&SwapBuffersWithDamageExtHook),
            reinterpret_cast<void**>(
                &g_originalSwapBuffersWithDamageExt)) ||
            anyRenderHook;
    }

    if (!anyRenderHook) {
        if (!waitingLogged) {
            NEBULA_LOGW(
                "EGL swap symbols are not ready; overlay will retry");
            waitingLogged = true;
        }
        return false;
    }

    if (!inputAttempted) {
        inputAttempted = true;
        void* finish =
            dlsym(RTLD_DEFAULT, "AInputQueue_finishEvent");
        if (finish == nullptr ||
            !Hook::HookFunction(
                finish,
                reinterpret_cast<void*>(&FinishInputEventHook),
                reinterpret_cast<void**>(
                    &g_originalFinishInputEvent))) {
            NEBULA_LOGW(
                "Touch hook unavailable; rendering remains enabled");
        }
    }

    renderInstalled = true;
    NEBULA_LOGI(
        "Overlay render hook installed "
        "(eglSwapBuffers=%p, KHR=%p, EXT=%p)",
        swap, swapKhr, swapExt);
    return true;
}

} // namespace Nebula
