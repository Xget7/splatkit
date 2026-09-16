# SDK harness

Host integration: [Android](../packages/splatkit-android/README.md), [iOS](../packages/splatkit-ios/README.md).
Use native views, forward lifecycle, load asynchronously; renderer internals stay private.
React Native GPU controls remain pending.

```sh
python3 scripts/sdk_harness.py plan android
python3 scripts/sdk_harness.py check android
python3 scripts/sdk_harness.py check engine
python3 scripts/sdk_harness.py check metal
python3 scripts/sdk_harness.py android-log capture.log --pid 123 --expected-splats 500000 --environment emulator
python3 -m unittest discover -s scripts/tests
```

Requires Python 3.9+, CMake/native toolchain; Android needs the pinned SDK/NDK and JDK.
Builds may fetch dependencies.
JSON stdout; logs in `build/sdk-harness`; failures exit nonzero.
Skipped tests remain unvalidated.
Log checks require one-session `logcat -v threadtime` evidence and the app PID.
Visual acceptance and physical-device performance require separate evidence.
Device launch/install/capture is explicit and outside this harness.
