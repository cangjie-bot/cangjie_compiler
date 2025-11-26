# 仓颉编译器 MetaTransformation 插件机制总结

## 1. 背景与目标

MetaTransformation 插件体系允许在 CHIR（Cangjie High-Level IR）层引入自定义变换，以满足领域化优化、代码生成增强和工具链扩展等需求。该体系定位为“编译器内核与外部生态之间的扩展桥梁”，通过运行时加载插件的方式，保证主线编译流程保持轻量，同时支持按需插拔的高级能力。

## 2. 编译插件执行流程

1. **解析插件路径**：用户通过 `--plugin` 等编译参数传入动态库路径，`CompilerInvocation` 将其写入 `globalOptions.pluginPaths`。
2. **加载并校验插件**：`CompilerInstance::PerformPluginLoad` 依次打开动态库，调用导出符号 `getMetaTransformPluginInfo` 获取插件元信息、版本号与注册函数；若版本不匹配或符号缺失则诊断失败。
3. **注册回调**：插件在 `registerTo(MetaTransformPluginBuilder&)` 回调中，通过 `MetaTransformPluginBuilder::RegisterCHIRPluginCallback` 将“构建时回调”入队，回调函数负责往 `CHIRPluginManager` 注入具体的 MetaTransform 实例。
4. **编译过程中触发**：在前端生成 CHIR 后，`ToCHIR::PerformPlugin` 创建 `CHIRPluginManager`，对所有 MetaTransformConcept 逐一执行：若声明为函数级（`MetaTransform<CHIR::Func>`）则遍历包内函数逐一运行；若为包级（`MetaTransform<CHIR::Package>`）则直接对包执行。执行完毕可选进行 IR Check 以确保变换合法。

## 3. 架构设计概览

- **Plugin Loader 层**：负责动态库加载、版本校验、句柄管理以及异常隔离，位于 `Frontend::CompilerInstance`。
- **Builder/Manager 层**：`MetaTransformPluginBuilder` 负责收集插件提供的构建器回调，`CHIRPluginManager` 则承载最终可执行的变换序列，并提供统一迭代接口。
- **Transform 抽象层**：`MetaTransformConcept` 与 `MetaTransform<T>` 模板定义函数级 / 包级变换的生命周期与运行接口。
- **执行管线**：在 CHIR 生成阶段插入 Profile 记录，按插件声明顺序执行；失败会触发诊断，成功则可启用 `IRCheck` 防守式验证。

该架构解耦了插件注册、生命周期管理与实际执行，确保易扩展、可热插拔、可观测。

## 4. 核心数据结构

`include/cangjie/MetaTransformation/MetaTransform.h` 定义了插件抽象、管理器以及插件信息等关键结构：

```27:152:include/cangjie/MetaTransformation/MetaTransform.h
enum class MetaTransformKind { UNKNOWN, FOR_CHIR_FUNC, FOR_CHIR_PACKAGE, FOR_CHIR };
struct MetaTransformConcept { ... bool IsForFunc() const; bool IsForPackage() const; };
template <typename DeclT> struct MetaTransform : public MetaTransformConcept {
    virtual void Run(DeclT&) = 0;
    MetaTransform() { if constexpr (std::is_same_v<DeclT, CHIR::Func>) { kind = MetaTransformKind::FOR_CHIR_FUNC; } ... }
};
template <typename MetaKindT> class MetaTransformPluginManager {
    void ForEachMetaTransformConcept(std::function<void(MetaTransformConcept&)> action);
};
struct MetaTransformPluginInfo {
    const char* cjcVersion;
    void (*registerTo)(MetaTransformPluginBuilder&);
};
#define CHIR_PLUGIN(plugin_name) extern "C" MetaTransformPluginInfo getMetaTransformPluginInfo() { ... }
```

该文件还提供 `MetaTransformPluginBuilder` 与 `CHIR_PLUGIN` 宏，帮助手写插件时自动注册。

## 5. 关键实现细节

- **插件加载**：`CompilerInstance::PerformPluginLoad` 使用 `InvokeRuntime::OpenSymbolTable` 打开动态库，取出 `MetaTransformPluginInfo`，若 `cjcVersion` 与编译器不一致立即诊断；插件句柄保存在 `CompilerInstance`，用于编译结束时统一释放（参见 `src/Frontend/CompilerInstance.cpp` 中 162–245 行）。
- **执行入口**：`src/CHIR/CHIR.cpp` 的 `ToCHIR::PerformPlugin` 负责构建 `CHIRPluginManager`、遍历插件、按函数/包粒度执行，并在开启 IR Checker 时对“插件后 IR”进行合法性校验，保证变换不会破坏后续编译（1288–1315 行）。
- **异常安全**：若编译配置未开启覆盖测试（`CANGJIE_ENABLE_GCOV`），插件执行包裹在 try-catch 中，捕获任意异常并上报 `plugin_throws_exception` 诊断，从而隔离第三方插件风险。

## 6. 代码层面的优势

- **强类型抽象**：通过模板推断自动设置 `MetaTransformKind`，避免插件作者手工标注，降低误用概率。
- **统一生命周期管理**：Builder 收集回调、Manager 负责实例与遍历，清晰分工便于维护。
- **运行时安全保障**：覆盖版本校验、异常捕获、可选 IRCheck 与 profile 记录，使得插件扩展既安全又可观察。
- **宏辅助注册**：`CHIR_PLUGIN` 宏封装了导出符号、版本透传与实例化逻辑，插件开发者仅需关注核心变换。

## 7. 功能价值

