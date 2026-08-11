#include "Nebula/Hook/MemoryPatch.h"

#include <charconv>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

#include <dobby.h>

#include "Nebula/Core/Log.h"
#include "Nebula/Il2Cpp/Il2Cpp.h"

namespace Nebula::Hook {

namespace {

constexpr uintptr_t kArm64InstructionSize = 4;

bool IsSeparator(char value) {
    return value == ' ' || value == '\t' || value == '\r' ||
           value == '\n' || value == ',';
}

std::string_view Trim(std::string_view value) {
    while (!value.empty() && IsSeparator(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && IsSeparator(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

bool ParseRva(std::string_view text, uintptr_t& rva) {
    text = Trim(text);
    if (text.size() >= 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return false;
    }

    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, rva, 16);
    return result.ec == std::errc{} && result.ptr == end;
}

bool ParseBytes(std::string_view text, MemoryPatch::Bytes& bytes) {
    bytes.clear();
    size_t offset = 0;
    while (offset < text.size()) {
        while (offset < text.size() && IsSeparator(text[offset])) {
            ++offset;
        }
        if (offset == text.size()) {
            break;
        }

        const size_t begin = offset;
        while (offset < text.size() && !IsSeparator(text[offset])) {
            ++offset;
        }
        std::string_view token = text.substr(begin, offset - begin);
        if (token.size() >= 2 && token[0] == '0' &&
            (token[1] == 'x' || token[1] == 'X')) {
            token.remove_prefix(2);
        }
        if (token.size() != 2) {
            bytes.clear();
            return false;
        }

        uint32_t byte = 0;
        const char* const tokenBegin = token.data();
        const char* const tokenEnd = tokenBegin + token.size();
        const auto result =
            std::from_chars(tokenBegin, tokenEnd, byte, 16);
        if (result.ec != std::errc{} || result.ptr != tokenEnd ||
            byte > std::numeric_limits<uint8_t>::max()) {
            bytes.clear();
            return false;
        }
        bytes.push_back(static_cast<uint8_t>(byte));
    }
    return !bytes.empty();
}

bool BytesEqual(uintptr_t address, const MemoryPatch::Bytes& bytes) {
    return std::memcmp(
               reinterpret_cast<const void*>(address),
               bytes.data(), bytes.size()) == 0;
}

} // namespace

uintptr_t ResolveIl2CppRva(uintptr_t rva) {
    const uintptr_t base = Il2Cpp::Get().GetBaseAddress();
    if (base == 0 ||
        rva > std::numeric_limits<uintptr_t>::max() - base) {
        NEBULA_LOGE(
            "Could not resolve IL2CPP RVA 0x%zx (base=%p)",
            static_cast<size_t>(rva),
            reinterpret_cast<void*>(base));
        return 0;
    }
    return base + rva;
}

MemoryPatch::MemoryPatch(
    uintptr_t address, Bytes replacement, Bytes expected) {
    Configure(address, std::move(replacement), std::move(expected));
}

MemoryPatch::MemoryPatch(
    std::string_view il2cppRva, std::string_view replacement,
    std::string_view expected) {
    Configure(il2cppRva, replacement, expected);
}

bool MemoryPatch::Configure(
    uintptr_t address, Bytes replacement, Bytes expected) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (applied_) {
        NEBULA_LOGE("Cannot configure an applied memory patch");
        return false;
    }

    address_ = address;
    replacement_ = std::move(replacement);
    expected_ = std::move(expected);
    original_.clear();

    if (!IsValidConfigurationLocked()) {
        address_ = 0;
        replacement_.clear();
        expected_.clear();
        return false;
    }
    return true;
}

bool MemoryPatch::Configure(
    std::string_view il2cppRva, std::string_view replacement,
    std::string_view expected) {
    uintptr_t rva = 0;
    Bytes replacementBytes;
    Bytes expectedBytes;
    if (!ParseRva(il2cppRva, rva)) {
        NEBULA_LOGE("Invalid IL2CPP RVA string");
        return false;
    }
    if (!ParseBytes(replacement, replacementBytes)) {
        NEBULA_LOGE("Invalid replacement byte string");
        return false;
    }
    if (!Trim(expected).empty() &&
        !ParseBytes(expected, expectedBytes)) {
        NEBULA_LOGE("Invalid expected-byte string");
        return false;
    }

    const uintptr_t address = ResolveIl2CppRva(rva);
    if (address == 0) {
        return false;
    }
    return Configure(
        address, std::move(replacementBytes),
        std::move(expectedBytes));
}

bool MemoryPatch::Apply() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (applied_) {
        return true;
    }
    if (!IsValidConfigurationLocked()) {
        return false;
    }
    if (!expected_.empty() && !BytesEqual(address_, expected_)) {
        NEBULA_LOGE(
            "Memory patch expected-byte mismatch at %p",
            reinterpret_cast<void*>(address_));
        return false;
    }

    original_.resize(replacement_.size());
    std::memcpy(
        original_.data(), reinterpret_cast<const void*>(address_),
        original_.size());

    const int status = DobbyCodePatch(
        reinterpret_cast<void*>(address_), replacement_.data(),
        static_cast<uint32_t>(replacement_.size()));
    if (status != 0) {
        original_.clear();
        NEBULA_LOGE(
            "DobbyCodePatch failed for %p (status=%d)",
            reinterpret_cast<void*>(address_), status);
        return false;
    }

    applied_ = true;
    NEBULA_LOGI(
        "Memory patch applied at %p (%zu bytes)",
        reinterpret_cast<void*>(address_), replacement_.size());
    return true;
}

bool MemoryPatch::Restore() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!applied_) {
        return true;
    }
    if (original_.size() != replacement_.size()) {
        NEBULA_LOGE("Memory patch has no valid restore bytes");
        return false;
    }
    if (!BytesEqual(address_, replacement_)) {
        NEBULA_LOGE(
            "Memory patch changed externally at %p; refusing to restore",
            reinterpret_cast<void*>(address_));
        return false;
    }

