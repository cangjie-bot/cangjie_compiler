## Cangjie 编译插件流程与 MetaTransformation 架构设计文档

### 一、总体概述

仓颉编译器通过 **MetaTransformation 插件体系**，在 **CHIR（Cangjie High-Level IR）阶段** 提供一个可插拔、可演进的扩展机制，使得：

- **业务方可以在不修改编译器主干代码的前提下，对 IR 进行定制变换**；
- **第三方可以实现领域化优化、合规审计、可观测性埋点、跨语言桥接等高级能力**；
- **编译器主线保持安全稳定**（版本校验、异常隔离、IR 校验）。

整体可以概括为一句话：**用插件把“编译器内核能力”与“业务/行业扩展逻辑”解耦，为仓颉生态提供可持续的扩展点。**

---

### 二、Cangjie 编译插件流程

#### 2.1 插件配置与路径解析

- 用户在编译命令行中使用类似 `--plugin=/path/to/plugin.so`（或平台对应动态库）指定插件。
- 前端组件 `CompilerInvocation`：
  - 解析命令行选项；
  - 将插件路径收集到 `globalOptions.pluginPaths`。

#### 2.2 插件加载（Load Plugins）

- `CompilerInstance::PerformPluginLoad` 负责加载所有插件：
  1. 遍历 `globalOptions.pluginPaths`。
  2. 对每个路径调用 `MetaTransformPlugin::Get(path)`：
     - 使用 `InvokeRuntime::OpenSymbolTable` 打开动态库，获取句柄。
     - 查找导出符号 `getMetaTransformPluginInfo`。
     - 调用该函数获得 `MetaTransformPluginInfo`：
       - 其中包含 `cjcVersion`（插件编译时绑定的编译器版本号）；
       - 以及 `registerTo(MetaTransformPluginBuilder&)` 注册函数。
  3. 校验 `cjcVersion` 是否与当前编译器版本一致：
     - 不一致则报告版本不兼容诊断，拒绝加载该插件。
  4. 将动态库句柄缓存到 `CompilerInstance`，便于编译结束统一释放。

- 同时，`MetaTransformPlugin::RegisterCallbackTo` 会调用：
  - `registerTo(metaTransformPluginBuilder)`，由插件向 `MetaTransformPluginBuilder` 注册构建回调。

#### 2.3 插件构建（Build Plugin Manager）

- 在前端生成 CHIR 后，CHIR 阶段的转换类（如 `ToCHIR`）会在适当时机调用：

  ```cpp
  CHIRPluginManager chirPluginManager =
      ci.metaTransformPluginBuilder.BuildCHIRPluginManager(builder);
  ```

- `MetaTransformPluginBuilder::BuildCHIRPluginManager`：
  - 创建一个空的 `CHIRPluginManager`；
  - 依次执行所有在加载阶段注册进来的回调：
    - 每个回调都可以基于当前 `CHIR::CHIRBuilder` 创建一个或多个 `MetaTransform<CHIR::Func>` / `MetaTransform<CHIR::Package>` 实例；
    - 调用 `CHIRPluginManager::AddMetaTransform` 注入到管理器中。

#### 2.4 插件执行（Perform Plugin）

- `ToCHIR::PerformPlugin(CHIR::Package& package)` 承担插件执行逻辑，大致流程：

  ```cpp
  bool succeed = true;
  bool hasPluginForCHIR = false;
  Utils::ProfileRecorder recorder("CHIR", "Plugin Execution");

  CHIRPluginManager chirPluginManager =
      ci.metaTransformPluginBuilder.BuildCHIRPluginManager(builder);

  chirPluginManager.ForEachMetaTransformConcept(
      [&package, &hasPluginForCHIR](MetaTransformConcept& mtc) {
          if (!mtc.IsForCHIR()) {
              return;
          }
          hasPluginForCHIR = true;
          if (mtc.IsForFunc()) {
              for (auto func : package.GetGlobalFuncs()) {
                  static_cast<MetaTransform<CHIR::Func>*>(&mtc)->Run(*func);
              }
          } else if (mtc.IsForPackage()) {
              static_cast<MetaTransform<CHIR::Package>*>(&mtc)->Run(package);
          }
      });
  ```

