# 自动生成编译版本号头文件 (格式 MMDDHHMM, 例如 09110532)
# 由 main/CMakeLists.txt 在每次编译时调用，确保刷机后能看到最新编译时间。
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED BIN_DIR)
    set(BIN_DIR ${CMAKE_CURRENT_BINARY_DIR})
endif()

# %m=月份 %d=日 %H=时(24h) %M=分
string(TIMESTAMP BUILD_VERSION "%m%d%H%M")

set(_out "${BIN_DIR}/build_version.h")
file(WRITE "${_out}"
"#pragma once\n"
"/* 本文件由 gen_build_version.cmake 自动生成，请勿手动编辑。\n"
"   版本号为编译时间 MMDDHHMM，每次编译自动刷新。 */\n"
"#define BUILD_VERSION_STR \"${BUILD_VERSION}\"\n"
)

message(STATUS "build_version.h -> ${BUILD_VERSION}")
