# Standalone Build Guide

## Specifications

The current frontend compiler build supports:

1. [Building a Linux platform compiler on Linux](#building-a-linux-platform-compiler-on-linux)
2. [Building a macOS platform compiler on macOS](#building-a-macos-platform-compiler-on-macos)
3. [Building a Windows platform compiler on Linux (cross-compilation)](#building-a-windows-platform-compiler-on-linux-cross-compilation)

## Building a Linux Platform Compiler on Linux

### Environment Dependencies

The standalone compiler build environment is largely consistent with the integrated build environment, except for the additional dependency on googletest for executing UTs. For detailed information, please refer to [Cangjie Build Guide (Ubuntu 22.04) - Environment Preparation].  
For googletest dependency installation, refer to [General Build Guide](https://github.com/google/googletest/blob/main/googletest/README.md). Alternatively, you can temporarily disable UT builds during compilation using the [`--no-test`](#build-options) option.

### Build Commands

Download the source code:

Note: Ensure the compilation platform has normal network connectivity and can access code hosting platforms like Gitcode or Gitee.

```shell
export WORKSPACE=$HOME/cangjie_build;
git clone https://gitcode.com/Cangjie/cangjie_compiler.git -b main;
```

Compile the source code:

```shell
cd $WORKSPACE/cangjie_compiler;
export CMAKE_PREFIX_PATH=/opt/buildtools/libedit-3.1:/opt/buildtools/ncurses-6.3/usr;
python3 build.py clean;
python3 build.py build -t release --build-cjdb;
python3 build.py install;
```

1. The `build.py clean` command clears temporary files in the workspace.
2. The `build.py build` command initiates compilation:
   - The secondary option `-t` (i.e., `--build-type`) specifies the build product type, which can be `release`, `debug`, or `relwithdebuginfo`.
   - The secondary option `--build-cjdb` enables cjdb (lldb) compilation. For more details about cjdb, refer to [`cjdb` Tool Introduction](https://gitcode.com/Cangjie/cangjie_docs/blob/main/docs/tools/source_en/cmd-tools/cjdb_manual.md).
3. The `build.py install` command installs the build products to the `output` directory.

Verify the products:

```shell
source ./output/envsetup.sh
cjc -v
```

Output example:

```text
Cangjie Compiler: x.xx.xx (cjnative)
Target: xxxx-xxxx-xxxx
```

## Building a macOS Platform Compiler on macOS

### Environment Preparation

The standalone compiler build environment is largely consistent with the integrated build environment, except for the additional dependency on googletest for executing UTs. For detailed information, please refer to [Cangjie Build Guide (macOS 14 Sonoma) - Environment Preparation].  
For googletest dependency installation, refer to [General Build Guide](https://github.com/google/googletest/blob/main/googletest/README.md). Alternatively, you can temporarily disable UT builds during compilation using the [`--no-test`](#build-options) option.

### Build Commands

Download the source code:

Note: Ensure the compilation platform has normal network connectivity and can access code hosting platforms like Gitcode or Gitee.

```shell
export WORKSPACE=$HOME/cangjie_build;
git clone https://gitcode.com/Cangjie/cangjie_compiler.git -b main;
```

Compile the source code:

```shell
cd $WORKSPACE/cangjie_compiler;
python3 build.py clean;
python3 build.py build -t release --build-cjdb;
python3 build.py install;
```

1. The `build.py clean` command clears temporary files in the workspace.
2. The `build.py build` command initiates compilation:
   - The secondary option `-t` (i.e., `--build-type`) specifies the build product type, which can be `release`, `debug`, or `relwithdebuginfo`.
   - The secondary option `--build-cjdb` enables cjdb (lldb) compilation. For more details about cjdb, refer to [`cjdb` Tool Introduction](https://gitcode.com/Cangjie/cangjie_docs/blob/main/docs/tools/source_en/cmd-tools/cjdb_manual.md).
3. The `build.py install` command installs the build products to the `output` directory.

Verify the products:

```shell
source ./output/envsetup.sh
cjc -v
```

Output example:

```text
Cangjie Compiler: x.xx.xx (cjnative)
Target: xxxx-xxxx-xxxx
```

## Building a Windows Platform Compiler on Linux (Cross-Compilation)

### Environment Preparation

The standalone compiler build environment is largely consistent with the integrated build environment, except for the additional dependency on googletest for executing UTs. For detailed information, please refer to [Cangjie Build Guide (Ubuntu 22.04) - Environment Preparation].  
For googletest dependency installation, refer to [General Build Guide](https://github.com/google/googletest/blob/main/googletest/README.md). Alternatively, you can temporarily disable UT builds during compilation using the [`--no-test`](#build-options) option.

Note: Ensure the compilation platform has normal network connectivity and can access code hosting platforms like Gitcode or Gitee.

### Build Commands

Download the source code:

```shell
export WORKSPACE=$HOME/cangjie_build;
git clone https://gitcode.com/Cangjie/cangjie_compiler.git -b main;
```

Compile the source code:

```shell
cd $WORKSPACE/cangjie_compiler;
export CMAKE_PREFIX_PATH=${MINGW_PATH}/x86_64-w64-mingw32;
python3 build.py build -t release \
	--product cjc \
	--target windows-x86_64 \
	--target-sysroot /opt/buildtools/mingw-w64/ \
	--target-toolchain /opt/buildtools/mingw-w64/bin \
	--build-cjdb;
python3 build.py install --host windows-x86_64;
```

1. The `CMAKE_PREFIX_PATH` environment variable specifies the folder where cmake generates products for the target platform.
2. The `build.py clean` command clears temporary files in the workspace.
3. The `build.py build` command initiates compilation:
   - The secondary option `-t` (i.e., `--build-type`) specifies the build product type, which can be `release`, `debug`, or `relwithdebuginfo`.
   - The secondary option `--target` specifies the target platform description, which can be `native` (current compilation platform), `windows-x86_64`, `ohos-aarch64`, or `ohos-x86_64`.
   - The secondary option `--target-sysroot` passes the subsequent parameter to the C/C++ compiler as its `--sysroot` parameter.
   - The secondary option `--target-toolchain` specifies the path to the target platform toolchain, using the compiler in this path for cross-compilation.
   - The secondary option `--build-cjdb` enables cjdb (lldb) compilation. For more details about cjdb, refer to [`cjdb` Tool Introduction](https://gitcode.com/Cangjie/cangjie_docs/blob/main/docs/tools/source_en/cmd-tools/cjdb_manual.md).
4. The `build.py install` command installs the build products to the `output` directory:
   - The secondary option `--host` specifies the target platform installation strategy, which can be `native` (current compilation platform), `windows-x86_64`, `ohos-aarch64`, or `ohos-x86_64`.

Verify the products:

Since the build products are Windows platform executables, copy them to a Windows machine and use the `./output/envsetup.bat` script to set up the cjc environment.

```bash
source ./output/envsetup.bat
cjc.exe -v
```

This step only generates the target platform cjc executable. For building peripheral dependencies, refer to [Cangjie Build Guide (Ubuntu 22.04) - Source Code Build].

## build.py Option Help

### `clean` Option

The `clean` option clears the build/output folders.

### `build` Option

The `build` option builds the project files. It provides the following secondary options:

- `-h, --help`: Displays help information for secondary options.
- `-t, --build-type`: Specifies the build product type, which can be `release`, `debug`, or `relwithdebuginfo`.
- `--print-cmd`: Displays the complete cmake command configured by the build script.
- `-j, --jobs JOBS`: Specifies the number of concurrent build tasks.
- `--link-jobs LINK_JOBS`: Specifies the number of concurrent linking tasks.
- `--enable-assert`: Enables compiler assertions for development and debugging.
- `--no-tests`: Skips compiling unittest code.
- `--disable-stack-grow-feature`: Disables stack growth functionality.
- `--hwasan`: Enables hardware asan functionality for compiler source code. Currently, this is only supported on the ohos platform due to dependency on hwasan tools.
- `--gcc-toolchain`: Specifies the gcc toolchain for cross-compilation.
- `--target`: Specifies the target platform description, which can be `native` (current compilation platform), `windows-x86_64`, `ohos-aarch64`, or `ohos-x86_64`.
- `-L, --target-lib`: Specifies the path to the target platform's required linked libraries.
- `--target-toolchain`: Specifies the path to the compilation tools.
- `-I, --include`: Specifies the target platform's header file search path.
- `--target-sysroot`: Passes the sysroot content to the C/C++ compiler's sysroot option.
- `--product {all,cjc,libs}`: Specifies the build target products, which can be `all` (default, includes `cjc` and `libs`), `cjc` (compiler binary), or `libs` (compiler libraries required by the standard library).
- `--build-cjdb`: Enables building the Cangjie debugger.

### `install` Option

The `install` option organizes the build products into the specified directory. It provides the following secondary options:

- `-h, --help`: Displays help information for secondary options.
- `--host`: Specifies the target platform installation strategy, which can be `native` (current compilation platform), `windows-x86_64`, `ohos-aarch64`, or `ohos-x86_64`.
- `--prefix`: Specifies the installation folder path for the products. If neither this option nor `--host` is specified, the products are installed in the `output` folder under the project directory. If both are specified, the products are installed in the path specified by `--prefix`.

### `test` Option

The `test` option executes the compiled unittest cases. It has no effect if `--no-test` was specified during compilation.