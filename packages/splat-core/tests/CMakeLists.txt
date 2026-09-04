add_executable(splat_core_tests
  formats/SplatDecoderTest.cpp
  formats/SpzDecoderTest.cpp
  formats/GlbDecoderTest.cpp
  navigation/ColliderTest.cpp
  math/Mat4Test.cpp
  math/HalfTest.cpp
  math/SymmetricEigenTest.cpp
  sorting/DistanceSorterTest.cpp
  sorting/SpatialOrderTest.cpp
  sorting/FrustumSortTest.cpp
  lod/LodTreeTest.cpp
  lod/LodSorterTest.cpp
)
target_link_libraries(splat_core_tests PRIVATE splat_core spz GTest::gtest_main)
target_compile_definitions(splat_core_tests PRIVATE
  SPLAT_FIXTURES_DIR="${SPLAT_FIXTURES_DIR}"
)

include(GoogleTest)
gtest_discover_tests(splat_core_tests)
