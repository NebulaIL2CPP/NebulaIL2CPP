#pragma once

namespace Nebula {

class IMod {
public:
    virtual ~IMod() = default;
    virtual void OnLoad() = 0;
    virtual void OnUpdate() = 0;
    virtual void OnGUI() = 0;
};

} // namespace Nebula
