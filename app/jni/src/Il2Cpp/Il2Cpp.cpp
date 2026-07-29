#include "Nebula/Il2Cpp/Il2Cpp.h"

#include <dlfcn.h>
#include <link.h>

#include <cstring>
#include <atomic>
#include <mutex>

#include "Nebula/Core/Log.h"
#include "Nebula/Hook/Hook.h"

namespace {

using Il2CppInitFn = Il2CppDomain* (*)(const char*);

Il2CppInitFn g_originalIl2CppInit = nullptr;
std::atomic<bool> g_runtimeInitialized{false};

Il2CppDomain* Il2CppInitHook(const char* domainName) {
    if (g_originalIl2CppInit == nullptr) {
        return nullptr;
    }

    Il2CppDomain* domain = g_originalIl2CppInit(domainName);
    g_runtimeInitialized.store(
        domain != nullptr, std::memory_order_release);
    NEBULA_LOGI(
        "il2cpp_init completed; domain=%p",
        static_cast<void*>(domain));
    return domain;
}

uintptr_t FindLibraryBase(const char* libraryName) {
    struct Search {
        const char* name;
        uintptr_t base;
    } search{libraryName, 0};

    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) {
            auto* searchData = static_cast<Search*>(data);
            if (info->dlpi_name != nullptr &&
                std::strstr(info->dlpi_name, searchData->name) != nullptr) {
                searchData->base = static_cast<uintptr_t>(info->dlpi_addr);
                return 1;
            }
            return 0;
        },
        &search);
    return search.base;
}

template <typename T>
bool Resolve(void* handle, const char* name, T& target) {
    target = reinterpret_cast<T>(dlsym(handle, name));
    if (target == nullptr) {
        NEBULA_LOGE("Missing IL2CPP export: %s", name);
        return false;
    }
    return true;
}

bool AssemblyNameMatches(const char* actual, const std::string& requested) {
    if (actual == nullptr) {
        return false;
    }
    if (requested == actual) {
        return true;
    }
    constexpr const char* suffix = ".dll";
    const size_t actualLength = std::strlen(actual);
    const size_t suffixLength = std::strlen(suffix);
    if (actualLength > suffixLength &&
        std::strcmp(actual + actualLength - suffixLength, suffix) == 0) {
        return requested ==
               std::string(actual, actualLength - suffixLength);
    }
    return requested + suffix == actual;
}

} // namespace

namespace Nebula {

Il2Cpp& Il2Cpp::Get() {
    static Il2Cpp instance;
    return instance;
}

bool Il2Cpp::Initialize(void* libraryHandle) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if (IsReady()) {
        return true;
    }

    handle_ = libraryHandle;
    if (handle_ == nullptr) {
        handle_ = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    }
    if (handle_ == nullptr) {
        return false;
    }

    baseAddress_ = FindLibraryBase("libil2cpp.so");
    const bool resolved =
        Resolve(handle_, "il2cpp_domain_get", domainGet_) &&
        Resolve(handle_, "il2cpp_domain_get_assemblies",
                domainGetAssemblies_) &&
        Resolve(handle_, "il2cpp_assembly_get_image", assemblyGetImage_) &&
        Resolve(handle_, "il2cpp_image_get_name", imageGetName_) &&
        Resolve(handle_, "il2cpp_class_from_name", classFromName_) &&
        Resolve(handle_, "il2cpp_class_get_method_from_name",
                classGetMethodFromName_) &&
        Resolve(handle_, "il2cpp_class_get_field_from_name",
                classGetFieldFromName_) &&
        Resolve(handle_, "il2cpp_field_get_offset", fieldGetOffset_) &&
        Resolve(handle_, "il2cpp_field_static_get_value",
                fieldStaticGetValue_) &&
        Resolve(handle_, "il2cpp_runtime_invoke", runtimeInvoke_) &&
        Resolve(handle_, "il2cpp_object_unbox", objectUnbox_) &&
        Resolve(handle_, "il2cpp_thread_attach", threadAttach_) &&
        Resolve(handle_, "il2cpp_string_new", stringNew_);

    if (!resolved || baseAddress_ == 0) {
        NEBULA_LOGE("Could not initialize IL2CPP runtime");
        return false;
    }

    NEBULA_LOGI("IL2CPP initialized; base=%p",
                reinterpret_cast<void*>(baseAddress_));
    return true;
}

bool Il2Cpp::IsReady() const {
    return handle_ != nullptr && baseAddress_ != 0 && domainGet_ != nullptr &&
           runtimeInvoke_ != nullptr;
}

bool Il2Cpp::ObserveRuntimeInitialization() {
    if (!IsReady()) {
        return false;
    }
    if (g_runtimeInitialized.load(std::memory_order_acquire) ||
        g_originalIl2CppInit != nullptr) {
        return true;
    }

    void* init = dlsym(handle_, "il2cpp_init");
    if (init == nullptr) {
        NEBULA_LOGE("Missing IL2CPP export: il2cpp_init");
        return false;
    }

    if (!Hook::HookFunction(
            init,
            reinterpret_cast<void*>(&Il2CppInitHook),
            reinterpret_cast<void**>(&g_originalIl2CppInit))) {
        NEBULA_LOGE("Could not observe il2cpp_init");
        return false;
    }

    NEBULA_LOGI("Waiting for il2cpp_init to complete");
    return true;
}