- 核心行为：
  - 按序遍历所有插件实例；  
  - 只执行对 CHIR 生效的变换（`IsForCHIR()`）；  
  - 函数级变换：对 `Package.GetGlobalFuncs()` 中每个函数执行 `Run(func)`；  
  - 包级变换：直接对 `package` 执行 `Run(package)`。

- 若启用了 IR 检查：
  - 在执行完所有插件、且存在至少一个 CHIR 插件成功运行后：
    - 启动 IRCheck，对“已经被插件改写后的 CHIR”进行合法性校验。

#### 2.5 异常处理与诊断

- 插件执行被 try/catch 包裹（在非覆盖测试模式下）：
  - 任意未捕获异常都会触发 `plugin_throws_exception` 诊断；
  - 编译流程会中止或标记失败，防止状态污染。
- 结合 Profile 记录（如 `ProfileRecorder("CHIR", "Plugin Execution")`），可以诊断插件耗时与性能问题。

---

### 三、MetaTransformation 架构设计

#### 3.1 关键角色与分层

- **Plugin Loader（插件加载层）**
  - 模块：`CompilerInstance` + `MetaTransformPlugin` + `InvokeRuntime`
  - 职责：动态库加载、版本校验、符号查找、异常隔离、资源管理。

- **MetaTransformPluginBuilder（构建器层）**
  - 负责接收插件注册回调：
    - `RegisterCHIRPluginCallback(function)`；
  - 在编译过程中根据当前 `CHIRBuilder` 构建最终的插件管理器。

- **MetaTransformPluginManager（管理器层）**
  - 模板：`MetaTransformPluginManager<MetaKind::CHIR>` → `CHIRPluginManager`
  - 存放所有 `MetaTransformConcept` 派生实例；
  - 提供统一的遍历接口 `ForEachMetaTransformConcept`。

- **MetaTransform 抽象层（变换层）**
  - `MetaTransformConcept`：抽象基类，持有 `MetaTransformKind kind`；
  - `MetaTransform<DeclT>`：模板基类，定义 `Run(DeclT&)`。

整体形成 **Loader → Builder → Manager → Transform → IRCheck** 的执行链路。

---

### 四、核心数据结构（含目录与文件结构）

#### 4.1 目录与关键文件

- 头文件目录：`include/cangjie/MetaTransformation/`
  - `MetaTransform.h`：**核心类型与 API 定义**。
- 源码目录：`src/MetaTransformation/`
  - `MetaTransformPluginManager.cpp`：`MetaTransformPluginManager<MetaKind::CHIR>` 显式实例化。
  - `MetaTransformPluginBuilder.cpp`：`MetaTransformPluginBuilder::BuildCHIRPluginManager` 实现。
- 相关调用点：
  - `src/Frontend/CompilerInstance.cpp`：插件加载与句柄管理。
  - `src/CHIR/CHIR.cpp`：`ToCHIR::PerformPlugin` 执行插件。
- 文档说明：
  - `doc/MetaTransformation总结.md`、`doc/a.md`、`doc/b.md` 等：内部架构与流程说明。

#### 4.2 变换类型与抽象：`MetaTransformKind` & `MetaTransformConcept`

```cpp
enum class MetaTransformKind {
    UNKNOWN,
    FOR_CHIR_FUNC,
    FOR_CHIR_PACKAGE,
    FOR_CHIR,
};

struct MetaTransformConcept {
    virtual ~MetaTransformConcept() = default;

    bool IsForCHIR() const {
        return kind > MetaTransformKind::UNKNOWN && kind < MetaTransformKind::FOR_CHIR;
    }

    bool IsForFunc() const {
        return kind == MetaTransformKind::FOR_CHIR_FUNC;
    }

    bool IsForPackage() const {
        return kind == MetaTransformKind::FOR_CHIR_PACKAGE;
    }

protected:
    MetaTransformKind kind = MetaTransformKind::UNKNOWN;
};
```

- 提供统一的类型分类与查询接口；
- `IsForFunc/IsForPackage` 驱动执行阶段的调度逻辑。

#### 4.3 模板基类：`MetaTransform<DeclT>`

