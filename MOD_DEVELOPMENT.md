# NebulaIL2CPP Mod 制作指南

本文档说明如何为 NebulaIL2CPP 编写一个可编译的 Native Mod。当前框架
不依赖 Unity 官方头文件，也不需要把完整的 Il2CppDumper `il2cpp.h`
放进工程。类、方法和字段均通过 IL2CPP Runtime API 按名称动态解析。

请只在获得授权的应用、离线测试环境或自己的项目中使用。

## 1. Mod 生命周期

所有 Mod 都实现 `Nebula::IMod`：

```cpp
class IMod {
public:
    virtual ~IMod() = default;
    virtual void OnLoad() = 0;
    virtual void OnUpdate() = 0;
    virtual void OnGUI() = 0;
};
```

- `OnLoad()`：IL2CPP 初始化完成、当前线程完成 attach 后调用一次。适合
  查找类、方法、字段并安装 Hook。
- `OnUpdate()`：每个 ImGui 渲染帧调用。不要在这里执行耗时扫描或重复安装
  Hook。
- `OnGUI()`：每个 UI 帧调用。只在这里编写 ImGui 控件。

## 2. 创建最小 Mod

创建头文件：

```text
app/jni/include/Nebula/Mods/MyMod.h
```

```cpp
#pragma once

#include <atomic>

#include "Nebula/Mods/IMod.h"

namespace Nebula {

class MyMod final : public IMod {
public:
    void OnLoad() override;
    void OnUpdate() override;
    void OnGUI() override;

private:
    std::atomic<bool> enabled_{false};
};

} // namespace Nebula
```

创建实现文件：

```text
app/jni/src/Mods/MyMod.cpp
```

```cpp
#include "Nebula/Mods/MyMod.h"

#include <imgui.h>

#include "Nebula/Config/Config.h"
#include "Nebula/Core/Log.h"

namespace Nebula {

void MyMod::OnLoad() {
    enabled_.store(
        Config::Get().GetBool("my_mod.enabled", false));
    NEBULA_LOGI("MyMod loaded");
}

void MyMod::OnUpdate() {
}

void MyMod::OnGUI() {
    if (!ImGui::BeginTabItem("My Mod")) {
        return;
    }

    bool enabled = enabled_.load();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        enabled_.store(enabled);
        Config::Get().SetBool("my_mod.enabled", enabled);
    }

    ImGui::EndTabItem();
}

} // namespace Nebula
```

## 3. 加入编译

在 `app/jni/CMakeLists.txt` 的 `add_library(Nebula SHARED ...)` 中加入：

```cmake
src/Mods/MyMod.cpp
```

然后在 `app/jni/src/Core/Runtime.cpp` 中包含并注册：

```cpp
#include "Nebula/Mods/MyMod.h"
```

注册必须发生在 `ModManager::Get().LoadAll()` 之前：

```cpp
ModManager::Get().Register(std::make_unique<MyMod>());
ModManager::Get().LoadAll();
```

不要在 `LoadAll()` 之后注册，框架会忽略迟到的 Mod。

## 4. 查找 IL2CPP 类

```cpp
auto& il2cpp = Nebula::Il2Cpp::Get();

Il2CppClass* player = il2cpp.GetClass(
    "Assembly-CSharp",
    "",             // namespace
    "Player");
```

带命名空间的示例：

```cpp
Il2CppClass* player = il2cpp.GetClass(
    "Assembly-CSharp",
    "Game.Characters",
    "Player");
```

程序集名称既可以写 `Assembly-CSharp`，也可以按目标游戏实际 image 名称
填写。名称区分大小写。返回 `nullptr` 表示程序集、命名空间或类名不匹配。

## 5. 查找方法和地址

按类、方法名和参数数量查找：

```cpp
const MethodInfo* method =
    il2cpp.GetMethod(player, "TakeDamage", 1);

uintptr_t address =
    il2cpp.GetMethodAddress(method);
```

也可以一步完成：

```cpp
uintptr_t address = il2cpp.GetMethodAddress(
    "Assembly-CSharp",
    "",
    "Player",
    "TakeDamage",
    1);
```

这里的参数数量不包含实例指针 `this`，也不包含 IL2CPP Native 函数末尾的
`MethodInfo*`。

## 6. Hook IL2CPP 实例方法

假设 C# 方法为：

```csharp
public void TakeDamage(int damage);
```

对应常见的 Native Hook 签名：

