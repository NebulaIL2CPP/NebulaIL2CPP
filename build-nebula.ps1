[CmdletBinding()]
param(
    [string]$OutputDir = "E:\Work\NebulaIL2CPP\dist\arm64-v8a",
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release",
    [string]$JavaHome = $env:JAVA_HOME,
    [string]$AndroidSdkRoot = $env:ANDROID_SDK_ROOT,
    [string]$CMakeVersion = "3.22.1",
    [string]$NdkVersion = "26.1.10909125",
    [string]$BuildToolsVersion = "",
    [int]$CompileSdk = 34,
    [string]$AndroidAbi = "arm64-v8a",
    [int]$AndroidPlatform = 24,
    [switch]$CleanOutput
)

$ErrorActionPreference = "Stop"

$workspaceRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$sourceDir = Join-Path $workspaceRoot "app\jni"
$stagingDir = Join-Path $workspaceRoot ".nebula-build\$($AndroidAbi.ToLowerInvariant())-$($Configuration.ToLowerInvariant())"
$outputPath = [IO.Path]::GetFullPath($OutputDir)
$artifactName = "libNebula.so"
$artifactPath = Join-Path $outputPath $artifactName
$dexName = "classes.dex"
$dexPath = Join-Path $outputPath $dexName

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
}

function Resolve-VersionDirectory(
    [string]$Parent,
    [string]$Preferred,
    [string]$Label
) {
    if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
        throw "$Label directory not found: $Parent"
    }
    if (-not [string]::IsNullOrWhiteSpace($Preferred)) {
        $preferredPath = Join-Path $Parent $Preferred
        if (Test-Path -LiteralPath $preferredPath -PathType Container) {
            return (Resolve-Path -LiteralPath $preferredPath).Path
        }
    }
    $candidate = Get-ChildItem -LiteralPath $Parent -Directory |
        Sort-Object {
            try { [version]$_.Name } catch { [version]"0.0" }
        } -Descending |
        Select-Object -First 1
    if ($null -eq $candidate) {
        throw "No installed $Label version found under: $Parent"
    }
    if (-not [string]::IsNullOrWhiteSpace($Preferred)) {
        Write-Warning (
            "$Label version '$Preferred' was not found; " +
            "using '$($candidate.Name)'")
    }
    return $candidate.FullName
}

if ([string]::IsNullOrWhiteSpace($JavaHome)) {
    throw "JAVA_HOME is not set. Set it to a JDK directory."
}
if ([string]::IsNullOrWhiteSpace($AndroidSdkRoot)) {
    $AndroidSdkRoot = $env:ANDROID_HOME
}
if ([string]::IsNullOrWhiteSpace($AndroidSdkRoot)) {
    throw "ANDROID_SDK_ROOT or ANDROID_HOME is not set."
}

$JavaHome = [IO.Path]::GetFullPath($JavaHome)
$AndroidSdkRoot = [IO.Path]::GetFullPath($AndroidSdkRoot)
$JavaExe = Join-Path $JavaHome "bin\java.exe"
$JavacExe = Join-Path $JavaHome "bin\javac.exe"
$JarExe = Join-Path $JavaHome "bin\jar.exe"

$cmakeRoot = Resolve-VersionDirectory `
    (Join-Path $AndroidSdkRoot "cmake") $CMakeVersion "CMake"
$ndkRoot = Resolve-VersionDirectory `
    (Join-Path $AndroidSdkRoot "ndk") $NdkVersion "NDK"
$buildToolsRoot = Resolve-VersionDirectory `
    (Join-Path $AndroidSdkRoot "build-tools") `
    $BuildToolsVersion "Android Build Tools"

$platformName = "android-$CompileSdk"
$platformRoot = Join-Path `
    (Join-Path $AndroidSdkRoot "platforms") $platformName
if (-not (Test-Path -LiteralPath $platformRoot -PathType Container)) {
    $platformRoot = Resolve-VersionDirectory `
        (Join-Path $AndroidSdkRoot "platforms") "" "Android Platform"
    Write-Warning (
        "Android platform '$platformName' was not found; using " +
        "'$(Split-Path $platformRoot -Leaf)'")
}

