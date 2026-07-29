# NebulaIL2CPP

NebulaIL2CPP 是一个面向 **Android ARM64 Unity IL2CPP** 应用的 Native
Mod Loader 框架。它提供 IL2CPP Runtime 动态解析、Dobby Inline Hook、
多 Mod 生命周期、JSON 配置，以及独立于 Unity 渲染后端的 Dear ImGui
覆盖层。

> 本项目用于安全研究、兼容性测试、自己的 Unity 项目以及获得明确授权的
> 应用。仓库不提供通用注入器、签名绕过、完整性校验绕过或任何游戏资源。
> 使用者必须自行确认目标应用许可和所在地法律要求。

## 特性

- Android NDK、C++17、CMake、ARM64-v8a
- `JNI_OnLoad` 与 ELF constructor 自动启动
- 等待 `libil2cpp.so` 和 `il2cpp_init` 完成
- IL2CPP Runtime API 动态解析：
  - 获取 `libil2cpp.so` 基址
  - 按程序集、命名空间和名称查找类
  - 查找方法并获取 Native 地址
  - 查找实例/静态字段
  - 读取和修改实例字段
  - `il2cpp_runtime_invoke`
  - Native 线程 attach
  - 创建托管字符串和拆箱返回值
- Dobby ARM64 Inline Hook 与原函数 trampoline
- `IMod` / `ModManager` 多 Mod 生命周期
- Dear ImGui 菜单与完整控件示例
- 推荐的独立透明 `GLSurfaceView`：
  - 使用独立 GLES3 上下文
  - 不依赖 Unity 使用 GLES 还是 Vulkan
  - 支持窗口拖动和按实际 ImGui 窗口范围捕获触摸
- 实验性 EGL/Vulkan Present Hook 后端
- nlohmann/json 配置系统
- Logcat 与公共 `Download` 目录文件日志
- C++ Runtime 静态链接，不依赖 `libc++_shared.so`
- 一键 PowerShell 构建，同时生成 SO 和 DEX

## 工作流程

```text
System.loadLibrary("Nebula")
            │
            ├── JNI_OnLoad / ELF constructor
            │
            └── Bootstrap thread
                    │
                    ├── 等待 libil2cpp.so
                    ├── 观察 il2cpp_init
                    ├── Attach IL2CPP thread
                    ├── 加载配置和 Mods
                    └── 启动 UI

NebulaLoader.attach(Activity)
            │
            ├── 创建 Download 日志
            └── 添加透明 GLSurfaceView
                    └── 独立 GLES3 + Dear ImGui
```

## 仓库结构

```text
NebulaIL2CPP/
├── app/
│   ├── build.gradle
│   ├── src/main/
│   │   ├── AndroidManifest.xml
│   │   └── java/dev/nebula/il2cpp/
│   │       ├── NebulaLoader.java
│   │       └── NebulaOverlayView.java
│   └── jni/
│       ├── CMakeLists.txt
│       ├── include/Nebula/
│       │   ├── Config/
│       │   ├── Core/
│       │   ├── Hook/
│       │   ├── Il2Cpp/
│       │   ├── Mods/
│       │   └── UI/
│       ├── src/
│       └── external/
│           ├── Dobby/
│           ├── imgui/
│           ├── json/
│           └── generated/il2cpp.h
├── build-nebula.ps1
├── MOD_DEVELOPMENT.md
├── NOTICE-THIRD_PARTY.md
└── README.md
```

`external/generated/il2cpp.h` 仅保留最小 opaque 声明。不要把几十 MB 的
完整 Il2CppDumper 头文件放进工程，否则 Android Studio C++ 索引会明显
变慢。开发 Mod 时优先使用 Runtime API 按名称动态解析。

## 环境要求

推荐版本：

- Windows PowerShell 5.1 或 PowerShell 7
- JDK 17 或更高版本
- Android SDK Platform 34
- Android NDK `26.1.10909125`
- CMake `3.22.1`
- Android Build Tools（脚本自动选择最高已安装版本）

使用 submodule 克隆第三方依赖：

```bash
git clone --recursive <your-repository-url>
```

如果已经普通克隆：

```bash
git submodule update --init --recursive
```

设置环境变量：

```powershell
[Environment]::SetEnvironmentVariable(
  "JAVA_HOME",
  "C:\Program Files\Android\Android Studio\jbr",
  "User")

[Environment]::SetEnvironmentVariable(
  "ANDROID_SDK_ROOT",
  "D:\Android\Sdk",
  "User")
```

重新打开终端后检查：

```powershell
$env:JAVA_HOME
$env:ANDROID_SDK_ROOT
```