```cpp
using TakeDamageFn = void (*)(
    void* instance,
    int32_t damage,
    const MethodInfo* method);

static TakeDamageFn originalTakeDamage = nullptr;

static void TakeDamageHook(
    void* instance,
    int32_t damage,
    const MethodInfo* method) {
    damage = 0;
    originalTakeDamage(instance, damage, method);
}
```

安装 Hook：

```cpp
bool installed = Nebula::Hook::HookFunction(
    address,
    reinterpret_cast<void*>(&TakeDamageHook),
    reinterpret_cast<void**>(&originalTakeDamage));
```

注意事项：

- ARM64 参数类型、顺序和返回值必须与目标函数一致。
- 实例方法第一个参数通常是对象指针。
- 普通 IL2CPP 方法最后通常带 `const MethodInfo*` 隐藏参数。
- 静态方法没有实例指针。
- 返回非 `void` 的 Hook 必须返回正确类型。
- 调用原函数前必须检查 trampoline 是否为 `nullptr`。

## 7. 读取和修改字段

先动态获取字段偏移：

```cpp
ptrdiff_t healthOffset =
    il2cpp.GetFieldOffset(player, "health");
```

必须检查解析结果：

```cpp
if (healthOffset < 0) {
    NEBULA_LOGW("Player.health was not found");
    return;
}
```

读取字段：

```cpp
int32_t health =
    il2cpp.ReadField<int32_t>(instance, healthOffset);
```

写入字段：

```cpp
il2cpp.WriteField<int32_t>(
    instance, healthOffset, 999999);
```

字段操作前必须确保：

- `instance != nullptr`；
- 偏移不小于 `0`；
- 模板类型与实际字段类型一致；
- 对象仍然存活，没有被 Unity 销毁或 GC 回收。

不要长期保存不受管理的 IL2CPP 对象指针。当前 `SunMod` 保存 `Board`
指针是面向目标游戏的简化实现，切换场景时应特别注意对象生命周期。

## 8. 调用 IL2CPP 方法

推荐使用 `Il2Cpp::Invoke()`，它通过 `il2cpp_runtime_invoke` 调用：

```cpp
const MethodInfo* method =
    il2cpp.GetMethod(player, "Heal", 1);

int32_t amount = 100;
void* params[] = {&amount};
Il2CppException* exception = nullptr;

Il2CppObject* result = il2cpp.Invoke(
    method,
    instance,
    params,
    &exception);

if (exception != nullptr) {
    NEBULA_LOGE("Player.Heal raised an IL2CPP exception");
}
```

值类型参数传入参数变量的地址。引用类型参数应按 IL2CPP Runtime 的参数
约定传递。返回值为装箱对象时可以使用：

```cpp
int32_t value = il2cpp.Unbox<int32_t>(result);
```

创建托管字符串：

```cpp
Il2CppString* text = il2cpp.NewString("Nebula");
```

从自己创建的 Native 线程调用 IL2CPP API 前，必须先执行：

```cpp
Il2CppThread* thread =
    il2cpp.AttachCurrentThread();
```

框架 Bootstrap 线程已经自动 attach，但其他新线程不会自动 attach。

## 9. 配置系统

支持 `bool`、`int` 和 `float`：

```cpp
bool enabled =
    Config::Get().GetBool("my_mod.enabled", true);
int damage =
    Config::Get().GetInt("my_mod.damage", 100);
float speed =
    Config::Get().GetFloat("my_mod.speed", 1.0F);

Config::Get().SetBool("my_mod.enabled", enabled);
Config::Get().SetInt("my_mod.damage", damage);
Config::Get().SetFloat("my_mod.speed", speed);
Config::Get().Save();
```

默认配置路径：

```text
/data/data/<package>/files/Nebula/config.json
```

建议使用带 Mod 前缀的 key，避免冲突：

```text
my_mod.enabled
my_mod.damage
sun_mod.enabled
```

## 10. ImGui 控件

每个 Mod 推荐使用独立 Tab：

```cpp
void MyMod::OnGUI() {
    if (!ImGui::BeginTabItem("Player")) {
        return;
    }

    ImGui::Checkbox("God mode", &godMode);
    ImGui::SliderInt("Damage", &damage, 1, 1000);
    ImGui::SliderFloat("Speed", &speed, 0.1F, 10.0F);

    if (ImGui::Button("Apply")) {
        ApplySettings();
    }

    ImGui::EndTabItem();
}
```

