// The JNI entry points of com.splatkit.engine.SplatEngine. Each one checks the handle
// and forwards to the SplatEngine; the layouts of the float arrays shared with Kotlin are
// documented where they are filled.

#include <jni.h>

#include <android/native_window_jni.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "engine/AndroidEngine.h"
#include "splatkit/Log.h"

// `SPLATKIT_JNI(void, nativeLook)(JNIEnv*, jobject, ...)` declares the exported symbol
// the JVM binds to `SplatEngine.nativeLook`. The package is part of the name.
#define SPLATKIT_JNI(returnType, name) \
  extern "C" JNIEXPORT returnType JNICALL Java_com_splatkit_engine_SplatEngine_##name

namespace {

constexpr jsize kPoseFloats = 5;
constexpr jsize kStatsFloats = 7;
constexpr jsize kAttitudeFloats = 9;

// The handle Kotlin holds is the host's address; JNI has no other way to carry it.
splatkit::AndroidEngine* toHost(jlong handle) {
  return reinterpret_cast<splatkit::AndroidEngine*>(handle);  // NOLINT(performance-no-int-to-ptr)
}

splatkit::SplatEngine* toEngine(jlong handle) {
  auto* host = toHost(handle);
  return host != nullptr ? &host->engine() : nullptr;
}

JavaVM* gVm = nullptr;

// Delivers engine events to SplatEngine.onNativeEvent on whatever thread raised them.
// Both threads that can raise one (the loader executor and the render HandlerThread)
// are Java threads, so GetEnv succeeds; a native thread would be attached for the call.
class EventBridge {
 public:
  EventBridge(JNIEnv* env, jobject engine) : engine_(env->NewGlobalRef(engine)) {
    jclass cls = env->GetObjectClass(engine);
    method_ = env->GetMethodID(cls, "onNativeEvent", "(ILjava/lang/String;I)V");
    env->DeleteLocalRef(cls);
    // A signature drift between the .so and SplatEngine would otherwise surface as a
    // pending exception thrown out of a later render call.
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      method_ = nullptr;
      LOGE("SplatEngine.onNativeEvent not found: events will not be delivered");
    }
  }
  ~EventBridge() {
    JNIEnv* env = nullptr;
    if (gVm != nullptr && gVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
      env->DeleteGlobalRef(engine_);
    }
  }

  EventBridge(const EventBridge&) = delete;
  EventBridge& operator=(const EventBridge&) = delete;

  void operator()(splatkit::SplatEngine::Event event, const std::string& message,
                  uint32_t count) const {
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

// Hands the bytes of a Java array to `use` without copying them for longer than the call.
template <typename Use>
void withBytes(JNIEnv* env, jbyteArray bytes, Use use) {
  if (bytes == nullptr) return;
  const jsize size = env->GetArrayLength(bytes);
  jbyte* data = env->GetByteArrayElements(bytes, nullptr);
  if (data == nullptr) return;
  use(reinterpret_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size));
  env->ReleaseByteArrayElements(bytes, data, JNI_ABORT);
}

template <typename Use>
void withUtf8(JNIEnv* env, jstring text, Use use) {
  if (text == nullptr) return;
  const char* chars = env->GetStringUTFChars(text, nullptr);
  if (chars == nullptr) return;
  use(std::string(chars));
  env->ReleaseStringUTFChars(text, chars);
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  gVm = vm;
  return JNI_VERSION_1_6;
}

// Lifetime.

SPLATKIT_JNI(jlong, nativeCreate)(JNIEnv* env, jobject thiz) {
  auto result = splatkit::AndroidEngine::create();
  if (!result) {
    LOGE("engine creation failed: %s", result.error().message.c_str());
    return 0;
  }
  splatkit::AndroidEngine* host = result.value().release();
  // The bridge lives in the sink and dies with the engine.
  host->engine().setEventSink([bridge = std::make_shared<EventBridge>(env, thiz)](
                                  splatkit::SplatEngine::Event e, const std::string& m,
                                  uint32_t c) { (*bridge)(e, m, c); });
  return reinterpret_cast<jlong>(host);
}

SPLATKIT_JNI(void, nativeDestroy)(JNIEnv*, jobject, jlong handle) {
  delete toHost(handle);
}

SPLATKIT_JNI(jstring, nativeGpuDescription)(JNIEnv* env, jobject, jlong handle) {
  auto* engine = toEngine(handle);
  return env->NewStringUTF(engine != nullptr ? engine->gpuDescription().c_str() : "");
}

// Surface and frames.

SPLATKIT_JNI(void, nativeSetSurface)(JNIEnv* env, jobject, jlong handle, jobject surface) {
  auto* host = toHost(handle);
  if (host == nullptr) return;
  if (surface == nullptr) {
    host->setWindow(nullptr);
    return;
  }
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) LOGE("the Surface has no native window; the view stays blank");
  host->setWindow(window);
  // The engine holds its own reference; drop the one fromSurface gave us.
  if (window != nullptr) ANativeWindow_release(window);
}

