cmake_minimum_required(VERSION 3.16)
project(ohh-ls LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(TREE_SITTER REQUIRED tree-sitter)
pkg_check_modules(YYJSON REQUIRED yyjson)

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
target_compile_options(ohh-ls PRIVATE ${TREE_SITTER_CFLAGS_OTHER} ${YYJSON_CFLAGS_OTHER})
target_include_directories(ohh-ls PRIVATE ${TREE_SITTER_INCLUDE_DIRS} ${YYJSON_INCLUDE_DIRS})
target_link_directories(ohh-ls PRIVATE ${TREE_SITTER_LIBRARY_DIRS} ${YYJSON_LIBRARY_DIRS})
target_link_libraries(ohh-ls PRIVATE ohh_grammar ${TREE_SITTER_LIBRARIES} ${YYJSON_LIBRARIES})

# Standalone CLI for fast iteration against the corpus, independent of the
# LSP/stdio layer, mirrors phh-check.
add_executable(ohh-check
  src/registry.cpp
  src/document.cpp
  src/validate.cpp
  src/check_main.cpp
)
target_include_directories(ohh-check PRIVATE ${TREE_SITTER_OHH_DIR}/src)
target_compile_options(ohh-check PRIVATE ${TREE_SITTER_CFLAGS_OTHER} ${YYJSON_CFLAGS_OTHER})
target_include_directories(ohh-check PRIVATE ${TREE_SITTER_INCLUDE_DIRS} ${YYJSON_INCLUDE_DIRS})
target_link_directories(ohh-check PRIVATE ${TREE_SITTER_LIBRARY_DIRS} ${YYJSON_LIBRARY_DIRS})
target_link_libraries(ohh-check PRIVATE ohh_grammar ${TREE_SITTER_LIBRARIES} ${YYJSON_LIBRARIES})
