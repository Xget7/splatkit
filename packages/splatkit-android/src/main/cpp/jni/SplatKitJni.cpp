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
constexpr jsize kCameraRequestFields = 8;
constexpr jsize kCameraStateFields = 9;
constexpr jsize kCameraResolutionFields = kCameraStateFields + 1;
// fps, frame, gpu and sort milliseconds, splat count, walking and motion flags, then the drawn,
// compute tile, non-empty compute tile and hardware tile counts.
constexpr jsize kStatsFloats = 11;
constexpr jsize kAttitudeFloats = 9;

constexpr jboolean toJni(bool value) {
  return value ? JNI_TRUE : JNI_FALSE;
}

constexpr double flag(bool value) {
  return value ? 1.0 : 0.0;
}

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

SPLATKIT_JNI(void, nativeSetCameraLookAt)
(JNIEnv*, jobject, jlong handle, jfloat fromX, jfloat fromY, jfloat fromZ, jfloat targetX,
 jfloat targetY, jfloat targetZ, jfloat upX, jfloat upY, jfloat upZ) {
  if (auto* engine = toEngine(handle)) {
    engine->setCameraLookAt({fromX, fromY, fromZ}, {targetX, targetY, targetZ}, {upX, upY, upZ});
  }
}

SPLATKIT_JNI(void, nativeSetAnchor)
(JNIEnv*, jobject, jlong handle, jfloat x, jfloat y, jfloat z) {
  if (auto* engine = toEngine(handle)) engine->setCameraAnchor({x, y, z});
}

SPLATKIT_JNI(jboolean, nativeOrbit)
(JNIEnv*, jobject, jlong handle, jfloat deltaAzimuth, jfloat deltaElevation) {
  auto* engine = toEngine(handle);
  return toJni(engine != nullptr && engine->orbit(deltaAzimuth, deltaElevation));
}

SPLATKIT_JNI(jboolean, nativeDolly)(JNIEnv*, jobject, jlong handle, jfloat deltaRadius) {
  auto* engine = toEngine(handle);
  return toJni(engine != nullptr && engine->dolly(deltaRadius));
}

SPLATKIT_JNI(jboolean, nativeFocus)(JNIEnv*, jobject, jlong handle, jfloat x, jfloat y) {
  auto* engine = toEngine(handle);
  return toJni(engine != nullptr && engine->focus(x, y));
}

SPLATKIT_JNI(jboolean, nativeStartOrbitAnimation)
(JNIEnv*, jobject, jlong handle, jfloat degrees, jfloat degreesPerSecond, jboolean easeInOut) {
  auto* engine = toEngine(handle);
  return toJni(engine != nullptr &&
               engine->startOrbitAnimation(degrees, degreesPerSecond, easeInOut == JNI_TRUE));
}

// Fills out[0..4]: x, y, z, yaw, pitch.
SPLATKIT_JNI(void, nativeCameraPose)(JNIEnv* env, jobject, jlong handle, jfloatArray out) {
  auto* engine = toEngine(handle);
  if (engine == nullptr || out == nullptr || env->GetArrayLength(out) < kPoseFloats) return;
  const splatkit::CameraPose p = engine->cameraPose();
  const float values[kPoseFloats] = {p.x, p.y, p.z, p.yaw, p.pitch};
  env->SetFloatArrayRegion(out, 0, kPoseFloats, values);
}

// State: mode, anchor xyz, radius, azimuth, elevation, angular rate, hasAnchor.
namespace {
void cameraStateToDoubles(const splatkit::CameraState& state, double* out) {
  out[0] = static_cast<double>(static_cast<int>(state.mode));
  out[1] = state.anchor.x;
  out[2] = state.anchor.y;
  out[3] = state.anchor.z;
  out[4] = state.radius;
  out[5] = state.azimuth;
  out[6] = state.elevation;
  out[7] = state.orbitRadiansPerSecond;
  out[8] = flag(state.hasAnchor);
}
}  // namespace

SPLATKIT_JNI(void, nativeCameraState)(JNIEnv* env, jobject, jlong handle, jdoubleArray out) {
  auto* engine = toEngine(handle);
  if (engine == nullptr || out == nullptr || env->GetArrayLength(out) < kCameraStateFields) return;
  double values[kCameraStateFields]{};
  cameraStateToDoubles(engine->cameraState(), values);
  env->SetDoubleArrayRegion(out, 0, kCameraStateFields, values);
}