```cpp
template <typename DeclT> struct MetaTransform : public MetaTransformConcept {
public:
    virtual void Run(DeclT&) = 0;

    MetaTransform() : MetaTransformConcept() {
        if constexpr (std::is_same_v<DeclT, CHIR::Func>) {
            kind = MetaTransformKind::FOR_CHIR_FUNC;
        } else if constexpr (std::is_same_v<DeclT, CHIR::Package>) {
            kind = MetaTransformKind::FOR_CHIR_PACKAGE;
        } else {
            kind = MetaTransformKind::UNKNOWN;
        }
    }

    virtual ~MetaTransform() = default;
};
```

- 使用 `if constexpr` + `std::is_same_v`，在编译期自动设定 `kind`：
  - 避免插件开发者忘记设置分类；
  - 降低运行时类型判断复杂度。

#### 4.4 管理器：`MetaTransformPluginManager<MetaKindT>`

```cpp
template <typename MetaKindT> class MetaTransformPluginManager {
public:
    template <typename MT> void AddMetaTransform(std::unique_ptr<MT> mt) {
        mtConcepts.emplace_back(std::move(mt));
    }

    void ForEachMetaTransformConcept(std::function<void(MetaTransformConcept&)> action) {
        for (auto& mtc : mtConcepts) {
            action(*mtc);
        }
    }

private:
    std::vector<std::unique_ptr<MetaTransformConcept>> mtConcepts;
};

using CHIRPluginManager = MetaTransformPluginManager<MetaKind::CHIR>;
```

- 以 `unique_ptr<MetaTransformConcept>` 存储所有插件实例；
- 对外只暴露一个遍历接口，隐藏存储细节。

#### 4.5 构建器：`MetaTransformPluginBuilder`

```cpp
class MetaTransformPluginBuilder {
public:
    void RegisterCHIRPluginCallback(
        std::function<void(CHIRPluginManager&, CHIR::CHIRBuilder&)> callback) {
        chirPluginCallbacks.emplace_back(callback);
    }

    CHIRPluginManager BuildCHIRPluginManager(CHIR::CHIRBuilder& builder);

private:
    std::vector<std::function<void(CHIRPluginManager&, CHIR::CHIRBuilder&)>> chirPluginCallbacks;
};
```

- 插件通过 `RegisterCHIRPluginCallback` 注册构建逻辑；
- 在 `BuildCHIRPluginManager` 中把这些回调转化为真正的插件实例。

#### 4.6 插件导出信息与宏：`MetaTransformPluginInfo` & `CHIR_PLUGIN`

```cpp
struct MetaTransformPluginInfo {
    const char* cjcVersion;
    void (*registerTo)(MetaTransformPluginBuilder&);
    /* 可扩展成员：插件名、优先级、依赖关系等 */
};

#define CHIR_PLUGIN(plugin_name)                                                          \
    namespace Cangjie { extern const std::string CANGJIE_VERSION; }                       \
    extern "C" MetaTransformPluginInfo getMetaTransformPluginInfo() {                     \
        return {Cangjie::CANGJIE_VERSION.c_str(), [](MetaTransformPluginBuilder& mtBuilder) { \
                    mtBuilder.RegisterCHIRPluginCallback([](CHIRPluginManager& mtm, CHIR::CHIRBuilder& builder) { \
                        mtm.AddMetaTransform(std::make_unique<plugin_name>(builder));    \
                    });                                                                  \
                }};                                                                      \
    }
```

- 对插件开发者暴露的唯一必须实现符号：`getMetaTransformPluginInfo`。
- 宏封装：
  - 版本号透传；
  - 注册回调；
  - 在构建阶段实例化 `plugin_name`。

---

### 五、实现方案亮点与代码优点

- **1）强类型 + 模板的安全抽象**
  - 通过 `MetaTransform<DeclT>` 自动绑定 `DeclT` 与 `MetaTransformKind`，编译期确定插件作用粒度。
  - 避免运行时依赖 RTTI 或手工字符串标记，提高类型安全。

- **2）职责清晰的分层架构**
  - Loader 负责加载和版本校验；
  - Builder 只负责收集回调和构建 Manager；
  - Manager 只负责插件实例的存储和遍历；
  - Transform 层只关心 `Run(DeclT&)` 的业务逻辑；
  - 便于维护、调试和扩展。

- **3）异常隔离与防御式编程**
  - 插件执行包裹在 try/catch 中；
  - 出错统一诊断，主编译流程不被插件拖垮；
  - IRCheck 作为末端防线，防止非法 IR 流入后续阶段。

