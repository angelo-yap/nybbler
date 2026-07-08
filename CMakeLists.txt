cmake_minimum_required(VERSION 3.20)
project(Nybbler LANGUAGES C CXX)

# Slice 1: out-of-tree LLVM pass plugin. Pinned to the team's LLVM 22 install
# (see docs/superpowers/specs/2026-06-22-nybbler-design.md section 2).
# LLVM's package config only accepts an exact major.minor match, so requesting
# a bare major ("22") fails against point releases like 22.1.x. Request any LLVM
# and assert the major ourselves -- this pins to 22 across all 22.x releases.
find_package(LLVM REQUIRED CONFIG)
if(NOT LLVM_VERSION_MAJOR EQUAL 22)
  message(FATAL_ERROR
    "Nybbler requires LLVM 22, but found ${LLVM_PACKAGE_VERSION}")
endif()

message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "LLVM version: ${LLVM_PACKAGE_VERSION}")

list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(AddLLVM)

# Match LLVM's build settings so the plugin's ABI is compatible.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

add_definitions(${LLVM_DEFINITIONS})

# MODULE library -> libNybbler.so, loaded by opt via -load-pass-plugin.
add_library(Nybbler MODULE lib/Nybbler.cpp)

target_include_directories(Nybbler SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(Nybbler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Plugins resolve LLVM symbols from the opt process at load time; do not link
# them in, and allow the undefined references at link time. Keep default symbol
# visibility so opt can find the exported `llvmGetPassPluginInfo` entry point.
set_target_properties(Nybbler PROPERTIES 
    PREFIX "lib"
    ENABLE_EXPORTS ON
)

# Tell the macOS linker to allow undefined symbols to be resolved at runtime by opt
if(APPLE)
    target_link_options(Nybbler PRIVATE "-undefined" "dynamic_lookup")
  # Map and link necessary LLVM component interfaces
  llvm_map_components_to_libnames(llvm_libs Core Support IRReader)
  target_link_libraries(Nybbler PRIVATE ${llvm_libs})
endif()

# lit needs to know where the freshly built plugin lives.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/test/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/test/lit.site.cfg.py
  @ONLY)