必须保证每个成功的 `BeginTabItem()` 都有对应的 `EndTabItem()`。

## 11. 完整 Hook Mod 模板

```cpp
#pragma once

#include <atomic>
#include <cstdint>

#include "Nebula/Mods/IMod.h"

struct MethodInfo;

namespace Nebula {

class DamageMod final : public IMod {
public:
    void OnLoad() override;
    void OnUpdate() override;
    void OnGUI() override;

private:
    using TakeDamageFn = void (*)(
        void*, int32_t, const MethodInfo*);

    static void TakeDamageHook(
        void* instance,
        int32_t damage,
        const MethodInfo* method);

    static inline TakeDamageFn original_ = nullptr;
    static inline std::atomic<bool> enabled_{false};
};

} // namespace Nebula
```

```cpp
#include "Nebula/Mods/DamageMod.h"

#include <imgui.h>

#include "Nebula/Config/Config.h"
#include "Nebula/Core/Log.h"
#include "Nebula/Hook/Hook.h"
#include "Nebula/Il2Cpp/Il2Cpp.h"

namespace Nebula {

void DamageMod::OnLoad() {
    enabled_.store(
        Config::Get().GetBool("damage_mod.enabled", false));

    uintptr_t target = Il2Cpp::Get().GetMethodAddress(
        "Assembly-CSharp", "", "Player", "TakeDamage", 1);
    if (target == 0) {
        NEBULA_LOGW("DamageMod: target method was not found");
        return;
    }

    if (!Hook::HookFunction(
            target,
            reinterpret_cast<void*>(&DamageMod::TakeDamageHook),
            reinterpret_cast<void**>(&original_))) {
        NEBULA_LOGE("DamageMod: hook installation failed");
    }
}

void DamageMod::OnUpdate() {
}

void DamageMod::OnGUI() {
    if (!ImGui::BeginTabItem("Damage")) {
        return;
    }
    bool enabled = enabled_.load();
    if (ImGui::Checkbox("Ignore damage", &enabled)) {
        enabled_.store(enabled);
        Config::Get().SetBool("damage_mod.enabled", enabled);
    }
    ImGui::EndTabItem();
}

void DamageMod::TakeDamageHook(
    void* instance,
    int32_t damage,
    const MethodInfo* method) {
    if (enabled_.load()) {
        damage = 0;
    }
    if (original_ != nullptr) {
        original_(instance, damage, method);
    }
}

} // namespace Nebula
```

## 12. 编译

在项目根目录运行：

```powershell
.\build-nebula.ps1 `
  -OutputDir "E:\Output\Nebula" `
  -CleanOutput
```

产物：

```text
E:\Output\Nebula\libNebula.so
```

脚本使用临时目录完成 CMake/Ninja 构建，成功后只保留最终 SO。

## 13. 日志与排错

使用 Logcat 过滤：

```text
NebulaIL2CPP
```

常见问题：

### 类解析失败

```text
Assembly-CSharp::Player was not found
```

检查程序集、命名空间、类名和大小写。

### 方法解析失败

检查方法名称以及参数数量。重载方法必须传入正确的参数数量。

### Hook 后立即崩溃

通常是 Native 函数签名错误，包括：

- 返回类型错误；
- 参数类型或顺序错误；
- 忘记实例指针；
- 隐藏 `MethodInfo*` 参数处理错误。

### 字段写入崩溃

检查实例指针、字段偏移、字段类型和对象生命周期。

### Mod 没有加载

检查：

1. `.cpp` 是否加入 `CMakeLists.txt`；
2. 头文件是否在 `Runtime.cpp` 中 include；
3. 是否在 `LoadAll()` 前调用 `Register()`；
4. 是否替换了设备中的最新 `libNebula.so`。

## 14. 完整 dump 的使用建议

不要再把几十 MB 的完整 `il2cpp.h` 放进 `app/jni`，否则 Android Studio
会索引全部游戏类型并明显变卡。

推荐流程：

1. 把完整 dump 保存在工程目录之外；
2. 用 dump 查出程序集、命名空间、类型、方法和字段名；
3. 在 Mod 中通过 Nebula Runtime API 按名称解析；
4. 如果必须访问复杂结构体，只复制该目标类型所需的最小布局；
5. 优先使用 `GetFieldOffset()`，避免硬编码版本相关偏移。

仓库中的 `app/jni/external/generated/il2cpp.h` 仅保留最小 opaque 声明，
不应替换成完整游戏 dump。
