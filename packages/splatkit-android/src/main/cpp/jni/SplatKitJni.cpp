#include <jni.h>

#include <android/native_window_jni.h>

#include <memory>
#include <string>

#include "Engine.h"
#include "Log.h"

namespace {

splatkit::Engine* toEngine(jlong handle) { return reinterpret_cast<splatkit::Engine*>(handle); }

JavaVM* gVm = nullptr;

// Delivers engine events to NativeEngine.onNativeEvent on whatever thread raised them.
// Both threads that can raise one (the loader executor and the render HandlerThread)
// are Java threads, so GetEnv succeeds; a native thread would be attached for the call.
class EventBridge {
 public:
  EventBridge(JNIEnv* env, jobject engine) : engine_(env->NewGlobalRef(engine)) {
    jclass cls = env->GetObjectClass(engine);
    method_ = env->GetMethodID(cls, "onNativeEvent", "(ILjava/lang/String;I)V");
    env->DeleteLocalRef(cls);
  }
  ~EventBridge() {
    JNIEnv* env = nullptr;
    if (gVm != nullptr && gVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
      env->DeleteGlobalRef(engine_);
    }
  }
  void operator()(splatkit::Engine::Event event, const std::string& message, uint32_t count) const {
    if (gVm == nullptr || method_ == nullptr) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (gVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
      if (gVm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
      attached = true;
    }
    jstring text = env->NewStringUTF(message.c_str());
    env->CallVoidMethod(engine_, method_, static_cast<jint>(event), text, static_cast<jint>(count));
    env->DeleteLocalRef(text);
    if (attached) gVm->DetachCurrentThread();
  }

 private:
  jobject engine_;
  jmethodID method_ = nullptr;
};

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  gVm = vm;
  return JNI_VERSION_1_6;
}

JNIEXPORT jlong JNICALL Java_com_splatkit_NativeEngine_nativeCreate(JNIEnv* env, jobject thiz) {
  auto result = splatkit::Engine::create();
  if (!result) {
    LOGE("engine creation failed: %s", result.error().message.c_str());
    return 0;
  }
  splatkit::Engine* engine = result.value().release();
  // The bridge lives in the sink and dies with the engine.
  engine->setEventSink([bridge = std::make_shared<EventBridge>(env, thiz)](
                           splatkit::Engine::Event e, const std::string& m, uint32_t c) { (*bridge)(e, m, c); });
  return reinterpret_cast<jlong>(engine);
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

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeSetRenderScale(JNIEnv*, jobject,
                                                                                      jlong handle,
                                                                                      jfloat scale) {
  if (auto* engine = toEngine(handle)) engine->setRenderScale(scale);
}

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeSetSplatBudget(JNIEnv*, jobject,
                                                                                      jlong handle,
                                                                                      jint budget) {
  if (auto* engine = toEngine(handle)) engine->setSplatBudget(budget);
}

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeSetMaxShDegree(JNIEnv*, jobject,
                                                                                      jlong handle,
                                                                                      jint degree) {
  if (auto* engine = toEngine(handle)) engine->setMaxShDegree(degree);
}

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeStartBenchmark(JNIEnv*, jobject,
                                                                                      jlong handle,
                                                                                      jfloat seconds) {
  if (auto* engine = toEngine(handle)) engine->startBenchmark(seconds);
}

extern "C" JNIEXPORT jstring JNICALL Java_com_splatkit_NativeEngine_nativeGpuDescription(JNIEnv* env,
                                                                                        jobject,
                                                                                        jlong handle) {
  auto* engine = toEngine(handle);
  return env->NewStringUTF(engine ? engine->gpuDescription().c_str() : "");
}

// Fills out[0..6]: fps, frame ms, sort ms, splat count, walking (0/1), motion (0/1), gpu ms.
extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeStats(JNIEnv* env, jobject,
                                                                            jlong handle,
                                                                            jfloatArray out) {
  auto* engine = toEngine(handle);
  if (engine == nullptr || out == nullptr || env->GetArrayLength(out) < 7) return;
  const splatkit::Engine::Stats s = engine->stats();
  const float values[7] = {s.fps, s.frameMillis, s.sortMillis, static_cast<float>(s.splatCount),
                           s.walking ? 1.0f : 0.0f, s.motion ? 1.0f : 0.0f, s.gpuMillis};
  env->SetFloatArrayRegion(out, 0, 7, values);
}

extern "C" JNIEXPORT void JNICALL Java_com_splatkit_NativeEngine_nativeSetMotionEnabled(JNIEnv*, jobject,
                                                                                       jlong handle,
                                                                                       jboolean enabled) {
  if (auto* engine = toEngine(handle)) engine->setMotionEnabled(enabled == JNI_TRUE);
}