- **领域优化**：可在 CHIR 层实现特定行业、特定框架的模式替换与内联，获得额外性能。
- **跨团队扩展**：第三方可按需加载插件，而无需修改主线编译器，降低升级阻力。
- **调试与审计**：通过插件可注入额外的诊断 / 指标收集逻辑，例如记录函数级元数据、生成静态分析报告等。
- **合规与安全**：在 IR 层静态检测敏感 API、注入安全桩代码，为行业监管提供自动化手段。

## 8. 商业场景示例

1. **金融风控 DSL**：银行可通过插件把内部 DSL 语义直接降解为高性能 CHIR 模式，减少多语言转换成本，同时保证编译期审计。
2. **车载安全审计**：车企可在编译阶段插入必须的遥测或安全断言，自动检测是否调用未认证模块，满足功能安全合规。
3. **SaaS 多租户优化**：SaaS 厂商针对热点租户函数注入内联缓存或特定硬件指令优化，无需 fork 主仓即可快速迭代。
4. **AIOps 可观测性**：云平台利用插件在编译期植入调试钩子与指标上报逻辑，在不改动业务代码的情况下增强可观测能力。

---

本总结可配合后续的 PPT（`doc/MetaTransformation插件机制.pptx`）向业务 / 架构团队汇报，帮助其快速理解仓颉编译插件能力与落地方向。

关键要点
插件动态库在前端加载阶段完成打开、版本校验与回调注册，随后统一交由 MetaTransformPluginBuilder 管理，确保安全性与热插拔能力。
CompilerInstance.cppLines 196-245
MetaTransformPlugin MetaTransformPlugin::Get(const std::string& path){    HANDLE handle = nullptr;#ifdef _WIN32    handle = InvokeRuntime::OpenSymbolTable(path);#elif defined(__linux__) || defined(__APPLE__)    handle = InvokeRuntime::OpenSymbolTable(path, RTLD_NOW | RTLD_LOCAL);#endif    if (!handle) {        throw NullPointerException();    }    void* fPtr = InvokeRuntime::GetMethod(handle, "getMetaTransformPluginInfo");    auto pluginInfo = reinterpret_cast<MetaTransformPluginInfo (*)()>(fPtr)();    return MetaTransformPlugin(path, pluginInfo, handle);}...bool CompilerInstance::PerformPluginLoad(){    for (auto pluginPath : invocation.globalOptions.pluginPaths) {        auto metaTransformPlugin = MetaTransformPlugin::Get(pluginPath);        AddPluginHandle(metaTransformPlugin.GetHandle());        metaTransformPlugin.RegisterCallbackTo(metaTransformPluginBuilder);    }    return true;}
CHIR 生成后通过 BuildCHIRPluginManager 构造插件序列，函数级与包级变换均可在同一遍历中执行，并提供 IRCheck 护栏。
CHIR.cppLines 1278-1315
bool ToCHIR::PerformPlugin(CHIR::Package& package){    bool succeed = true;    bool hasPluginForCHIR = false;    Utils::ProfileRecorder recorder("CHIR", "Plugin Execution");    CHIRPluginManager chirPluginManager = ci.metaTransformPluginBuilder.BuildCHIRPluginManager(builder);    chirPluginManager.ForEachMetaTransformConcept([&package, &hasPluginForCHIR](MetaTransformConcept& mtc) {        if (!mtc.IsForCHIR()) {            return;        }        hasPluginForCHIR = true;        if (mtc.IsForFunc()) {            for (auto func : package.GetGlobalFuncs()) {                static_cast<MetaTransform<CHIR::Func>*>(&mtc)->Run(*func);            }        } else if (mtc.IsForPackage()) {            static_cast<MetaTransform<CHIR::Package>*>(&mtc)->Run(package);        }    });    if (!succeed) {        diag.DiagnoseRefactor(DiagKindRefactor::plugin_throws_exception, DEFAULT_POSITION);    } else if (hasPluginForCHIR && builder.IsEnableIRCheckerAfterPlugin()) {        succeed = IRCheck(package, opts, builder, Phase::PLUGIN);    }    return succeed;}
MetaTransformKind、MetaTransformConcept、MetaTransformPluginManager 与 CHIR_PLUGIN 宏明确了插件声明、注册与执行契约，简化扩展开发并加强类型约束。
MetaTransform.hLines 27-152
enum class MetaTransformKind { UNKNOWN, FOR_CHIR_FUNC, FOR_CHIR_PACKAGE, FOR_CHIR };struct MetaTransformConcept {    bool IsForFunc() const { return kind == MetaTransformKind::FOR_CHIR_FUNC; }    bool IsForPackage() const { return kind == MetaTransformKind::FOR_CHIR_PACKAGE; }protected:    MetaTransformKind kind = MetaTransformKind::UNKNOWN;};template <typename DeclT> struct MetaTransform : public MetaTransformConcept {    virtual void Run(DeclT&) = 0;    MetaTransform() {        if constexpr (std::is_same_v<DeclT, CHIR::Func>) {            kind = MetaTransformKind::FOR_CHIR_FUNC;        } else if constexpr (std::is_same_v<DeclT, CHIR::Package>) {            kind = MetaTransformKind::FOR_CHIR_PACKAGE;        }    }};template <typename MetaKindT> class MetaTransformPluginManager {    void ForEachMetaTransformConcept(std::function<void(MetaTransformConcept&)> action) {        for (auto& mtc : mtConcepts) {            action(*mtc);        }    }};#define CHIR_PLUGIN(plugin_name) \    extern "C" MetaTransformPluginInfo getMetaTransformPluginInfo() { \        return {Cangjie::CANGJIE_VERSION.c_str(), [](MetaTransformPluginBuilder& mtBuilder) { \            mtBuilder.RegisterCHIRPluginCallback([](CHIRPluginManager& mtm, CHIR::CHIRBuilder& builder) { \                mtm.AddMetaTransform(std::make_unique<plugin_name>(builder)); \            }); \        }}; \    }
使用提示