如果没有设置 `ANDROID_SDK_ROOT`，构建脚本会回退读取 `ANDROID_HOME`。

## 快速构建

在仓库根目录执行：

```powershell
.\build-nebula.ps1 `
  -OutputDir ".\dist\arm64-v8a" `
  -CleanOutput
```

输出目录只包含最终产物：

```text
dist/arm64-v8a/
├── libNebula.so
└── classes.dex
```

脚本会自动：

1. 从环境变量查找 JDK 和 Android SDK；
2. 查找 CMake、Ninja、NDK、D8 和 Android Platform；
3. 在临时目录构建 Native 库；
4. 编译 Java Overlay 并通过 D8 生成 DEX；
5. 复制两个最终产物；
6. 删除 CMake、对象文件、Java class 和 JAR 等中间文件；
7. 输出大小与 SHA-256。

指定工具版本：

```powershell
.\build-nebula.ps1 `
  -CMakeVersion "3.22.1" `
  -NdkVersion "26.1.10909125" `
  -CompileSdk 34 `
  -BuildToolsVersion "36.1.0" `
  -OutputDir ".\dist\arm64-v8a" `
  -CleanOutput
```

如果指定版本未安装，CMake 和 NDK 会自动回退到 SDK 中最高的已安装版本。

### Android Studio

也可以在 Android Studio 中打开仓库根目录，通过 `app` Library 模块构建。
如果 Gradle 无法联网下载依赖，直接使用上面的 `build-nebula.ps1`，Native
和 DEX 构建不依赖 Gradle。

## 接入受控目标 APK

### 1. 放置 Native 库

将：

```text
libNebula.so
```

放入 APK：

```text
lib/arm64-v8a/libNebula.so
```

目标进程必须是 `arm64-v8a`。

### 2. 合并 DEX

构建产物 `classes.dex` 包含：

```text
dev.nebula.il2cpp.NebulaLoader
dev.nebula.il2cpp.NebulaOverlayView
```

不要覆盖目标 APK 已有 DEX。将它重命名为下一个未占用的名称，例如：

```text
classes4.dex
```

### 3. 从 Activity 启动

在目标 Activity（Unity 通常是 `UnityPlayerActivity`）完成原有
`onCreate()` 初始化后调用：

```java
NebulaLoader.attach(this);
```

对应 smali：

```smali
invoke-static {p0}, Ldev/nebula/il2cpp/NebulaLoader;->attach(Landroid/app/Activity;)V
```

`attach()` 会加载 SO、初始化 Download 文件日志并添加透明
`GLSurfaceView`。不需要再重复调用 `System.loadLibrary("Nebula")`。

修改 APK 后需要按 Android 平台要求重新签名。仓库不包含签名、注入或
完整性校验绕过逻辑。

## UI 操作

- 窗口可以拖动。
- 点击标题栏右上角 ImGui `X` 隐藏菜单。
- 音量上键尝试显示菜单。
- 音量下键尝试隐藏菜单。
- 菜单外触摸不由 ImGui 捕获。

部分 ROM 或 Activity 会提前消费音量键；这类目标需要在 Activity 的
`dispatchKeyEvent()` 中显式转发按键。ImGui 窗口关闭后，透明
`GLSurfaceView` 仍然存在，以便继续渲染和重新打开菜单。

## IL2CPP Runtime 使用

获取类：

```cpp
auto& il2cpp = Nebula::Il2Cpp::Get();
Il2CppClass* board = il2cpp.GetClass(
    "Assembly-CSharp", "", "Board");
```

获取方法地址：

```cpp
uintptr_t address = il2cpp.GetMethodAddress(
    "Assembly-CSharp", "", "Player", "TakeDamage", 1);
```

获取并修改实例字段：

```cpp
ptrdiff_t offset =
    il2cpp.GetFieldOffset(board, "theSun");

if (instance != nullptr && offset >= 0) {
    il2cpp.WriteField<int32_t>(instance, offset, 9999);
}
```

读取静态字段：

```cpp
FieldInfo* instanceField =
    il2cpp.GetField(board, "Instance");

void* instance = nullptr;
il2cpp.GetStaticFieldValue(instanceField, &instance);
```

更完整的 Mod 制作说明、Hook ABI 和模板见：

[MOD_DEVELOPMENT.md](MOD_DEVELOPMENT.md)

## Mod 生命周期

```cpp
class MyMod final : public Nebula::IMod {
public:
    void OnLoad() override;
    void OnUpdate() override;
    void OnGUI() override;
};
```

- `OnLoad()`：IL2CPP 初始化完成后调用一次，适合解析元数据和安装 Hook。
- `OnUpdate()`：每个 Overlay 帧调用，当前运行在渲染线程。
- `OnGUI()`：在 ImGui TabBar 中绘制菜单。

