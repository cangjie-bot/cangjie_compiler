#!/bin/bash
# 将此脚本放置在 cangjie_compiler 目录下
WORKSPACE=$(cd "$(dirname "$0")" && pwd)
WORKSPACE=${WORKSPACE}/..
echo WORKSPACE=${WORKSPACE}

set -ex
set -o pipefail

PPROGNAME=build-ohos
BUILD_TYPE=debug
TARGET_ARCH=aarch64
export BUILD_TOOLS_ROOT=/opt/buildtools
export OHOS_ROOT=${BUILD_TOOLS_ROOT}/ohos/ohos_root
cangjie_version=666.666.666 # 请根据实际版本修改

# 帮助系统
show_help() {
    cat <<EOF
$PPROGNAME - linux 交叉编译 ohos sdk 脚本

用法：
  $PPROGNAME.sh <命令> [命令选项]

命令:
  -t <value>                    构建的 cjc 及 cangjie-std-libs 类型
    <value>=debug               (default)
    <value>=release
    <value>=relwithdebinfo
  --arch <value>                目标 ohos 架构
    <value>=aarch64             (default)
    <value>=x86_64
  --ohos-root <value>           OHOS SDK 路径
  -c, --clean                   清除构建产物
  -h, --help                    显示帮助信息
EOF
}

# 解析构建类型
handle_build_type() {
    case ${1} in
        debug|release|relwithdebinfo) BUILD_TYPE=${1} ;;
        *) echo "-t 仅支持  debug、release、relwithdebinfo" >&2; exit -1 ;;
    esac
}

# 解析目标 ohos 架构
handle_arch() {
    case ${1} in
        aarch64|x86_64) TARGET_ARCH=${1} ;;
        *) echo "--aarch 仅支持  aarch64、x86_64" >&2; exit -1 ;;
    esac
}

# 解析 ohos sdk 路径
handle_ohos_root() {
    OHOS_ROOT=${1}
}

# 清除构建产物
handle_clean() {
    python3 build.py clean
}

