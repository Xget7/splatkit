# splat-core

C++17 library shared by the Android and iOS engines.
No graphics, no platform APIs, no React Native.

## Domains

| Domain | Responsibility | Public headers |
|---|---|---|
| Formats | Decode `.spz` and `.glb` into library-owned types in the internal frame | `splat/formats/*.h` |
| Sorting | Back to front order by distance, on a background thread | `splat/sorting/*.h` |
| Math | Column major matrices and vectors shared by every renderer | `splat/math/*.h` |
| Navigation | Collider grid, raycast, character controller | `splat/navigation/*.h` |

## Build and test

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Set `SPLAT_FIXTURES_DIR` to a folder with World Labs example files to run the integration tests.
