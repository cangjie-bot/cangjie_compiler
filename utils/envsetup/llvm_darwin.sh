#!/bin/bash

# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# This script needs to be placed in the output directory of Cangjie compiler.
# ** NOTE: Please use `source' command to execute this script. **

# Get current shell name.
shell_name=$(basename -- $(ps -o comm= $$))

# Get the absolute path of this script according to different shells.
case "${shell_name}" in
    "zsh")
        source_dir="${(%):-%N}"
        ;;
    "sh" | "bash")
        source_dir="${BASH_SOURCE[0]}"
        ;;
    *)
        echo "[ERROR] Unsupported shell: ${shell_name}, please switch to bash, sh or zsh."
        return 1
        ;;
esac

if [ -L ${source_dir} ]; then
    if command -v realpath 2>&1 >/dev/null; then
        source_dir=$(realpath "${source_dir}")
    else
        echo '`realpath` is not found, setup may not process properly.'
    fi
fi
script_dir=$(cd "$(dirname "${source_dir}")"; pwd)

export CANGJIE_HOME=${script_dir}

hw_arch=$(uname -m)
if [ "$hw_arch" = "" ]; then
    hw_arch="x86_64"
elif [ "$hw_arch" = "arm64" ]; then
    hw_arch="aarch64"
fi
export PATH=${CANGJIE_HOME}/bin:${CANGJIE_HOME}/tools/bin:$PATH:${HOME}/.cjpm/bin
export DYLD_LIBRARY_PATH=${CANGJIE_HOME}/runtime/lib/darwin_${hw_arch}_cjnative:${CANGJIE_HOME}/tools/lib:${DYLD_LIBRARY_PATH}
unset hw_arch

if [ -z ${SDKROOT+x} ]; then
    export SDKROOT=`xcrun --sdk macosx --show-sdk-path`
fi

xattr -dr com.apple.quarantine ${script_dir}/* &> /dev/null || true
codesign -s - -f --preserve-metadata=entitlements,requirements,flags,runtime ${script_dir}/third_party/llvm/bin/debugserver &> /dev/null || true
