#include <jni.h>

#include "Nebula/Core/Export.h"
#include "Nebula/Core/Log.h"
#include "Nebula/Core/Runtime.h"
#include "Nebula/UI/Overlay.h"

namespace {

void StartNebula() {
    Nebula::Runtime::Get().Start();
}

} // namespace

extern "C" NEBULA_EXPORT JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM*, void*) {
    StartNebula();
    return JNI_VERSION_1_6;
}

extern "C" NEBULA_EXPORT void Nebula_Initialize() {
    StartNebula();
}

extern "C" NEBULA_EXPORT JNIEXPORT void JNICALL
Java_dev_nebula_il2cpp_NebulaOverlayView_nativeUseCompatibilityRenderer(
    JNIEnv*, jclass) {
    Nebula::Overlay::UseExternalSurface();
}

extern "C" NEBULA_EXPORT JNIEXPORT void JNICALL
Java_dev_nebula_il2cpp_NebulaOverlayView_nativeSurfaceCreated(
    JNIEnv*, jclass) {
    NEBULA_LOGI("Compatibility renderer nativeSurfaceCreated entered");
    Nebula::Overlay::InitializeExternalOpenGL();
}

extern "C" NEBULA_EXPORT JNIEXPORT void JNICALL
Java_dev_nebula_il2cpp_NebulaOverlayView_nativeRender(
    JNIEnv*, jclass, jint width, jint height) {
    static bool firstFrame = true;
    if (firstFrame) {
        firstFrame = false;
        NEBULA_LOGI(
            "Compatibility renderer nativeRender entered (%dx%d)",
            width, height);
    }
    Nebula::Overlay::RenderExternalOpenGL(
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));
}

extern "C" NEBULA_EXPORT JNIEXPORT jboolean JNICALL
Java_dev_nebula_il2cpp_NebulaOverlayView_nativeTouch(
    JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
    return Nebula::Overlay::SubmitExternalTouch(action, x, y)
               ? JNI_TRUE
               : JNI_FALSE;
}

extern "C" NEBULA_EXPORT JNIEXPORT void JNICALL
Java_dev_nebula_il2cpp_NebulaOverlayView_nativeSetVisible(
    JNIEnv*, jclass, jboolean visible) {
    Nebula::Overlay::SetVisible(visible == JNI_TRUE);
}

extern "C" NEBULA_EXPORT JNIEXPORT jboolean JNICALL
Java_dev_nebula_il2cpp_NebulaLoader_nativeSetLogFileDescriptor(
    JNIEnv*, jclass, jint fileDescriptor) {
    const bool ready =
        Nebula::SetLogFileDescriptor(fileDescriptor);
    if (ready) {
        NEBULA_LOGI("File logging initialized in public Download directory");
    }
    return ready ? JNI_TRUE : JNI_FALSE;
}

__attribute__((constructor)) static void NebulaConstructor() {
    StartNebula();
}
