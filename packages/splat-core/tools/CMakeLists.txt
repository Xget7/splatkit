add_executable(ply2spz ply2spz.cpp)
target_link_libraries(ply2spz PRIVATE spz)
target_compile_options(ply2spz PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>
)