    const int status = DobbyCodePatch(
        reinterpret_cast<void*>(address_), original_.data(),
        static_cast<uint32_t>(original_.size()));
    if (status != 0) {
        NEBULA_LOGE(
            "Could not restore memory patch at %p (status=%d)",
            reinterpret_cast<void*>(address_), status);
        return false;
    }

    applied_ = false;
    original_.clear();
    NEBULA_LOGI(
        "Memory patch restored at %p",
        reinterpret_cast<void*>(address_));
    return true;
}

bool MemoryPatch::SetEnabled(bool enabled) {
    return enabled ? Apply() : Restore();
}

bool MemoryPatch::IsConfigured() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return IsValidConfigurationLocked();
}

bool MemoryPatch::IsApplied() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return applied_;
}

uintptr_t MemoryPatch::GetAddress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return address_;
}

size_t MemoryPatch::GetSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return replacement_.size();
}

bool MemoryPatch::IsValidConfigurationLocked() const {
    if (address_ == 0 || replacement_.empty()) {
        NEBULA_LOGE("Memory patch requires an address and replacement bytes");
        return false;
    }
    if ((address_ % kArm64InstructionSize) != 0 ||
        (replacement_.size() % kArm64InstructionSize) != 0) {
        NEBULA_LOGE(
            "ARM64 code patches must be 4-byte aligned and sized");
        return false;
    }
    if (replacement_.size() >
        static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        NEBULA_LOGE("Memory patch is too large");
        return false;
    }
    if (!expected_.empty() &&
        expected_.size() != replacement_.size()) {
        NEBULA_LOGE(
            "Expected and replacement byte counts must match");
        return false;
    }
    return true;
}

} // namespace Nebula::Hook
