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

  void operator()(int event, const std::string& message, uint32_t count) const {
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
  host->setEventSink([bridge = std::make_shared<EventBridge>(env, thiz)](
                         int e, const std::string& m, uint32_t c) { (*bridge)(e, m, c); });
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
  if (auto* host = toHost(handle)) host->render(frameTimeNanos);
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

SPLATKIT_JNI(jboolean, nativeSetCharacter)
(JNIEnv*, jobject, jlong handle, jfloat eyeHeight, jfloat bodyRadius, jfloat stepHeight) {
  auto* engine = toEngine(handle);
  if (engine == nullptr) return JNI_FALSE;
  splat::CharacterSettings character = engine->character();
  character.eyeHeight = eyeHeight;
  character.bodyRadius = bodyRadius;
  character.stepHeight = stepHeight;
  return engine->setCharacter(character) ? JNI_TRUE : JNI_FALSE;
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

// Policy and capabilities. Both arrays are double: every field is a count, flag or float.

namespace {
constexpr jsize kPolicyFields = 9;  // see the index list below
// accepted, preparationFailed, then the effective policy
constexpr jsize kPolicyResolutionFields = kPolicyFields + 2;
constexpr jsize kCapabilityFields = 30;  // limits, flags, support then fallback

// 0 raster, 1 tileSize, 2 lodErrorPixels, 3 alphaThreshold, 4 subpixelThreshold,
// 5 enableFrustumCulling, 6 enableHiZOcclusion, 7 enableEarlyTermination, 8 sortDepth.
splatkit::RenderPolicy policyFromDoubles(const double* v) {
  splatkit::RenderPolicy p;
  p.raster = static_cast<splatkit::RasterStrategy>(static_cast<uint32_t>(v[0]));
  p.tileSize = static_cast<uint32_t>(v[1]);
  p.lodErrorPixels = static_cast<float>(v[2]);
  p.alphaThreshold = static_cast<float>(v[3]);
  p.subpixelThreshold = static_cast<float>(v[4]);
  p.enableFrustumCulling = v[5] != 0.0;
  p.enableHiZOcclusion = v[6] != 0.0;
  p.enableEarlyTermination = v[7] != 0.0;
  p.sortDepth = static_cast<splatkit::SortKeyBits>(static_cast<uint32_t>(v[8]));
  return p;
}

void policyToDoubles(const splatkit::RenderPolicy& p, double* v) {
  v[0] = static_cast<double>(static_cast<uint32_t>(p.raster));
  v[1] = static_cast<double>(p.tileSize);
  v[2] = static_cast<double>(p.lodErrorPixels);
  v[3] = static_cast<double>(p.alphaThreshold);
  v[4] = static_cast<double>(p.subpixelThreshold);
  v[5] = p.enableFrustumCulling ? 1.0 : 0.0;
  v[6] = p.enableHiZOcclusion ? 1.0 : 0.0;
  v[7] = p.enableEarlyTermination ? 1.0 : 0.0;
  v[8] = static_cast<double>(static_cast<uint32_t>(p.sortDepth));
}

void supportToDoubles(const splatkit::RenderPolicySupport& s, double* v) {
  v[0] = s.raster ? 1.0 : 0.0;
  v[1] = s.tileSize ? 1.0 : 0.0;
  v[2] = s.lodErrorPixels ? 1.0 : 0.0;
  v[3] = s.alphaThreshold ? 1.0 : 0.0;
  v[4] = s.subpixelThreshold ? 1.0 : 0.0;
  v[5] = s.enableFrustumCulling ? 1.0 : 0.0;
  v[6] = s.enableHiZOcclusion ? 1.0 : 0.0;
  v[7] = s.enableEarlyTermination ? 1.0 : 0.0;
  v[8] = s.sortDepth ? 1.0 : 0.0;
  v[9] = static_cast<double>(s.tileSizeMask);
  v[10] = static_cast<double>(s.minLodErrorPixels);
  v[11] = static_cast<double>(s.maxLodErrorPixels);
  v[12] = static_cast<double>(s.minSubpixelThreshold);
  v[13] = static_cast<double>(s.maxSubpixelThreshold);
}
}  // namespace

// out[0..29]: limits, feature flags, policy support [7..20], fallback policy [21..29].
SPLATKIT_JNI(void, nativeCapabilities)(JNIEnv* env, jobject, jlong handle, jdoubleArray out) {
  const splatkit::SplatEngine* engine = toEngine(handle);
  if (engine == nullptr || out == nullptr || env->GetArrayLength(out) < kCapabilityFields) return;
  const splatkit::DeviceCapabilities caps = engine->deviceCapabilities();
  double values[kCapabilityFields]{};
  values[0] = static_cast<double>(caps.limits.maxLodCapacitySplats);
  values[1] = static_cast<double>(caps.limits.minResidencyCapacitySplats);
  values[2] = static_cast<double>(caps.limits.maxResidencyCapacitySplats);
  values[3] = caps.supportsComputeTiles ? 1.0 : 0.0;
  values[4] = caps.supportsHiZOcclusion ? 1.0 : 0.0;
  values[5] = caps.supportsSubgroups ? 1.0 : 0.0;
  values[6] = static_cast<double>(caps.maxTextureDimension);
  supportToDoubles(caps.policy, values + 7);
  policyToDoubles(caps.policy.fallback, values + 21);
  env->SetDoubleArrayRegion(out, 0, kCapabilityFields, values);
}

// requested[0..8] in; out[0] accepted, out[1] preparationFailed, out[2..10] the effective
// policy. Returns the rejection reason, or one warning per fallback; both are also logged.
SPLATKIT_JNI(jobjectArray, nativeApplyRenderPolicy)
(JNIEnv* env, jobject, jlong handle, jdoubleArray requested, jdoubleArray out) {
  jclass stringClass = env->FindClass("java/lang/String");
  splatkit::SplatEngine* engine = toEngine(handle);
  if (engine == nullptr || requested == nullptr || out == nullptr ||
      env->GetArrayLength(requested) < kPolicyFields ||
      env->GetArrayLength(out) < kPolicyResolutionFields) {
    return env->NewObjectArray(0, stringClass, nullptr);
  }
  double input[kPolicyFields];
  env->GetDoubleArrayRegion(requested, 0, kPolicyFields, input);
  const splatkit::RenderPolicyResolution resolution =
      engine->setRenderPolicy(policyFromDoubles(input));
  double values[kPolicyResolutionFields]{};
  values[0] = resolution.accepted ? 1.0 : 0.0;
  values[1] = resolution.preparationFailed ? 1.0 : 0.0;
  policyToDoubles(resolution.effective, values + 2);
  env->SetDoubleArrayRegion(out, 0, kPolicyResolutionFields, values);

  if (!resolution.accepted) {
    LOGE("render policy rejected: %s", resolution.error.c_str());
    jobjectArray messages = env->NewObjectArray(1, stringClass, nullptr);
    jstring reason = env->NewStringUTF(resolution.error.c_str());
    env->SetObjectArrayElement(messages, 0, reason);
    env->DeleteLocalRef(reason);
    return messages;
  }
  const auto count = static_cast<jsize>(resolution.warnings.size());
  jobjectArray messages = env->NewObjectArray(count, stringClass, nullptr);
  for (jsize i = 0; i < count; ++i) {
    const std::string& message = resolution.warnings[static_cast<size_t>(i)].message;
    LOGW("render policy: %s", message.c_str());
    jstring text = env->NewStringUTF(message.c_str());
    env->SetObjectArrayElement(messages, i, text);
    env->DeleteLocalRef(text);
  }
  return messages;
}

// out[0..8]: the policy currently applied to this instance.
SPLATKIT_JNI(void, nativeRenderPolicy)(JNIEnv* env, jobject, jlong handle, jdoubleArray out) {
  const splatkit::SplatEngine* engine = toEngine(handle);
  if (engine == nullptr || out == nullptr || env->GetArrayLength(out) < kPolicyFields) return;
  double values[kPolicyFields]{};
  policyToDoubles(engine->renderPolicy(), values);
  env->SetDoubleArrayRegion(out, 0, kPolicyFields, values);
}