SPLATKIT_JNI(void, nativeSurfaceResized)(JNIEnv*, jobject, jlong handle, jint width, jint height) {
  if (auto* host = toHost(handle)) {
    host->onSurfaceResized(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  }
}

SPLATKIT_JNI(void, nativeRender)(JNIEnv*, jobject, jlong handle, jlong frameTimeNanos) {
  if (auto* engine = toEngine(handle)) engine->render(frameTimeNanos);
}

// Loading.

SPLATKIT_JNI(void, nativeLoadWorld)(JNIEnv* env, jobject, jlong handle, jbyteArray bytes) {
  auto* engine = toEngine(handle);
  if (engine == nullptr) return;
  withBytes(env, bytes, [engine](const std::uint8_t* data, std::size_t size) {
    engine->loadWorld(data, size);
  });
}

SPLATKIT_JNI(void, nativeLoadCollider)(JNIEnv* env, jobject, jlong handle, jbyteArray bytes) {
  auto* engine = toEngine(handle);
  if (engine == nullptr) return;
  withBytes(env, bytes, [engine](const std::uint8_t* data, std::size_t size) {
    engine->loadCollider(data, size);
  });
}

SPLATKIT_JNI(void, nativeLoadWorldFile)(JNIEnv* env, jobject, jlong handle, jstring path) {
  auto* engine = toEngine(handle);
  if (engine == nullptr) return;
  withUtf8(env, path, [engine](const std::string& p) { engine->loadWorldFile(p); });
}

SPLATKIT_JNI(void, nativeLoadTiledWorldFile)(JNIEnv* env, jobject, jlong handle, jstring path) {
  auto* engine = toEngine(handle);
  if (engine == nullptr) return;
  withUtf8(env, path, [engine](const std::string& p) { engine->loadTiledWorldFile(p); });
}

SPLATKIT_JNI(void, nativeLoadColliderFile)(JNIEnv* env, jobject, jlong handle, jstring path) {
  auto* engine = toEngine(handle);
  if (engine == nullptr) return;
  withUtf8(env, path, [engine](const std::string& p) { engine->loadColliderFile(p); });
}

// Camera and input.

SPLATKIT_JNI(void, nativeSetCameraPose)
(JNIEnv*, jobject, jlong handle, jfloat x, jfloat y, jfloat z, jfloat yaw, jfloat pitch) {
  if (auto* engine = toEngine(handle)) engine->setCameraPose({x, y, z, yaw, pitch});
}

// Fills out[0..4]: x, y, z, yaw, pitch.
SPLATKIT_JNI(void, nativeCameraPose)(JNIEnv* env, jobject, jlong handle, jfloatArray out) {
  auto* engine = toEngine(handle);
  if (engine == nullptr || out == nullptr || env->GetArrayLength(out) < kPoseFloats) return;
  const splatkit::CameraPose p = engine->cameraPose();
  const float values[kPoseFloats] = {p.x, p.y, p.z, p.yaw, p.pitch};
  env->SetFloatArrayRegion(out, 0, kPoseFloats, values);
}

SPLATKIT_JNI(void, nativeLook)(JNIEnv*, jobject, jlong handle, jfloat deltaYaw, jfloat deltaPitch) {
  if (auto* engine = toEngine(handle)) engine->look(deltaYaw, deltaPitch);
}

SPLATKIT_JNI(void, nativeWalk)(JNIEnv*, jobject, jlong handle, jfloat forward, jfloat right) {
  if (auto* engine = toEngine(handle)) engine->walk(forward, right);
}

SPLATKIT_JNI(void, nativeSetVelocity)
(JNIEnv*, jobject, jlong handle, jfloat forward, jfloat right) {
  if (auto* engine = toEngine(handle)) engine->setVelocity(forward, right);
}

// `rowMajor` is the 3x3 device to reference rotation as Android hands it out.
SPLATKIT_JNI(void, nativeSetAttitude)(JNIEnv* env, jobject, jlong handle, jfloatArray rowMajor) {
  auto* engine = toEngine(handle);
  if (engine == nullptr || rowMajor == nullptr || env->GetArrayLength(rowMajor) < kAttitudeFloats) {
    return;
  }
  float m[kAttitudeFloats];
  env->GetFloatArrayRegion(rowMajor, 0, kAttitudeFloats, m);
  engine->setAttitude(m);
}

SPLATKIT_JNI(void, nativeSetMotionEnabled)(JNIEnv*, jobject, jlong handle, jboolean enabled) {
  if (auto* engine = toEngine(handle)) engine->setMotionEnabled(enabled == JNI_TRUE);
}

// Quality settings.

SPLATKIT_JNI(void, nativeSetRenderScale)(JNIEnv*, jobject, jlong handle, jfloat scale) {
  if (auto* engine = toEngine(handle)) engine->setRenderScale(scale);
}

SPLATKIT_JNI(void, nativeSetCullMargin)(JNIEnv*, jobject, jlong handle, jfloat degrees) {
  if (auto* engine = toEngine(handle)) engine->setCullMargin(degrees);
}

SPLATKIT_JNI(void, nativeSetLinearBlending)(JNIEnv*, jobject, jlong handle, jboolean linear) {
  if (auto* engine = toEngine(handle)) engine->setLinearBlending(linear == JNI_TRUE);
}

SPLATKIT_JNI(void, nativeSetSplatBudget)(JNIEnv*, jobject, jlong handle, jint budget) {
  if (auto* engine = toEngine(handle)) engine->setSplatBudget(budget);
}

SPLATKIT_JNI(void, nativeSetResidencyBudget)(JNIEnv*, jobject, jlong handle, jint splats) {
  if (auto* engine = toEngine(handle)) engine->setResidencyBudget(splats);
}

SPLATKIT_JNI(void, nativeSetMaxShDegree)(JNIEnv*, jobject, jlong handle, jint degree) {
  if (auto* engine = toEngine(handle)) engine->setMaxShDegree(degree);
}

SPLATKIT_JNI(void, nativeSetShDegree)(JNIEnv*, jobject, jlong handle, jint degree) {
  if (auto* engine = toEngine(handle)) engine->setShDegree(degree);
}

// Diagnostics.

SPLATKIT_JNI(void, nativeStartBenchmark)(JNIEnv*, jobject, jlong handle, jfloat seconds) {
  if (auto* engine = toEngine(handle)) engine->startBenchmark(seconds);
}

// Legacy out[0..6] is unchanged; [7..10] adds drawn/compute/nonempty/hardware counts.
SPLATKIT_JNI(void, nativeStats)(JNIEnv* env, jobject, jlong handle, jfloatArray out) {
  constexpr jsize kExtendedStatsFloats = 11;
  auto* engine = toEngine(handle);
  if (engine == nullptr || out == nullptr || env->GetArrayLength(out) < kStatsFloats) return;
  const splatkit::Stats s = engine->stats();
  // Float transport represents every integer through 2^24 exactly; larger counts can round.
  const float values[kExtendedStatsFloats] = {s.fps,
                                              s.frameMillis,
                                              s.gpuMillis,
                                              s.sortMillis,
                                              static_cast<float>(s.splatCount),
                                              s.walking ? 1.0f : 0.0f,
                                              s.motion ? 1.0f : 0.0f,
                                              static_cast<float>(s.drawnSplatCount),
                                              static_cast<float>(s.computeTileCount),
                                              static_cast<float>(s.nonemptyComputeTileCount),
                                              static_cast<float>(s.hardwareTileCount)};
  const jsize count =
      env->GetArrayLength(out) >= kExtendedStatsFloats ? kExtendedStatsFloats : kStatsFloats;
  env->SetFloatArrayRegion(out, 0, count, values);
}
