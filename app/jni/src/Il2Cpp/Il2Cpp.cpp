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

void AppendUtf8(std::string& output, uint32_t codePoint) {
    if (codePoint <= 0x7F) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(
            0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(
            0x80 | ((codePoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(
            0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
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
        Resolve(handle_, "il2cpp_field_static_set_value",
                fieldStaticSetValue_) &&
        Resolve(handle_, "il2cpp_field_get_value", fieldGetValue_) &&
        Resolve(handle_, "il2cpp_field_set_value", fieldSetValue_) &&
        Resolve(handle_, "il2cpp_runtime_invoke", runtimeInvoke_) &&
        Resolve(handle_, "il2cpp_object_unbox", objectUnbox_) &&
        Resolve(handle_, "il2cpp_object_get_class", objectGetClass_) &&
        Resolve(handle_, "il2cpp_object_new", objectNew_) &&
        Resolve(handle_, "il2cpp_thread_attach", threadAttach_) &&
        Resolve(handle_, "il2cpp_string_new", stringNew_) &&
        Resolve(handle_, "il2cpp_string_length", stringLength_) &&
        Resolve(handle_, "il2cpp_string_chars", stringChars_);

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

bool Il2Cpp::SetStaticFieldValue(
    FieldInfo* field, const void* value) const {
    if (!IsReady() || fieldStaticSetValue_ == nullptr ||
        field == nullptr || value == nullptr) {
        return false;
    }
    fieldStaticSetValue_(field, const_cast<void*>(value));
    return true;
}

bool Il2Cpp::GetFieldValue(
    Il2CppObject* instance, FieldInfo* field, void* output) const {
    if (!IsReady() || fieldGetValue_ == nullptr ||
        instance == nullptr || field == nullptr || output == nullptr) {
        return false;
    }
    fieldGetValue_(instance, field, output);
    return true;
}

bool Il2Cpp::SetFieldValue(
    Il2CppObject* instance, FieldInfo* field,
    const void* value) const {
    if (!IsReady() || fieldSetValue_ == nullptr ||
        instance == nullptr || field == nullptr || value == nullptr) {
        return false;
    }
    fieldSetValue_(instance, field, const_cast<void*>(value));
    return true;
}

Il2CppObject* Il2Cpp::GetReferenceField(
    Il2CppObject* instance, FieldInfo* field) const {
    Il2CppObject* value = nullptr;
    return GetFieldValue(
               instance, field, static_cast<void*>(&value))
               ? value
               : nullptr;
}

Il2CppObject* Il2Cpp::GetReferenceField(
    Il2CppObject* instance, const std::string& fieldName) const {
    return GetReferenceField(
        instance, GetField(GetObjectClass(instance), fieldName));
}

bool Il2Cpp::SetReferenceField(
    Il2CppObject* instance, FieldInfo* field,
    Il2CppObject* value) const {
    return SetFieldValue(
        instance, field, static_cast<const void*>(&value));
}

bool Il2Cpp::SetReferenceField(
    Il2CppObject* instance, const std::string& fieldName,
    Il2CppObject* value) const {
    return SetReferenceField(
        instance, GetField(GetObjectClass(instance), fieldName), value);
}

Il2CppObject* Il2Cpp::GetStaticReferenceField(FieldInfo* field) const {
    Il2CppObject* value = nullptr;
    return GetStaticFieldValue(field, &value) ? value : nullptr;
}

bool Il2Cpp::SetStaticReferenceField(
    FieldInfo* field, Il2CppObject* value) const {
    return SetStaticFieldValue(field, &value);
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

Il2CppObject* Il2Cpp::Invoke(
    Il2CppObject* instance,
    const std::string& methodName,
    int argumentCount,
    void** params,
    Il2CppException** exception) const {
    if (instance == nullptr) {
        return nullptr;
    }
    return Invoke(
        GetMethod(GetObjectClass(instance), methodName, argumentCount),
        instance, params, exception);
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

std::string Il2Cpp::StringToUtf8(Il2CppString* value) const {
    std::string output;
    if (!IsReady() || value == nullptr || stringLength_ == nullptr ||
        stringChars_ == nullptr) {
        return output;
    }

    const int32_t length = stringLength_(value);
    const uint16_t* characters = stringChars_(value);
    if (length <= 0 || characters == nullptr) {
        return output;
    }
    output.reserve(static_cast<size_t>(length));
    for (int32_t index = 0; index < length; ++index) {
        uint32_t codePoint = characters[index];
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF &&
            index + 1 < length) {
            const uint32_t low = characters[index + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codePoint = 0x10000 +
                    ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                ++index;
            } else {
                codePoint = 0xFFFD;
            }
        } else if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
            codePoint = 0xFFFD;
        }
        AppendUtf8(output, codePoint);
    }
    return output;
}

Il2CppClass* Il2Cpp::GetObjectClass(Il2CppObject* object) const {
    return IsReady() && object != nullptr && objectGetClass_ != nullptr
               ? objectGetClass_(object)
               : nullptr;
}

Il2CppObject* Il2Cpp::NewObject(Il2CppClass* klass) const {
    return IsReady() && klass != nullptr && objectNew_ != nullptr
               ? objectNew_(klass)
               : nullptr;
}

bool Il2Cpp::GetStringField(
    Il2CppObject* instance, FieldInfo* field,
    std::string& output) const {
    Il2CppObject* object = nullptr;
    if (!GetFieldValue(
            instance, field, static_cast<void*>(&object))) {
        return false;
    }
    output = StringToUtf8(reinterpret_cast<Il2CppString*>(object));
    return true;
}

bool Il2Cpp::GetStringField(
    Il2CppObject* instance, const std::string& fieldName,
    std::string& output) const {
    return GetStringField(
        instance, GetField(GetObjectClass(instance), fieldName), output);
}

bool Il2Cpp::SetStringField(
    Il2CppObject* instance, FieldInfo* field,
    const std::string& value) const {
    Il2CppString* string = NewString(value);
    return string != nullptr && SetReferenceField(
        instance, field, reinterpret_cast<Il2CppObject*>(string));
}

bool Il2Cpp::SetStringField(
    Il2CppObject* instance, const std::string& fieldName,
    const std::string& value) const {
    return SetStringField(
        instance, GetField(GetObjectClass(instance), fieldName), value);
}

} // namespace Nebula
