cmake_minimum_required(VERSION 3.16)
project(ohh-ls LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# tree-sitter (the runtime library, not a grammar) and yyjson are fetched
# and built from source rather than found via pkg-config, so this builds
# the same way on macOS, Linux and Windows with nothing but a C/C++
# compiler and CMake, no system package manager, Homebrew formula or
# vcpkg port required. Pinned to the exact versions this project has been
# built and tested against (confirmed via `brew list --versions` on the
# machine this was last verified on), same pattern as
# phh-ls/CMakeLists.txt.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(tree_sitter_runtime
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG v0.27.0
)
FetchContent_Declare(yyjson
  GIT_REPOSITORY https://github.com/ibireme/yyjson.git
  GIT_TAG 0.12.0
)
FetchContent_MakeAvailable(tree_sitter_runtime yyjson)

# tree-sitter-ohh lives in its own sibling repository, ohh-ls builds
# against its generated parser.c directly, same pattern as
# phh-ls/CMakeLists.txt.
set(TREE_SITTER_OHH_DIR ${CMAKE_SOURCE_DIR}/../tree-sitter-ohh)

add_library(ohh_grammar STATIC
  ${TREE_SITTER_OHH_DIR}/src/parser.c
)
target_include_directories(ohh_grammar PUBLIC
  ${TREE_SITTER_OHH_DIR}/src
)

add_executable(ohh-ls
  src/registry.cpp
  src/document.cpp
  src/validate.cpp
  src/jsonrpc.cpp
  src/main.cpp
)

target_include_directories(ohh-ls PRIVATE ${TREE_SITTER_OHH_DIR}/src)
target_link_libraries(ohh-ls PRIVATE ohh_grammar tree-sitter yyjson)

# Standalone CLI for fast iteration against the corpus, independent of the
# LSP/stdio layer, mirrors phh-check.
add_executable(ohh-check
  src/registry.cpp
  src/document.cpp
  src/validate.cpp
  src/check_main.cpp
)
target_include_directories(ohh-check PRIVATE ${TREE_SITTER_OHH_DIR}/src)
target_link_libraries(ohh-check PRIVATE ohh_grammar tree-sitter yyjson)
