#include "Nebula/Core/Runtime.h"

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <fstream>
#include <memory>
#include <string>

#include "Nebula/Config/Config.h"
#include "Nebula/Core/Log.h"
#include "Nebula/Il2Cpp/Il2Cpp.h"
#include "Nebula/Mods/ExampleMod.h"
#include "Nebula/Mods/ModManager.h"
#include "Nebula/Mods/SunMod.h"
#include "Nebula/UI/Overlay.h"
#include "Nebula/UI/VulkanOverlay.h"

namespace {

std::string ReadProcessName() {
    std::ifstream input("/proc/self/cmdline", std::ios::binary);
    std::string name;
    std::getline(input, name, '\0');
    const size_t subprocess = name.find(':');
    if (subprocess != std::string::npos) {
        name.resize(subprocess);
    }
    return name;
}

std::string DefaultConfigPath() {
    const std::string packageName = ReadProcessName();
    if (packageName.empty()) {
        return "/data/local/tmp/NebulaIL2CPP/config.json";
    }
    return "/data/data/" + packageName +
           "/files/Nebula/config.json";
}

} // namespace

namespace Nebula {

Runtime& Runtime::Get() {
    static Runtime instance;
    return instance;
}

void Runtime::Start() {
    static std::atomic<bool> started{false};
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) {
        return;
    }

    pthread_t worker{};
    const int status =
        pthread_create(&worker, nullptr, &Runtime::Bootstrap, nullptr);
    if (status != 0) {
        started.store(false);
        NEBULA_LOGE("Could not create bootstrap thread: %d", status);
        return;
    }
    pthread_detach(worker);
}

void* Runtime::Bootstrap(void*) {
    LogBuildInformation();

    void* il2cppHandle = nullptr;
    while (il2cppHandle == nullptr) {
        // Install the diagnostic/sample UI independently from IL2CPP. This is
        // important for injected linker namespaces where RTLD_NOLOAD may not
        // be able to see libil2cpp.so even though EGL is already rendering.
        Overlay::Get().Install();
        VulkanOverlay::Get().Install();
        il2cppHandle =
            dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
        if (il2cppHandle == nullptr) {
            usleep(250 * 1000);
        }
    }

    if (!Il2Cpp::Get().Initialize(il2cppHandle)) {
        NEBULA_LOGE("Bootstrap stopped: IL2CPP initialization failed");
        return nullptr;
    }
    if (!Il2Cpp::Get().ObserveRuntimeInitialization()) {
        NEBULA_LOGE(
            "Bootstrap stopped: could not observe il2cpp_init");
        return nullptr;
    }

    // Do not poll il2cpp_domain_get here. Some Unity versions dereference
    // internal VM state inside that API before il2cpp_init has completed.
    while (!Il2Cpp::Get().IsRuntimeInitialized()) {
        Overlay::Get().Install();
        VulkanOverlay::Get().Install();
        usleep(50 * 1000);
    }

    Il2CppThread* attachedThread =
        Il2Cpp::Get().AttachCurrentThread();
    if (attachedThread == nullptr) {
        NEBULA_LOGE(
            "Bootstrap stopped: thread attach failed after il2cpp_init");
        return nullptr;
    }
    NEBULA_LOGI("Bootstrap thread attached to IL2CPP");

    Config::Get().SetPath(DefaultConfigPath());
    Config::Get().Load();

    ModManager::Get().Register(std::make_unique<ExampleMod>());
    ModManager::Get().Register(std::make_unique<SunMod>());
    ModManager::Get().LoadAll();

    bool overlayReady = false;
    for (int attempt = 0; attempt < 20 && !overlayReady; ++attempt) {
        overlayReady = Overlay::Get().Install();
        overlayReady = VulkanOverlay::Get().Install() || overlayReady;
        usleep(250 * 1000);
    }
    if (!overlayReady) {
        NEBULA_LOGW("No render backend hook installed yet; continuing bootstrap");
    }

    NEBULA_LOGI("NebulaIL2CPP bootstrap complete");
    return nullptr;
}

} // namespace Nebula