bool Il2Cpp::IsRuntimeInitialized() const {
    return g_runtimeInitialized.load(std::memory_order_acquire);
}

uintptr_t Il2Cpp::GetBaseAddress() const {
    return baseAddress_;
}

const Il2CppImage* Il2Cpp::GetImage(
    const std::string& assemblyName) const {
    if (!IsReady()) {
        return nullptr;
    }
    Il2CppDomain* domain = domainGet_();
    if (domain == nullptr) {
        return nullptr;
    }

    size_t count = 0;
    const Il2CppAssembly** assemblies =
        domainGetAssemblies_(domain, &count);
    for (size_t index = 0; index < count; ++index) {
        const Il2CppImage* image = assemblyGetImage_(assemblies[index]);
        if (image != nullptr &&
            AssemblyNameMatches(imageGetName_(image), assemblyName)) {
            return image;
        }
    }
    return nullptr;
}

Il2CppClass* Il2Cpp::GetClass(
    const std::string& assemblyName,
    const std::string& namespaze,
    const std::string& className) const {
    const Il2CppImage* image = GetImage(assemblyName);
    return image == nullptr
               ? nullptr
               : classFromName_(image, namespaze.c_str(), className.c_str());
}

const MethodInfo* Il2Cpp::GetMethod(
    Il2CppClass* klass,
    const std::string& methodName,
    int argumentCount) const {
    return klass == nullptr
               ? nullptr
               : classGetMethodFromName_(
                     klass, methodName.c_str(), argumentCount);
}

const MethodInfo* Il2Cpp::GetMethod(
    const std::string& assemblyName,
    const std::string& namespaze,
    const std::string& className,
    const std::string& methodName,
    int argumentCount) const {
    return GetMethod(GetClass(assemblyName, namespaze, className),
                     methodName, argumentCount);
}

uintptr_t Il2Cpp::GetMethodAddress(const MethodInfo* method) const {
    if (method == nullptr) {
        return 0;
    }
    // methodPointer is the first field of MethodInfo in supported Unity
    // IL2CPP layouts. Using an opaque read keeps Nebula independent of the
    // game's generated type header.
    return reinterpret_cast<uintptr_t>(
        *reinterpret_cast<void* const*>(method));
}

uintptr_t Il2Cpp::GetMethodAddress(
    const std::string& assemblyName,
    const std::string& namespaze,
    const std::string& className,
    const std::string& methodName,
    int argumentCount) const {
    return GetMethodAddress(GetMethod(
        assemblyName, namespaze, className, methodName, argumentCount));
}

FieldInfo* Il2Cpp::GetField(
    Il2CppClass* klass, const std::string& fieldName) const {
    return klass == nullptr
               ? nullptr
               : classGetFieldFromName_(klass, fieldName.c_str());
}

ptrdiff_t Il2Cpp::GetFieldOffset(
    Il2CppClass* klass, const std::string& fieldName) const {
    FieldInfo* field = GetField(klass, fieldName);
    return field == nullptr ? -1
                            : static_cast<ptrdiff_t>(fieldGetOffset_(field));
}

bool Il2Cpp::GetStaticFieldValue(
    FieldInfo* field, void* output) const {
    if (!IsReady() || fieldStaticGetValue_ == nullptr ||
        field == nullptr || output == nullptr) {
        return false;
    }
    fieldStaticGetValue_(field, output);
    return true;
}

ptrdiff_t Il2Cpp::GetFieldOffset(
    const std::string& assemblyName,
    const std::string& namespaze,
    const std::string& className,
    const std::string& fieldName) const {
    return GetFieldOffset(
        GetClass(assemblyName, namespaze, className), fieldName);
}

Il2CppObject* Il2Cpp::Invoke(
    const MethodInfo* method,
    void* instance,
    void** params,
    Il2CppException** exception) const {
    if (!IsReady() || method == nullptr) {
        return nullptr;
    }
    Il2CppException* localException = nullptr;
    Il2CppException** output =
        exception == nullptr ? &localException : exception;
    Il2CppObject* result =
        runtimeInvoke_(method, instance, params, output);
    if (*output != nullptr) {
        NEBULA_LOGE("Managed exception from il2cpp_runtime_invoke: %p",
                    static_cast<void*>(*output));
    }
    return result;
}

void* Il2Cpp::ObjectUnbox(Il2CppObject* object) const {
    return object == nullptr ? nullptr : objectUnbox_(object);
}

Il2CppThread* Il2Cpp::AttachCurrentThread() const {
    if (!IsReady()) {
        return nullptr;
    }

    // libil2cpp.so being mapped and its exports being resolvable does not
    // mean that Unity has already created the managed domain. Calling
    // il2cpp_thread_attach(nullptr) crashes on several Unity versions.
    Il2CppDomain* domain = domainGet_();
    if (domain == nullptr) {
        return nullptr;
    }

    return threadAttach_(domain);
}

Il2CppString* Il2Cpp::NewString(const std::string& value) const {
    return IsReady() ? stringNew_(value.c_str()) : nullptr;
}

} // namespace Nebula