// Applies once, then returns accepted plus a snapshot of the effective state atomically.
SPLATKIT_JNI(jstring, nativeApplyCameraRequest)
(JNIEnv* env, jobject, jlong handle, jdoubleArray requested, jdoubleArray out) {
  auto* engine = toEngine(handle);
  if (engine == nullptr || requested == nullptr || out == nullptr ||
      env->GetArrayLength(requested) < kCameraRequestFields ||
      env->GetArrayLength(out) < kCameraResolutionFields) {
    return env->NewStringUTF("SplatKit camera request unavailable");
  }
  double input[kCameraRequestFields]{};
  env->GetDoubleArrayRegion(requested, 0, kCameraRequestFields, input);
  splatkit::CameraRequest request;
  request.mode = static_cast<splatkit::CameraMode>(static_cast<int>(input[0]));
  request.anchor = {static_cast<float>(input[1]), static_cast<float>(input[2]),
                    static_cast<float>(input[3])};
  request.radius = static_cast<float>(input[4]);
  request.azimuth = static_cast<float>(input[5]);
  request.elevation = static_cast<float>(input[6]);
  request.orbitRadiansPerSecond = static_cast<float>(input[7]);
  const auto resolution = engine->applyCameraRequest(request);
  double values[kCameraResolutionFields]{};
  values[0] = flag(resolution.accepted);
  cameraStateToDoubles(resolution.effective, values + 1);
  env->SetDoubleArrayRegion(out, 0, kCameraResolutionFields, values);
  return env->NewStringUTF(resolution.error.c_str());
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
  return toJni(engine->setCharacter(character));
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

// Fills out[0..10] in the order kStatsFloats lists.
SPLATKIT_JNI(void, nativeStats)(JNIEnv* env, jobject, jlong handle, jfloatArray out) {
  auto* engine = toEngine(handle);
  if (engine == nullptr || out == nullptr || env->GetArrayLength(out) < kStatsFloats) return;
  const splatkit::Stats s = engine->stats();
  // Float transport represents every integer through 2^24 exactly; larger counts can round.
  const float values[kStatsFloats] = {s.fps,
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
  env->SetFloatArrayRegion(out, 0, kStatsFloats, values);
}

// Policy and capabilities. Both arrays are double: every field is a count, flag or float.

namespace {
constexpr jsize kPolicyFields = 9;  // see the index list below
// accepted, preparationFailed, then the effective policy
constexpr jsize kPolicyResolutionFields = kPolicyFields + 2;
// Capabilities: limits and feature flags, policy support, then the fallback policy.
constexpr jsize kCapabilityLeadFields = 7;
constexpr jsize kPolicySupportFields = 14;
constexpr jsize kCapabilityFields = kCapabilityLeadFields + kPolicySupportFields + kPolicyFields;

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
  v[5] = flag(p.enableFrustumCulling);
  v[6] = flag(p.enableHiZOcclusion);
  v[7] = flag(p.enableEarlyTermination);
  v[8] = static_cast<double>(static_cast<uint32_t>(p.sortDepth));
}

void supportToDoubles(const splatkit::RenderPolicySupport& s, double* v) {
  v[0] = flag(s.raster);
  v[1] = flag(s.tileSize);
  v[2] = flag(s.lodErrorPixels);
  v[3] = flag(s.alphaThreshold);
  v[4] = flag(s.subpixelThreshold);
  v[5] = flag(s.enableFrustumCulling);
  v[6] = flag(s.enableHiZOcclusion);
  v[7] = flag(s.enableEarlyTermination);
  v[8] = flag(s.sortDepth);
  v[9] = static_cast<double>(s.tileSizeMask);
  v[10] = static_cast<double>(s.minLodErrorPixels);
  v[11] = static_cast<double>(s.maxLodErrorPixels);
  v[12] = static_cast<double>(s.minSubpixelThreshold);
  v[13] = static_cast<double>(s.maxSubpixelThreshold);
}
}  // namespace

// out[0..29]: limits and feature flags [0..6], policy support [7..20], fallback policy [21..29].
SPLATKIT_JNI(void, nativeCapabilities)(JNIEnv* env, jobject, jlong handle, jdoubleArray out) {
  const splatkit::SplatEngine* engine = toEngine(handle);
  if (engine == nullptr || out == nullptr || env->GetArrayLength(out) < kCapabilityFields) return;
  const splatkit::DeviceCapabilities caps = engine->deviceCapabilities();
  double values[kCapabilityFields]{};
  values[0] = static_cast<double>(caps.limits.maxLodCapacitySplats);
  values[1] = static_cast<double>(caps.limits.minResidencyCapacitySplats);
  values[2] = static_cast<double>(caps.limits.maxResidencyCapacitySplats);
  values[3] = flag(caps.supportsComputeTiles);
  values[4] = flag(caps.supportsHiZOcclusion);
  values[5] = flag(caps.supportsSubgroups);
  values[6] = static_cast<double>(caps.maxTextureDimension);
  supportToDoubles(caps.policy, values + kCapabilityLeadFields);
  policyToDoubles(caps.policy.fallback, values + kCapabilityLeadFields + kPolicySupportFields);
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
  values[0] = flag(resolution.accepted);
  values[1] = flag(resolution.preparationFailed);
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
