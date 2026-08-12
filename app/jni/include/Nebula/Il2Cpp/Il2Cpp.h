#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Nebula resolves the exported IL2CPP API at runtime, so the core only needs
// opaque declarations. Game-specific Il2CppDumper headers should be included
// by individual mods, not by this frequently parsed public header.
struct MethodInfo;
struct Il2CppClass;
struct Il2CppObject;
struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppThread;
struct Il2CppString;
struct Il2CppException;
struct FieldInfo;

namespace Nebula {
class Il2Cpp final {
public:
    static Il2Cpp& Get();

    // Resolves the exported IL2CPP C API from an already loaded libil2cpp.so.
    bool Initialize(void* libraryHandle = nullptr);
    [[nodiscard]] bool IsReady() const;
    // Installs a one-shot observation hook on il2cpp_init. Domain APIs must
    // not be called until this reports true on Unity versions that initialize
    // IL2CPP asynchronously after loading the library.
    bool ObserveRuntimeInitialization();
    [[nodiscard]] bool IsRuntimeInitialized() const;
    [[nodiscard]] uintptr_t GetBaseAddress() const;

    [[nodiscard]] const Il2CppImage* GetImage(
        const std::string& assemblyName) const;
    [[nodiscard]] Il2CppClass* GetClass(
        const std::string& assemblyName,
        const std::string& namespaze,
        const std::string& className) const;
    [[nodiscard]] const MethodInfo* GetMethod(
        Il2CppClass* klass,
        const std::string& methodName,
        int argumentCount = -1) const;
    [[nodiscard]] const MethodInfo* GetMethod(
        const std::string& assemblyName,
        const std::string& namespaze,
        const std::string& className,
        const std::string& methodName,
        int argumentCount = -1) const;
    [[nodiscard]] uintptr_t GetMethodAddress(const MethodInfo* method) const;
    [[nodiscard]] uintptr_t GetMethodAddress(
        const std::string& assemblyName,
        const std::string& namespaze,
        const std::string& className,
        const std::string& methodName,
        int argumentCount = -1) const;

    [[nodiscard]] FieldInfo* GetField(
        Il2CppClass* klass, const std::string& fieldName) const;
    [[nodiscard]] ptrdiff_t GetFieldOffset(
        Il2CppClass* klass, const std::string& fieldName) const;
    [[nodiscard]] ptrdiff_t GetFieldOffset(
        const std::string& assemblyName,
        const std::string& namespaze,
        const std::string& className,
        const std::string& fieldName) const;
    bool GetStaticFieldValue(
        FieldInfo* field, void* output) const;
    bool SetStaticFieldValue(
        FieldInfo* field, const void* value) const;

    // Uses the IL2CPP field APIs rather than raw pointer writes. SetFieldValue
    // is required for managed references because the runtime applies the GC
    // write barrier when the object field changes.
    bool GetFieldValue(
        Il2CppObject* instance, FieldInfo* field, void* output) const;
    bool SetFieldValue(
        Il2CppObject* instance, FieldInfo* field,
        const void* value) const;

    template <typename T>
    bool GetFieldValue(
        Il2CppObject* instance, FieldInfo* field, T& output) const {
        return GetFieldValue(instance, field,
                             static_cast<void*>(&output));
    }

    template <typename T>
    bool SetFieldValue(
        Il2CppObject* instance, FieldInfo* field,
        const T& value) const {
        return SetFieldValue(instance, field,
                             static_cast<const void*>(&value));
    }

    [[nodiscard]] Il2CppObject* GetReferenceField(
        Il2CppObject* instance, FieldInfo* field) const;
    [[nodiscard]] Il2CppObject* GetReferenceField(
        Il2CppObject* instance, const std::string& fieldName) const;
    bool SetReferenceField(
        Il2CppObject* instance, FieldInfo* field,
        Il2CppObject* value) const;
    bool SetReferenceField(
        Il2CppObject* instance, const std::string& fieldName,
        Il2CppObject* value) const;

    [[nodiscard]] Il2CppObject* GetStaticReferenceField(
        FieldInfo* field) const;
    bool SetStaticReferenceField(
        FieldInfo* field, Il2CppObject* value) const;