until [ $# -eq 0 ]
do
    if [ ${1} = "-t" ]
    then
        handle_build_type ${2}
        shift
    elif [ ${1} = "--arch" ]
    then
        handle_arch ${2}
        shift
    elif [ ${1} = "--ohos-root" ]
    then
        handle_ohos_root ${2}
        shift
    elif [ ${1} = "-c" ] || [ ${1} = "--clean" ]
    then
        handle_clean
        exit 0
    elif [ ${1} = "-h" ] || [ ${1} = "--help" ]
    then
        show_help
        exit 0
    else
        echo "不支持的命令行参数：${1}" >&2
        show_help
        exit -1
    fi
    shift
done

# 检查 OHOS_ROOT
if [ ! -d ${OHOS_ROOT} ]
then
    echo "OHOS_ROOT:${OHOS_ROOT} 目录不存在" >&2
    exit -1
fi

# 显示配置信息
echo "*** OHOS_ROOT=${OHOS_ROOT} BUILD_TYPE=${BUILD_TYPE} TARGET_ARCH=${TARGET_ARCH} ***"

# 1 构建 native cangjie-sdk
# 1.1 编译 native cjc
cd ${WORKSPACE}/cangjie_compiler;
python3 build.py clean;
python3 build.py build -t ${BUILD_TYPE} --no-tests;
python3 build.py install;

# 1.2 编译 native runtime
cd ${WORKSPACE}/cangjie_runtime/runtime;
python3 build.py clean;
python3 build.py build -t ${BUILD_TYPE} -v ${cangjie_version};
python3 build.py install;

# 1.3 编译 native std
source ${WORKSPACE}/cangjie_compiler/output/envsetup.sh;
cd ${WORKSPACE}/cangjie_runtime/stdlib;
python3 build.py clean;
python3 build.py build -t ${BUILD_TYPE} \
    --target-lib=${BUILD_TOOLS_ROOT}/openssl-3.0.9/bin \
    --target-lib=${WORKSPACE}/cangjie_runtime/runtime/output;
python3 build.py install;

# 2 构建 ohos cangjie-sdk
# 2.1 构建 ohos cjc-libs
cd ${WORKSPACE}/cangjie_compiler;
python3 build.py build -t ${BUILD_TYPE} --no-tests \
    --product=libs \
    --target=ohos-aarch64 \
    --target-toolchain=${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/bin \
    --target-sysroot=${OHOS_ROOT}/out/sdk/obj/third_party/musl/sysroot;
python3 build.py install --host ohos-aarch64;
python3 build.py install; # 将 ohos cjc-libs 安装到 native cangjie-sdk 包中，构建 ohos stdx 时依赖 ohos cjc-libs

# 2.2 编译 ohos runtime
cd ${WORKSPACE}/cangjie_runtime/runtime;
python3 build.py build -t ${BUILD_TYPE} \
    --target=ohos-aarch64 \
    --target-toolchain=${OHOS_ROOT} \
    -v ${cangjie_version};
python3 build.py install;

# 2.3 使用 native cjc 编译 ohos std
source ${WORKSPACE}/cangjie_compiler/output/envsetup.sh;
cd ${WORKSPACE}/cangjie_runtime/stdlib;
python3 build.py build -t ${BUILD_TYPE} \
    --target=ohos-aarch64 \
    --target-lib=${WORKSPACE}/cangjie_runtime/runtime/output \
    --target-lib=${WORKSPACE}/cangjie_runtime/runtime/output/common \
    --target-toolchain=${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/bin \
    --target-sysroot=${OHOS_ROOT}/out/sdk/obj/third_party/musl/sysroot;
python3 build.py install --host ohos-aarch64;

# 2.4 编译 ohos stdx
cp -rf ${WORKSPACE}/cangjie_runtime/stdlib/output/* ${WORKSPACE}/cangjie_compiler/output; # 构建 ohos stdx 依赖 native std
cp -rf ${WORKSPACE}/cangjie_runtime/stdlib/output-aarch64-linux-ohos/* ${WORKSPACE}/cangjie_compiler/output; # 构建 ohos stdx 依赖 ohos std
cp -rf ${WORKSPACE}/cangjie_runtime/runtime/output/common/linux_${BUILD_TYPE}_x86_64/{lib,runtime} ${WORKSPACE}/cangjie_compiler/output; # 构建 ohos stdx 依赖 native runtime
cp -rf ${WORKSPACE}/cangjie_runtime/runtime/output/common/linux_ohos_${BUILD_TYPE}_aarch64/{lib,runtime} ${WORKSPACE}/cangjie_compiler/output; # 构建 ohos stdx 依赖 ohos runtime
source ${WORKSPACE}/cangjie_compiler/output/envsetup.sh;
cd ${WORKSPACE}/cangjie_stdx;
python3 build.py clean;
python3 build.py build -t ${BUILD_TYPE} \
    --include=${WORKSPACE}/cangjie_compiler/include \
    --target-lib=${WORKSPACE}/cangjie_runtime/runtime/output \
    --target-lib=${WORKSPACE}/cangjie_runtime/runtime/output/common \
    --target-lib=${BUILD_TOOLS_ROOT}/openssl-3.0.9 \
    --target=ohos-aarch64 \
    --target-toolchain=${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/bin \
    --target-sysroot=${OHOS_ROOT}/out/sdk/obj/third_party/musl/sysroot;
python3 build.py install --host ohos-aarch64;

# 2.5 编译 ohos cjc
cd ${WORKSPACE}/cangjie_compiler;
python3 build.py build -t ${BUILD_TYPE} --no-tests \
    --product=cjc \
    --target=ohos-aarch64 \
    --target-toolchain=${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/bin \
    --target-sysroot=${OHOS_ROOT}/out/sdk/obj/third_party/musl/sysroot;
python3 build.py install --host ohos-aarch64;

# 3 打包
# 3.1 打包 cangjie-sdk-ohos 产物
cd ${WORKSPACE}
mkdir -p ${WORKSPACE}/cangjie;
cp -rf ${WORKSPACE}/cangjie_compiler/output-aarch64-linux-ohos/* ${WORKSPACE}/cangjie;
cp -rf ${WORKSPACE}/cangjie_runtime/runtime/output/common/linux_ohos_${BUILD_TYPE}_aarch64/{lib,runtime} ${WORKSPACE}/cangjie;
cp -rf ${WORKSPACE}/cangjie_runtime/stdlib/output-aarch64-linux-ohos/* ${WORKSPACE}/cangjie;
tar -czf ${WORKSPACE}/cangjie-sdk-ohos-aarch64-${cangjie_version}-${BUILD_TYPE}.tar.gz ${WORKSPACE}/cangjie/*;
rm -rf ${WORKSPACE}/cangjie;
echo "cangjie-sdk-ohos-aarch64-${cangjie_version}-${BUILD_TYPE}.tar.gz 打包完成 ^_^";
# 3.2 打包 cangjie-stdx-ohos 产物
cp -rf ${WORKSPACE}/cangjie_stdx/output-aarch64-linux-ohos/* ${WORKSPACE}/.;
zip -q -r ${WORKSPACE}/cangjie-stdx-ohos-aarch64-${cangjie_version}-${BUILD_TYPE}.zip ${WORKSPACE}/linux_ohos_aarch64_cjnative;
rm -rf ${WORKSPACE}/linux_ohos_aarch64_cjnative;
echo "cangjie-stdx-ohos-aarch64-${cangjie_version}-${BUILD_TYPE}.zip 打包完成 ^_^";
