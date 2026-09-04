add_executable(splat_core_tests
  formats/SpzDecoderTest.cpp
  formats/GlbDecoderTest.cpp
  navigation/ColliderTest.cpp
  math/Mat4Test.cpp
  math/HalfTest.cpp
  sorting/DistanceSorterTest.cpp
)
target_link_libraries(splat_core_tests PRIVATE splat_core spz GTest::gtest_main)
target_compile_definitions(splat_core_tests PRIVATE
  SPLAT_FIXTURES_DIR="${SPLAT_FIXTURES_DIR}"
)

include(GoogleTest)
gtest_discover_tests(splat_core_tests)
