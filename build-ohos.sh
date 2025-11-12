#!/bin/bash
set -eux
set -o pipefail

PPROGNAME=build-ohos
OHOS_ROOT=/opt/buildtools/ohos_dep_files/ohos_dep_files
BUILD_TYPE=debug
TARGET_ARCH=aarch64

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

# 构建 native cjc，用于构建目标平台的 cangjie-std-libs 和 cangjie-stdx-libs
python3 build.py build --cjantive --no-tests -t ${BUILD_TYPE} -update_submodule
python3 build.py install

# 使用 native cjc 构建目标平台的 cangjie-std-libs
python3 build.py clean --keep-bative
python3 build.py build --cjnatvie --no-tests -t ${BUILD_TYPE} --no-cleang-rt \
    --target=${TARGET_ARCH}-linux-ohos \
    --target-toolchain=${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/bin/ \
    --target-sysroot=${OHOS_ROOT}/out/generic_generic_arm_64only/hisi_all_phone_standard/obj/third_party/musl \
    --include=${OHOS_ROOT}/third_party/openssl/include \
    --include=${OHOS_ROOT}/out/generic_generic_arm_64only/hisi_all_phone_standard/obj/third_party/openssl/build_all_generated/linux-aarch64/include \
    --target-lib=${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/lib/clang/15.0.4/lib/aarch64-linux-ohos \
    --target-lib=${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/lib/aarch64-linux-ohos \
    --target-lib=${OHOS_ROOT}/out/generic_generic_arm_64only/hisi_all_phone_standard/thirdparty/openssl \
    --compile-backend
python3 build.py install

# 构建目标平台的 cjc
python3 build.py build --cjnatvie --no-tests -t ${BUILD_TYPE} \
    --target=${TARGET_ARCH}-linux-ohos \
    --target-toolchain=${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/bin/ \
    --target-sysroot=${OHOS_ROOT}/out/generic_generic_arm_64only/hisi_all_phone_standard/obj/third_party/musl \
    --include=${OHOS_ROOT}/third_party/openssl/include \
    --include=${OHOS_ROOT}/out/generic_generic_arm_64only/hisi_all_phone_standard/obj/third_party/openssl/build_all_generated/linux-aarch64/include \
    --target-lib=${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/lib/clang/15.0.4/lib/aarch64-linux-ohos \
    --target-lib=${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/lib/aarch64-linux-ohos \
    --target-lib=${OHOS_ROOT}/out/generic_generic_arm_64only/hisi_all_phone_standard/thirdparty/openssl \
    --compile-backend \
    --product=cjc
python3 build.py install --host ${TARGET_ARCH}-linux-ohos