$CMakeExe = Join-Path $cmakeRoot "bin\cmake.exe"
$NinjaExe = Join-Path $cmakeRoot "bin\ninja.exe"
$AndroidToolchain = Join-Path `
    $ndkRoot "build\cmake\android.toolchain.cmake"
$D8Jar = Join-Path $buildToolsRoot "lib\d8.jar"
$AndroidJar = Join-Path $platformRoot "android.jar"

Require-File $CMakeExe "CMake"
Require-File $NinjaExe "Ninja"
Require-File $AndroidToolchain "Android NDK toolchain"
Require-File $JavaExe "Java"
Require-File $JavacExe "Javac"
Require-File $JarExe "Jar"
Require-File $D8Jar "D8"
Require-File $AndroidJar "Android platform jar"
Require-File (Join-Path $sourceDir "CMakeLists.txt") "Nebula CMakeLists.txt"

Write-Host "JAVA_HOME       : $JavaHome"
Write-Host "Android SDK     : $AndroidSdkRoot"
Write-Host "CMake           : $cmakeRoot"
Write-Host "NDK             : $ndkRoot"
Write-Host "Build Tools     : $buildToolsRoot"
Write-Host "Android Platform: $platformRoot"
Write-Host ""

if ((Test-Path -LiteralPath $outputPath) -and $CleanOutput) {
    # The caller explicitly requested cleanup of this exact output directory.
    Get-ChildItem -LiteralPath $outputPath -Force |
        Remove-Item -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagingDir | Out-Null

Write-Host "Configuring NebulaIL2CPP ($Configuration, $AndroidAbi)..."
& $CMakeExe `
    -S $sourceDir `
    -B $stagingDir `
    -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$NinjaExe" `
    "-DCMAKE_TOOLCHAIN_FILE=$AndroidToolchain" `
    "-DANDROID_ABI=$AndroidAbi" `
    "-DANDROID_PLATFORM=android-$AndroidPlatform" `
    "-DANDROID_STL=c++_static" `
    "-DCMAKE_ANDROID_STL_TYPE=c++_static" `
    "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE"
}

Write-Host "Building Nebula..."
& $CMakeExe --build $stagingDir --target Nebula --parallel 8
if ($LASTEXITCODE -ne 0) {
    throw "Native build failed with exit code $LASTEXITCODE"
}

$builtArtifact = Join-Path $stagingDir $artifactName
Require-File $builtArtifact "Build artifact"
Copy-Item -LiteralPath $builtArtifact -Destination $artifactPath -Force

Write-Host "Compiling Java compatibility overlay..."
$javaSourceDir = Join-Path $workspaceRoot "app\src\main\java\dev\nebula\il2cpp"
$javaClassesDir = Join-Path $stagingDir "java-classes"
$javaJar = Join-Path $stagingDir "nebula-overlay.jar"
$dexOutputDir = Join-Path $stagingDir "dex"
$javaSources = @(
    (Join-Path $javaSourceDir "NebulaLoader.java"),
    (Join-Path $javaSourceDir "NebulaOverlayView.java")
)
foreach ($source in $javaSources) {
    Require-File $source "Java source"
}
New-Item -ItemType Directory -Force -Path $javaClassesDir | Out-Null
New-Item -ItemType Directory -Force -Path $dexOutputDir | Out-Null

& $JavacExe `
    -source 8 `
    -target 8 `
    -cp $AndroidJar `
    -d $javaClassesDir `
    $javaSources
if ($LASTEXITCODE -ne 0) {
    throw "Java compilation failed with exit code $LASTEXITCODE"
}

& $JarExe cf $javaJar -C $javaClassesDir .
if ($LASTEXITCODE -ne 0) {
    throw "Java archive creation failed with exit code $LASTEXITCODE"
}

& $JavaExe `
    -cp $D8Jar `
    com.android.tools.r8.D8 `
    --min-api $AndroidPlatform `
    --lib $AndroidJar `
    --output $dexOutputDir `
    $javaJar
if ($LASTEXITCODE -ne 0) {
    throw "DEX compilation failed with exit code $LASTEXITCODE"
}

$builtDex = Join-Path $dexOutputDir $dexName
Require-File $builtDex "DEX artifact"
Copy-Item -LiteralPath $builtDex -Destination $dexPath -Force

# Keep the requested output directory limited to final artifacts when it is a
# dedicated directory. Without -CleanOutput, unrelated files are preserved.
Remove-Item -LiteralPath $stagingDir -Recurse -Force

$nativeHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash
$nativeSize = (Get-Item -LiteralPath $artifactPath).Length
$dexHash = (Get-FileHash -LiteralPath $dexPath -Algorithm SHA256).Hash
$dexSize = (Get-Item -LiteralPath $dexPath).Length
Write-Host ""
Write-Host "Build complete."
Write-Host "Native   : $artifactPath"
Write-Host "Size     : $nativeSize bytes"
Write-Host "SHA-256  : $nativeHash"
Write-Host ""
Write-Host "DEX      : $dexPath"
Write-Host "Size     : $dexSize bytes"
Write-Host "SHA-256  : $dexHash"
