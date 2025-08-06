# 仓颉编程语言

## 简介

仓颉编译器及调试器为开发者提供了编译和调试的基本工具链，详细的特性及使用指导，请参考[仓颉语言开发指南](https://gitcode.com/Cangjie/cangjie_docs/blob/dev/docs/dev-guide/source_zh_cn/first_understanding/basic.md)以及[调试工具](https://gitcode.com/Cangjie/cangjie_docs/blob/dev/docs/tools/source_zh_cn/cmd-tools/cjdb_manual.md)。

本仓提供了仓颉编译器源码及 cjdb 调试工具源码，仓颉编译器的整体框架及编译流程如下图展示：

![架构图](figures/Compiler_Architecture_Diagram_zh.png)

## 目录结构

```text
cangjie_compiler/
├── build.py                    # 编译器源码构建脚本
├── cmake                       # cmake文件夹，用于保存构建辅助脚本
├── include                     # 头文件文件夹
├── schema                      # 用于保存 FlatBuffers Schema 序列化数据结构文件
├── src                         # 编译器源码文件夹
│   ├── AST                     # 抽象语法树相关内容
│   ├── Basic                   # 编译器基础组件
│   ├── CHIR                    # 编译器中间层，该阶段对代码进行优化分析
│   ├── CodeGen                 # 代码生成，将 CHIR 翻译为 LLVMIR
│   ├── ConditionalCompilation  # 条件编译
│   ├── Demangle                # 符号还原
│   ├── Driver                  # 编译器流程驱动，用于启动前端以及调用后端命令
│   ├── Frontend                # 编译器实例类，组织编译器流程
│   ├── FrontendTool            # 提供给周边工具的编译器实例类
│   ├── IncrementalCompilation  # 增量编译
│   ├── Lex                     # 词法分析
│   ├── Macro                   # 宏展开
│   ├── main.cpp                # 编译器入口
│   ├── Mangle                  # 符号改名
│   ├── MetaTransformation      # 元编程编译器插件
│   ├── Modules                 # 包管理模块
│   ├── Option                  # 编译器选项控制
│   ├── Parse                   # 语法分析
│   ├── Sema                    # 语义分析
│   └── Utils                   # 公共组件
├── third_party                 # 依赖的三方库构建脚本及依赖库 patch 文件
│   ├── cmake                   # 三方库 cmake 构建辅助脚本
│   ├── llvmPatch.diff          # llvm 后端 patch 文件，包含 llvm 及 cjdb 相关源码
│   └── flatbufferPatch.diff    # flatbuffer 源码 patch 文件
├── unittests                   # 单元测试用例
└── utils                       # 编译器周边组件
```

## OpenHarmony 如何使用 cangjie_compiler

面向框架及应用开发者，仓颉编译器以及调试器作为仓颉代码的编译工具链可独立或配合[包管理工具](https://gitcode.com/Cangjie/cangjie_docs/blob/dev/docs/tools/source_zh_cn/cmd-tools/cjpm_manual.md)通过 DevEco Studio 进行使用。

## OpenHarmony 如何集成 cangjie_compiler

仓颉编译器及工具链预编译为二进制集成，构建过程可参考[OHOS 仓颉SDK构建指导书 (Ubuntu 22.04)](https://gitcode.com/Cangjie/cangjie_build/blob/dev/docs/linux_ohos_zh.md)。

## License 许可

本项目开源许可请参阅 [LICENSE](LICENSE)。

## 相关仓

本仓为仓颉编译器源码，完整的编译器组件还包含：

- [仓颉语言开发指南](https://gitcode.com/Cangjie/cangjie_docs/tree/main/docs/dev-guide)：提供仓颉语言开发使用指南；
- [仓颉语言标准库](https://gitcode.com/Cangjie/cangjie_runtime/tree/main/std)：提供仓颉标准库源码；
- [仓颉运行时](https://gitcode.com/Cangjie/cangjie_runtime/tree/main/runtime)：提供仓颉语言所必需的标准库代码；
- [仓颉工具](https://gitcode.com/Cangjie/cangjie_tools/tree/main)：提供仓颉工具套，包含代码格式化、包管理等工具。

## 使用的开源软件声明

| 开源软件名称 | 开源许可协议 | 使用说明 | 使用主体 | 使用方式 |
| ---------- | ---------- | ------ | ------- | ------- |
| mingw-w64 | Zope Public License V2.1 | 仓颉 Windows 版本 SDK 携带 Mingw 中的部分静态库文件，与仓颉代码生成的目标文件链接在一起，为用户生成最终的可以调用 Windows API 的可执行二进制文件 | 编译器 | 集成到仓颉二进制发布包中 |
| LLVM | Apache 2.0 with LLVM Exception | 仓颉编译器后端基于 llvm 开发实现 | 编译器 | 集成到仓颉二进制发布包中 |
| libxml2 | MIT License | 仓颉调试器基于 lldb 实现，本软件是 lldb 的依赖软件 | 调试器 | 集成到仓颉二进制发布包中 |
| libedit | BSD 3-Clause License | 仓颉调试器基于 lldb 实现，本软件是 lldb 的依赖软件 | 调试器 | 集成到仓颉二进制发布包中 |
| ncurses | MIT License | 仓颉调试器基于 lldb 实现，lldb 依赖 libedit，libedit 依赖本软件 | 调试器 | 集成到仓颉二进制发布包中 |
| flatbuffers | Apache License V2.0 | 仓颉的 cjo 文件和宏实现依赖该软件进行序列化和反序列化 | 编译器和标准库(std.ast) | 集成到仓颉二进制发布包中 |
| PCRE2 | BSD 3-Clause License | 标准库中的正则库基于该软件封装实现 | 标准库(std.regex) | 集成到仓颉二进制发布包中 | 
| zlib | zlib/libpng License | 扩展库中的压缩库基于该软件封装实现 | 扩展库(compress.zlib) | 集成到仓颉二进制发布包中 |
| libboundscheck | Mulan Permissive Software License Version 2 | 编译器等相关代码基于该软件实现 | 编译器、标准库、扩展库 | 集成到仓颉二进制发布包中 |
| OpenSSL | Apache License V2.0 | 扩展库中的 HTTP 和 TLS 封装该软件的接口 | 扩展库(net.http、net.tls) | 作为构建工具使用，不会集成到仓颉二进制包 |
