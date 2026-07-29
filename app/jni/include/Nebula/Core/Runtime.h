#pragma once

namespace Nebula {

class Runtime final {
public:
    static Runtime& Get();
    void Start();

private:
    Runtime() = default;
    static void* Bootstrap(void*);
};

} // namespace Nebula