    template <typename T>
    [[nodiscard]] T ReadField(void* instance, ptrdiff_t offset) const {
        return *reinterpret_cast<T*>(
            reinterpret_cast<uintptr_t>(instance) +
            static_cast<uintptr_t>(offset));
    }

    template <typename T>
    void WriteField(void* instance, ptrdiff_t offset, const T& value) const {
        *reinterpret_cast<T*>(
            reinterpret_cast<uintptr_t>(instance) +
            static_cast<uintptr_t>(offset)) = value;
    }

    // Calls a managed method through il2cpp_runtime_invoke. For instance
    // methods pass the object as `instance`; params is an array of pointers to
    // argument storage. A managed exception is returned through `exception`.
    [[nodiscard]] Il2CppObject* Invoke(
        const MethodInfo* method,
        void* instance,
        void** params = nullptr,
        Il2CppException** exception = nullptr) const;
    [[nodiscard]] Il2CppObject* Invoke(
        Il2CppObject* instance,
        const std::string& methodName,
        int argumentCount,
        void** params = nullptr,
        Il2CppException** exception = nullptr) const;

    template <typename T>
    [[nodiscard]] T Unbox(Il2CppObject* object) const {
        void* value = ObjectUnbox(object);
        return value == nullptr ? T{} : *static_cast<T*>(value);
    }

    // Attaches the current native thread to the IL2CPP GC.
    [[nodiscard]] Il2CppThread* AttachCurrentThread() const;
    [[nodiscard]] Il2CppString* NewString(const std::string& value) const;
    [[nodiscard]] std::string StringToUtf8(
        Il2CppString* value) const;
    [[nodiscard]] Il2CppClass* GetObjectClass(
        Il2CppObject* object) const;
    [[nodiscard]] Il2CppObject* NewObject(Il2CppClass* klass) const;

    bool GetStringField(
        Il2CppObject* instance, FieldInfo* field,
        std::string& output) const;
    bool GetStringField(
        Il2CppObject* instance, const std::string& fieldName,
        std::string& output) const;
    bool SetStringField(
        Il2CppObject* instance, FieldInfo* field,
        const std::string& value) const;
    bool SetStringField(
        Il2CppObject* instance, const std::string& fieldName,
        const std::string& value) const;

private:
    Il2Cpp() = default;
    void* ObjectUnbox(Il2CppObject* object) const;

    void* handle_ = nullptr;
    uintptr_t baseAddress_ = 0;

    Il2CppDomain* (*domainGet_)() = nullptr;
    const Il2CppAssembly** (*domainGetAssemblies_)(
        const Il2CppDomain*, size_t*) = nullptr;
    const Il2CppImage* (*assemblyGetImage_)(
        const Il2CppAssembly*) = nullptr;
    const char* (*imageGetName_)(const Il2CppImage*) = nullptr;
    Il2CppClass* (*classFromName_)(
        const Il2CppImage*, const char*, const char*) = nullptr;
    const MethodInfo* (*classGetMethodFromName_)(
        Il2CppClass*, const char*, int) = nullptr;
    FieldInfo* (*classGetFieldFromName_)(
        Il2CppClass*, const char*) = nullptr;
    size_t (*fieldGetOffset_)(FieldInfo*) = nullptr;
    void (*fieldStaticGetValue_)(FieldInfo*, void*) = nullptr;
    void (*fieldStaticSetValue_)(FieldInfo*, void*) = nullptr;
    void (*fieldGetValue_)(Il2CppObject*, FieldInfo*, void*) = nullptr;
    void (*fieldSetValue_)(Il2CppObject*, FieldInfo*, void*) = nullptr;
    Il2CppObject* (*runtimeInvoke_)(
        const MethodInfo*, void*, void**, Il2CppException**) = nullptr;
    void* (*objectUnbox_)(Il2CppObject*) = nullptr;
    Il2CppClass* (*objectGetClass_)(Il2CppObject*) = nullptr;
    Il2CppObject* (*objectNew_)(const Il2CppClass*) = nullptr;
    Il2CppThread* (*threadAttach_)(Il2CppDomain*) = nullptr;
    Il2CppString* (*stringNew_)(const char*) = nullptr;
    int32_t (*stringLength_)(Il2CppString*) = nullptr;
    const uint16_t* (*stringChars_)(Il2CppString*) = nullptr;
};

} // namespace Nebula
