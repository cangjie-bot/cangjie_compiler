# 仓颉编译插件流程与 MetaTransformation 方案

## 1. 编译插件流程概述
- **插件加载 (`LOAD_PLUGINS`)**：`CompilerInstance::PerformPluginLoad` 遍历 `--plugin` 传入的动态库，经由 `MetaTransformPlugin::Get` 打开符号表，校验 `CANGJIE_VERSION` 后将注册函数写入 `MetaTransformPluginBuilder`。
- **插件构建**：CHIR 阶段调用 `MetaTransformPluginBuilder::BuildCHIRPluginManager`，将所有回调注入 `CHIRPluginManager`，并携带当前 `CHIR::CHIRBuilder` 实例，保证插件可创建/修改 IR 节点。
- **插件执行 (`ToCHIR::PerformPlugin`)**：对 `CHIRPluginManager` 中的每个 `MetaTransformConcept` 执行遍历；函数级插件对 `Package.GetGlobalFuncs()` 逐一调用 `MetaTransform<CHIR::Func>::Run`，包级插件则直接操作 `MetaTransform<CHIR::Package>::Run`。如果启用了 `IRCheckerAfterPlugin`，执行完插件后会立即触发 IR 校验与调试导出。
- **异常处理**：插件执行包裹在 try/catch 中，任何未捕获异常都会触发 `DiagKindRefactor::plugin_throws_exception`，并阻断当前编译以防止状态污染。

## 2. MetaTransformation 架构
- **核心抽象**
  - `MetaTransformConcept`：记录 `MetaTransformKind`，提供 `IsForCHIR/IsForFunc/IsForPackage` 等判断方法。
  - `MetaTransform<DeclT>`：模板基类，定义 `Run(DeclT&)` 接口，并在构造阶段依据模板参数自动设置 `MetaTransformKind`（函数/包）。
- **管理组件**
  - `MetaTransformPluginManager<MetaKindT>`：统一维护 `vector<unique_ptr<MetaTransformConcept>>`，对外暴露 `AddMetaTransform` 与 `ForEachMetaTransformConcept`。
  - `MetaTransformPluginBuilder`：保存插件注册的回调列表，构建阶段将回调转化为具体插件实例并注入 Manager。
- **插件信息与宏**
  - `MetaTransformPluginInfo`：包含 `cjcVersion` 和 `registerTo` 函数指针。
  - `CHIR_PLUGIN(plugin_name)` 宏：自动导出 `getMetaTransformPluginInfo`，检查版本，并将 `plugin_name` 的实例化逻辑注入 Builder，极大降低插件样板代码。

## 3. 关键数据结构
| 数据结构 | 作用 | 关键字段/方法 |
| --- | --- | --- |
| `MetaTransformKind` | 区分插件作用维度 | `FOR_CHIR_FUNC`, `FOR_CHIR_PACKAGE` |
| `MetaTransformConcept` | 统一抽象基类 | `kind`、`IsForCHIR()` 等 |
| `MetaTransform<DeclT>` | 具体插件基类 | 纯虚 `Run(DeclT&)` |
| `MetaTransformPluginManager` | 插件管理器 | `AddMetaTransform()`、`ForEachMetaTransformConcept()` |
| `MetaTransformPluginBuilder` | 构建器 | `RegisterCHIRPluginCallback()`、`BuildCHIRPluginManager()` |
| `MetaTransformPluginInfo` | 动态库导出信息 | `cjcVersion`、`registerTo` |

## 4. 实现方案亮点
- **模板 + 枚举的类型系统**：编译期即可判断插件作用域，减少 RTTI 依赖，提升安全性。
- **回调惰性构建**：Builder 在运行期才接入 `CHIRBuilder`，避免插件在加载阶段持有无效上下文，便于并发/多实例。
- **异常隔离**：加载与执行阶段均进行异常保护，可稳定地向用户报告插件失效而不影响主编译流程。
- **可扩展性**：通过模板参数扩展 `MetaKind` 即可支持新的中间表示插件（如 AST、BCHIR），对现有插件无侵入。

## 5. 代码优势与功能价值
- **强拓展能力**：无需改动核心编译器即可上线业务特定语义转换，降低主干维护成本。
- **一致的 API 体验**：统一的宏与 Builder 机制让插件在不同团队之间可复用、可共享。
- **调试友好**：可选的 `IRCheck` 与 `DumpCHIRDebug` 帮助插件开发者快速定位元变换后的 IR 问题。
- **安全与稳定**：版本校验、防御式异常处理、RAII 式资源管理确保插件生态不会破坏编译器稳定性。

## 6. 商业场景应用示例
- **行业特化优化**：金融或运营商行业可在包级插件中注入风控检查、交易补偿逻辑，实现跨项目统一治理。
- **安全审计/合规**：函数级插件可自动为敏感 API 注入日志与权限校验代码，满足监管要求的同时保持代码整洁。
- **跨语言桥接**：插件能在 CHIR 层生成与 C/C++/Rust 的互操作包装，帮助企业平滑迁移遗留系统。
- **性能监控与 A/B 调试**：插件按需给性能热点函数植入轻量指标汇报，支撑业务实时观测与快速回滚。

---
若需补充示例代码、插件模板或体系图，可基于该 Markdown 再扩展为内部开发指南或分享材料。  

