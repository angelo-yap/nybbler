cmake_minimum_required(VERSION 3.20)
project(Nybbler LANGUAGES C CXX)

# Slice 1: out-of-tree LLVM pass plugin. Pinned to the team's LLVM 16 install
# (see docs/superpowers/specs/2026-06-22-nybbler-design.md section 2).
find_package(LLVM 16 REQUIRED CONFIG)

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

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
add_definitions(${LLVM_DEFINITIONS})

# MODULE library -> libNybbler.so, loaded by opt via -load-pass-plugin.
add_library(Nybbler MODULE lib/Nybbler.cpp)

# Plugins resolve LLVM symbols from the opt process at load time; do not link
# them in, and allow the undefined references at link time. Keep default symbol
# visibility so opt can find the exported `llvmGetPassPluginInfo` entry point.
set_target_properties(Nybbler PROPERTIES PREFIX "lib")

# lit needs to know where the freshly built plugin lives.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/test/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/test/lit.site.cfg.py
  @ONLY)
