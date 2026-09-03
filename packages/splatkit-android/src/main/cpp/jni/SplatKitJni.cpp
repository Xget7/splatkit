#include <jni.h>

#include <android/native_window_jni.h>

#include "Engine.h"
#include "Log.h"

namespace {

splatkit::Engine* toEngine(jlong handle) { return reinterpret_cast<splatkit::Engine*>(handle); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_splatkit_NativeEngine_nativeCreate(JNIEnv*, jobject) {
  auto result = splatkit::Engine::create();
  if (!result) {
    LOGE("engine creation failed: %s", result.error().message.c_str());
    return 0;
  }
  return reinterpret_cast<jlong>(result.value().release());
}

JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeDestroy(JNIEnv*, jobject, jlong handle) {
  delete toEngine(handle);
}

JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeSetSurface(JNIEnv* env, jobject,
                                                                       jlong handle,
                                                                       jobject surface) {
  splatkit::Engine* engine = toEngine(handle);
  if (engine == nullptr) return;
  if (surface == nullptr) {
    engine->setWindow(nullptr);
    return;
  }
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  engine->setWindow(window);
  // The engine holds its own reference; drop the one fromSurface gave us.
  if (window != nullptr) ANativeWindow_release(window);
}

JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeSurfaceResized(JNIEnv*, jobject,
                                                                           jlong handle, jint width,
                                                                           jint height) {
  if (splatkit::Engine* engine = toEngine(handle)) {
    engine->onSurfaceResized(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  }
}

JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeRender(JNIEnv*, jobject, jlong handle,
                                                                   jlong frameTimeNanos) {
  splatkit::Engine* engine = toEngine(handle);
  if (engine != nullptr) engine->render(frameTimeNanos);
}

}  // extern "C"

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeLoadWorld(JNIEnv* env, jobject,
                                                                                jlong handle,
                                                                                jbyteArray bytes) {
  splatkit::Engine* engine = toEngine(handle);
  if (engine == nullptr || bytes == nullptr) return;
  const jsize size = env->GetArrayLength(bytes);
  jbyte* data = env->GetByteArrayElements(bytes, nullptr);
  if (data == nullptr) return;
  engine->loadWorld(reinterpret_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size));
  env->ReleaseByteArrayElements(bytes, data, JNI_ABORT);
}

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeLoadCollider(JNIEnv* env, jobject,
                                                                                   jlong handle,
                                                                                   jbyteArray bytes) {
  splatkit::Engine* engine = toEngine(handle);
  if (engine == nullptr || bytes == nullptr) return;
  const jsize size = env->GetArrayLength(bytes);
  jbyte* data = env->GetByteArrayElements(bytes, nullptr);
  if (data == nullptr) return;
  engine->loadCollider(reinterpret_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size));
  env->ReleaseByteArrayElements(bytes, data, JNI_ABORT);
}

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeLook(JNIEnv*, jobject, jlong handle,
                                                                           jfloat deltaYaw, jfloat deltaPitch) {
  if (auto* engine = toEngine(handle)) engine->look(deltaYaw, deltaPitch);
}

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeWalk(JNIEnv*, jobject, jlong handle,
                                                                           jfloat forward, jfloat right) {
  if (auto* engine = toEngine(handle)) engine->walk(forward, right);
}

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeSetAttitude(JNIEnv* env, jobject,
                                                                                  jlong handle,
                                                                                  jfloatArray rowMajor) {
  auto* engine = toEngine(handle);
  if (engine == nullptr || rowMajor == nullptr || env->GetArrayLength(rowMajor) < 9) return;
  float m[9];
  env->GetFloatArrayRegion(rowMajor, 0, 9, m);
  engine->setAttitude(m);
}

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeSetVelocity(JNIEnv*, jobject,
                                                                                  jlong handle,
                                                                                  jfloat forward,
                                                                                  jfloat right) {
  if (auto* engine = toEngine(handle)) engine->setVelocity(forward, right);
}

extern "C" JNIEXPORT jstring JNICALL Java_com_splatkit_NativeEngine_nativeGpuDescription(JNIEnv* env,
                                                                                        jobject,
                                                                                        jlong handle) {
  auto* engine = toEngine(handle);
  return env->NewStringUTF(engine ? engine->gpuDescription().c_str() : "");
}

// Fills out[0..5]: fps, frame ms, sort ms, splat count, walking (0/1), motion (0/1).
extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeStats(JNIEnv* env, jobject,
                                                                            jlong handle,
                                                                            jfloatArray out) {
  auto* engine = toEngine(handle);
  if (engine == nullptr || out == nullptr || env->GetArrayLength(out) < 6) return;
  const splatkit::Engine::Stats s = engine->stats();
  const float values[6] = {s.fps, s.frameMillis, s.sortMillis, static_cast<float>(s.splatCount),
                           s.walking ? 1.0f : 0.0f, s.motion ? 1.0f : 0.0f};
  env->SetFloatArrayRegion(out, 0, 6, values);
}

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeSetMotionEnabled(JNIEnv*, jobject,
                                                                                       jlong handle,
                                                                                       jboolean enabled) {
  if (auto* engine = toEngine(handle)) engine->setMotionEnabled(enabled == JNI_TRUE);
}
