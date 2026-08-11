#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace Nebula::Hook {

// Resolves an IDA-style RVA against the currently loaded libil2cpp.so.
// Returns 0 until the IL2CPP module has been initialized.
uintptr_t ResolveIl2CppRva(uintptr_t rva);

// Owns a reversible ARM64 code patch. The optional expected bytes are checked
// before every Apply(), which prevents a stale RVA from patching a different
// game build. This object must outlive every UI/control path that may restore
// the patch.
class MemoryPatch final {
public:
    using Bytes = std::vector<uint8_t>;

    MemoryPatch() = default;
    MemoryPatch(uintptr_t address, Bytes replacement,
                Bytes expected = {});
    MemoryPatch(std::string_view il2cppRva,
                std::string_view replacement,
                std::string_view expected = {});

    MemoryPatch(const MemoryPatch&) = delete;
    MemoryPatch& operator=(const MemoryPatch&) = delete;
    MemoryPatch(MemoryPatch&&) = delete;
    MemoryPatch& operator=(MemoryPatch&&) = delete;

    // Configuration is allowed only while the patch is not applied.
    bool Configure(uintptr_t address, Bytes replacement,
                   Bytes expected = {});

    // Parses an IL2CPP RVA such as "0x212320" and space-separated bytes such
    // as "0f 2f a0 aa". The RVA is resolved against libil2cpp.so.
    bool Configure(std::string_view il2cppRva,
                   std::string_view replacement,
                   std::string_view expected = {});

    bool Apply();
    bool Restore();
    bool SetEnabled(bool enabled);

    bool IsConfigured() const;
    bool IsApplied() const;
    uintptr_t GetAddress() const;
    size_t GetSize() const;

private:
    bool IsValidConfigurationLocked() const;

    mutable std::mutex mutex_;
    uintptr_t address_ = 0;
    Bytes replacement_;
    Bytes expected_;
    Bytes original_;
    bool applied_ = false;
};

} // namespace Nebula::Hook