注册必须发生在 `ModManager::LoadAll()` 之前：

```cpp
Nebula::ModManager::Get().Register(
    std::make_unique<MyMod>());
```

从新的 Native 线程调用 IL2CPP API 前必须执行
`Il2Cpp::AttachCurrentThread()`。

## 内置示例

### ExampleMod

演示：

- 查找 `Player.TakeDamage(int)`
- Dobby Hook 与原函数调用
- 动态获取 `Player.health` 偏移
- Checkbox、Slider 和 JSON 配置

`Player` 是占位类型，通常不会直接匹配目标游戏，需要自行修改名称和 ABI。

### SunMod

面向当前示例目标，演示：

- 解析 `Board.Instance` 静态字段
- 每帧重新取得当前 Board
- 直接写入 `Board.theSun`
- 避免保存跨场景的旧对象指针

它不是通用 Unity Mod，其他项目应替换程序集、类型和字段名称。

## 配置

默认路径：

```text
/data/data/<package>/files/Nebula/config.json
```

无法取得包名时回退：

```text
/data/local/tmp/NebulaIL2CPP/config.json
```

支持 `bool`、`int` 和 `float`：

```cpp
Config::Get().GetBool("my_mod.enabled", false);
Config::Get().GetInt("my_mod.value", 100);
Config::Get().GetFloat("my_mod.speed", 1.0F);

Config::Get().SetBool("my_mod.enabled", true);
Config::Get().Save();
```

## 日志

Logcat Tag：

```text
NebulaIL2CPP
```

查看：

```bash
adb logcat -s NebulaIL2CPP
```

`NebulaLoader.attach()` 执行后，日志还会写入公共 Download 目录：

```text
Download/NebulaIL2CPP-yyyyMMdd-HHmmss.log
```

Android 10 及以上通过 MediaStore 写入，不需要传统存储权限。Android 9
及以下需要在目标 Manifest 中声明：

```xml
<uses-permission
    android:name="android.permission.WRITE_EXTERNAL_STORAGE"
    android:maxSdkVersion="28" />
```

SO constructor 到 Java `attach()` 之间的最早期日志只会进入 Logcat。

## 常见日志

正常启动通常包含：

```text
NebulaIL2CPP starting
Compatibility GLSurfaceView renderer selected
IL2CPP initialized
il2cpp_init completed
Bootstrap thread attached to IL2CPP
Loaded ... mod(s)
Compatibility GLSurfaceView surface created
Dear ImGui overlay initialized
First ImGui frame rendered
```

如果没有 `Compatibility GLSurfaceView surface created`，通常是 Overlay
View 没有正确加入 Activity，或 DEX 没有合并成功。

如果类、方法或字段解析失败，请检查程序集、命名空间、名称、大小写和参数
数量。Hook 后立即崩溃通常表示 Native 函数 ABI 不匹配。

## 已知限制

- 仅支持 `arm64-v8a`。
- 不同 Unity/IL2CPP 版本可能裁剪 Runtime 导出符号。
- `MethodInfo` 布局、泛型共享和方法 ABI 与 Unity 版本相关。
- EGL/Vulkan Hook 后端属于实验性路径，推荐使用独立 `GLSurfaceView`。
- 透明全屏 View 与不同 ROM/Unity Activity 的输入分发可能需要目标适配。
- 音量键可能被系统或 Activity 消费。
- Mod 必须自行处理 Unity 对象生命周期、线程安全和场景切换。
- 框架不是 APK、通用注入器或 root 管理工具。

## 第三方依赖

- [Dobby](https://github.com/jmpews/Dobby) — Apache-2.0
- [Dear ImGui](https://github.com/ocornut/imgui) — MIT
- [nlohmann/json](https://github.com/nlohmann/json) — MIT

固定版本和许可证说明见：

[NOTICE-THIRD_PARTY.md](NOTICE-THIRD_PARTY.md)

第三方源码目录中保留了完整许可证文本。重新分发源码或二进制时请遵守对应
许可证要求。

## 开源发布检查

在创建公开 GitHub 仓库前，请确认：

- 没有提交目标游戏的 APK、SO、metadata、dump、密钥或签名文件；
- 没有提交 `local.properties`、Android Studio 缓存和构建产物；
- 已根据你的发布意图添加仓库根目录 `LICENSE`；
- 已保留第三方版权和许可证声明；
- 示例中的目标游戏专用名称和逻辑符合你的公开发布范围。

当前仓库没有替你选择主项目许可证。添加 `LICENSE` 是正式开源发布前的
必要步骤。
