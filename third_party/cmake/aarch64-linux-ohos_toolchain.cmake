# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

# set the name of the target operating system
set(CMAKE_SYSTEM_NAME Linux)

# set the processor or handware name of the target system
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(OHOS_ROOT /opt/buildtools/ohos_dep_files/ohos_dep_files)
set(CMAKE_SYSROOT ${OHOS_ROOT}/out/generic_generic_arm_64only/hisi_all_phone_standard/obj/third_party/musl)
include_directories(${OHOS_ROOT}/out/generic_generic_arm_64only/hisi_all_phone_standard/obj/third_party/openssl/build_all_generated/linux-aarch64/include)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(TRIPLE aarch64-linux-ohos)
set(CMAKE_C_COMPILER_TARGET   ${TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${TRIPLE})

add_compile_definitions(__OHOS__)
add_compile_definitions(__MUSL__)
add_compile_definitions(STANDARD_SYSTEM)