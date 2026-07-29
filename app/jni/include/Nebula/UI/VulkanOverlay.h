#pragma once

namespace Nebula {

class VulkanOverlay final {
public:
    static VulkanOverlay& Get();
    bool Install();

private:
    VulkanOverlay() = default;
};

} // namespace Nebula
