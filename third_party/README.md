# 使用的开源软件说明

## libboundscheck

### 代码来源说明

该仓被编译器及周边组件源码依赖，仓库源码地址为 [third_party_bounds_checking_function](https://gitcode.com/openharmony/third_party_bounds_checking_function)，版本为 [OpenHarmony-v6.0-Release](https://gitcode.com/openharmony/third_party_bounds_checking_function/tags/OpenHarmony-v6.0-Release)。

### 构建说明

该仓由 CMake 作为子目标项目依赖，编译时自动编译该项目并依赖，详细的构建参数见 [CMakeLists.txt](third_party/cmake/CMakeLists.txt) 文件。

## flatbuffers

### 代码来源说明

FlatBuffers 是一个高效的跨平台、跨语言序列化库，仓颉语言使用 FlatBuffers 库完成编译器数据到指定格式的序列化反序列化操作。
该仓被编译器及标准库源码依赖，其基于开源代码 [flatbuffers v24.3.25](https://gitee.com/mirrors_trending/flatbuffers/tree/v24.3.25) 进行定制化修改。

### 三方库 patch 说明

为了提供 C++ 到仓颉语言的序列化反序列化能力，本项目对 FlatBuffers 库进行定制化修改，导出为 flatbufferPatch.diff 文件。

### 构建说明

#### 方式 1：手动应用

下载 FlatBuffers v24.3.25 版本到 third_party 目录下：

```
git clone https://gitee.com/mirrors_trending/flatbuffers -b v24.3.25
```

在 third_party 目录下，执行命令应用 patch：
```
git apply --dictionary flatbuffers flatbufferPatch.diff
```

#### 方式 2：自动应用（推荐）

项目的构建命令中已集成自动化下载第三方库，应用 patch 的脚本，执行以下命令自动应用 patch 并构建：

```
python build.py build -t release # 脚本会自动并应用 patch
```

#### 注意事项
此 patch 基于指定的官方版本 v24.3.25 开发，升级第三方库版本前需重新验证 patch 有效性，避免版本不兼容导致的问题。

## LLVM

### 代码来源说明

llvmPatch通过仓库 https://gitcode.com/Cangjie/llvm-project/ project的main分支基于llvmorg-15.0.4生成。

### 三方库 patch 说明（如果有）

### 构建说明