- **4）开发体验优先**
  - 插件开发“标准模版”极简：
    - 继承 `MetaTransform<CHIR::Func>` / `MetaTransform<CHIR::Package>`；
    - 实现 `Run`；
    - 用 `CHIR_PLUGIN(MyPlugin)` 暴露导出信息。
  - 几乎不需要接触复杂的 Loader/Builder 细节。

- **5）良好的扩展与演进空间**
  - 通过 `MetaKind` 模板参数，可以在将来扩展到 AST/BCHIR 等阶段；
  - `MetaTransformPluginInfo` 可拓展更多插件元数据（顺序、依赖、开关等），实现更精细的调度策略。

---

### 六、功能价值总结

- **领域化优化能力**
  - 在 CHIR 层实现行业/框架特定的模式识别与变换：
    - 如自动内联框架核心 API、把高层 DSL 转化为高效指令序列。
- **跨团队扩展与协作**
  - 各团队通过维护独立插件库扩展编译器能力，而无需修改主仓或强行 Fork。
- **调试与可观测性增强**
  - 插件注入调试钩子、性能计数器、Trace ID 传播逻辑，实现零侵入的可观测性。
- **安全与合规保障**
  - 在 IR 层实现敏感 API 扫描、策略校验、安全桩代码注入，满足金融、车载、政企等高合规行业要求。

---

### 七、典型商业场景与价值示例

- **1）金融风控 DSL 编译优化**
  - 需求：
    - 金融机构内部有自定义风控 DSL，需要高性能执行与审计。
  - 插件做法：
    - 在 CHIR 插件中，对由 DSL 编译器生成的 CHIR 模式进行专门识别和优化；
    - 将高层规则编译为少量优化过的底层指令序列。
  - 价值：
    - 提升规则执行性能，减少多级解释开销；
    - 在编译时完成规则审计与一致性检查。

- **2）车载/工业控制安全审计**
  - 需求：
    - 满足 ISO 26262 等功能安全标准，需要对关键函数插入安全防护逻辑。
  - 插件做法：
    - 函数级插件扫描所有调用安全关键 API 的函数；
    - 自动注入日志、冗余检查、容错代码；
    - 包级插件检查包级安全策略（如权限边界、模块依赖）。
  - 价值：
    - 把安全策略固化在编译流程，减少人工审计工作量；
    - 降低安全漏洞流入生产的概率。

- **3）SaaS 多租户性能与成本优化**
  - 需求：
    - 云端 SaaS 平台需要对部分大客或热点路径做精细化性能优化。
  - 插件做法：
    - 函数级插件在编译期识别热点调用；
    - 针对目标硬件（SIMD、AES 指令等）生成优化路径；
    - 或插入租户特定的缓存逻辑。
  - 价值：
    - 无需 Fork 业务代码即可提供“VIP 优化套餐”；
    - 降低整体计算资源消耗。

- **4）AIOps 与运维可观测性增强**
  - 需求：
    - 大规模分布式系统需要统一、可配置的指标采集与故障定位能力。
  - 插件做法：
    - 在 CHIR 层对关键路径函数植入埋点、Trace、Metrics；
    - 通过编译配置控制开关与采样率。
  - 价值：
    - 业务代码保持简洁，运维与调试能力集中在编译插件中；
    - 快速切换不同环境（开发 / 预发 / 线上）的观测策略。

- **5）跨语言互操作与遗留系统迁移**
  - 需求：
    - 需要与大量 C/C++/Rust 遗留系统互操作，并逐步迁移到仓颉。
  - 插件做法：
    - 在 CHIR 插件中自动为特定类型或调用模式生成 FFI 封装、错误码映射、资源管理代码。
  - 价值：
    - 降低迁移门槛，统一边界逻辑；
    - 减少手写桥接层的错误风险和维护成本。

---

### 八、结语

- **Cangjie MetaTransformation 插件体系** 通过 **清晰的分层架构、强类型抽象、安全的执行模型**，在 CHIR 阶段提供了强大的扩展能力。
- 它不仅解决了“如何让业务方安全地扩展编译器”的问题，也为“如何让编译器成为企业工程基础设施的一部分”提供了现实可行的方案。
- 在实际商业落地中，可以围绕性能、合规、安全、可观测性、跨语言互操作等多个维度构建插件生态，为仓颉语言和编译器持续赋能。


