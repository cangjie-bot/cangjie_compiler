## MetaTransformation 插件体系设计文档

### 一、概述

**MetaTransformation** 是仓颉编译器在 **CHIR（Cangjie High-Level IR）层** 提供的一套插件化元变换体系。  
它的目标是：

- **作为编译器内核与外部生态之间的扩展桥梁**：
  - 支持业务方、第三方在不修改编译器主干代码的前提下，对 IR 进行定制变换。
- **支持按需插拔的领域化能力**：
  - 例如领域优化、代码生成增强、安全审计、可观测性埋点等。
- **保持主线编译流程轻量与稳定**：
  - 插件以动态库方式加载，版本校验、异常隔离、IR 校验保证主流程安全。

---

### 二、整体架构设计

从编译流程视角，MetaTransformation 体系大致分为四层：

- **1）Plugin Loader 层（插件加载层）**
  - 所在位置：`Frontend::CompilerInstance`。
  - 主要职责：
    - 根据编译参数（如 `--plugin`）解析动态库路径。
    - 使用运行时接口打开动态库，查找导出符号 `getMetaTransformPluginInfo`。
    - 校验插件声明的 `cjcVersion` 是否与当前编译器版本匹配。
    - 缓存插件句柄，负责资源回收与异常隔离。

- **2）Builder 层（构建器层）**
  - 核心类型：`MetaTransformPluginBuilder`。
  - 主要职责：
    - 提供 `RegisterCHIRPluginCallback` 接口，供插件在 `registerTo` 中注册“构建回调”。
    - 在实际需要执行插件时，调用 `BuildCHIRPluginManager`，把所有回调转化为具体的 CHIR 变换实例。

- **3）Manager 层（管理器层）**
  - 核心类型：`MetaTransformPluginManager<MetaKind::CHIR>`，别名 `CHIRPluginManager`。
  - 主要职责：
    - 维护一个 `vector<unique_ptr<MetaTransformConcept>>`，存放所有已实例化的变换对象。
    - 提供 `AddMetaTransform` / `ForEachMetaTransformConcept`，统一管理和遍历所有变换。

- **4）Transform 抽象与执行层**
  - 抽象：`MetaTransformConcept` + `MetaTransform<DeclT>` 模板。
  - 执行：在 `ToCHIR::PerformPlugin` 中，对不同粒度（函数级/包级）的变换进行统一调度，执行完成后可选进行 IRCheck。

这种分层设计把 **“插件注册与加载”**、**“插件实例与生命周期”**、**“实际变换执行”** 彻底解耦，保证了：

- 插件开发者只需关注 `Run(DeclT&)` 的业务逻辑。
- 编译器可以稳定地控制加载、执行、回收与诊断。

---

### 三、核心数据结构设计

#### 3.1 变换类型枚举：`MetaTransformKind`

```cpp
enum class MetaTransformKind {
    UNKNOWN,
    FOR_CHIR_FUNC,
    FOR_CHIR_PACKAGE,
    FOR_CHIR,
};
```

- **作用**：标识变换的适用范围：
  - `FOR_CHIR_FUNC`：函数级变换（作用于 `CHIR::Func`）。
  - `FOR_CHIR_PACKAGE`：包级变换（作用于 `CHIR::Package`）。
  - `FOR_CHIR`：为后续扩展预留的 CHIR 级别标记。
- 插件执行时通过该枚举决定如何调度。

#### 3.2 抽象基类：`MetaTransformConcept`

```cpp
struct MetaTransformConcept {
    virtual ~MetaTransformConcept() = default;

    bool IsForCHIR() const { return kind > MetaTransformKind::UNKNOWN && kind < MetaTransformKind::FOR_CHIR; }
    bool IsForFunc() const { return kind == MetaTransformKind::FOR_CHIR_FUNC; }
    bool IsForPackage() const { return kind == MetaTransformKind::FOR_CHIR_PACKAGE; }

protected:
    MetaTransformKind kind = MetaTransformKind::UNKNOWN;
};
```

- **职责**：
  - 提供统一的多态基类。
  - 封装变换类别判断接口 `IsForFunc/IsForPackage/IsForCHIR`。
- **意义**：
  - 允许 `MetaTransformPluginManager` 以统一接口管理不同模板实例的变换。

#### 3.3 模板基类：`MetaTransform<DeclT>`

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

- **职责**：
  - 定义插件需要实现的唯一核心接口：`void Run(DeclT&)`。
  - 在构造函数里基于 `DeclT` **自动推导并设置 `MetaTransformKind`**，避免插件作者手动维护。
- **典型用法**：
  - 函数级插件：`class MyFuncPlugin : public MetaTransform<CHIR::Func> { ... }`
  - 包级插件：`class MyPkgPlugin : public MetaTransform<CHIR::Package> { ... }`

#### 3.4 插件管理器：`MetaTransformPluginManager<MetaKindT>`

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
```

- **职责**：
  - 存放所有 `MetaTransformConcept` 子类对象。
  - 对外提供统一的遍历接口，供执行管线使用。
- **专门化**：
  - `using CHIRPluginManager = MetaTransformPluginManager<MetaKind::CHIR>;`

#### 3.5 插件构建器：`MetaTransformPluginBuilder`

```cpp
class MetaTransformPluginBuilder {
public:
    void RegisterCHIRPluginCallback(std::function<void(CHIRPluginManager&, CHIR::CHIRBuilder&)> callback) {
        chirPluginCallbacks.emplace_back(callback);
    }

    CHIRPluginManager BuildCHIRPluginManager(CHIR::CHIRBuilder& builder);

private:
    std::vector<std::function<void(CHIRPluginManager&, CHIR::CHIRBuilder&)>> chirPluginCallbacks;
};
```

- **职责**：
  - 记录所有插件在加载阶段注册进来的“构建回调”。
  - 在 `BuildCHIRPluginManager` 中创建一个 `CHIRPluginManager`，并依次调用所有回调，把各个插件实例注入进去。

#### 3.6 插件导出信息：`MetaTransformPluginInfo` 与宏 `CHIR_PLUGIN`

```cpp
struct MetaTransformPluginInfo {
    const char* cjcVersion;
    void (*registerTo)(MetaTransformPluginBuilder&);
    /* 可扩展字段，如插件名、顺序等 */
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

- **`MetaTransformPluginInfo`**：
  - `cjcVersion`：插件编译时绑定的编译器版本号，用于运行时版本校验。
  - `registerTo`：插件对外导出的注册函数指针，用于把插件注册到 `MetaTransformPluginBuilder`。
- **`CHIR_PLUGIN` 宏**：
  - 自动生成 `extern "C" MetaTransformPluginInfo getMetaTransformPluginInfo()`。
  - 封装版本号透传、注册回调、实例化 `plugin_name` 的逻辑。
  - 极大降低插件样板代码，使插件开发聚焦在业务逻辑上。

---

### 四、实现方案与执行流程

#### 4.1 插件加载阶段（Frontend）

1. **解析插件路径**
   - 用户通过 `--plugin` 等编译参数传入动态库路径。
   - `CompilerInvocation` 将路径存入 `invocation.globalOptions.pluginPaths`。

2. **打开动态库与获取导出信息**
   - `CompilerInstance::PerformPluginLoad` 遍历每个路径：
     - 调用 `MetaTransformPlugin::Get(path)`：
       - 通过 `InvokeRuntime::OpenSymbolTable` 打开动态库，获取句柄。
       - 查找符号 `getMetaTransformPluginInfo`。
       - 调用该函数获得 `MetaTransformPluginInfo`，包含 `cjcVersion` 与 `registerTo`。
     - 版本不匹配或符号缺失会触发诊断并中止该插件加载。

3. **注册构建回调**
   - 对每个成功加载的插件：
     - 调用其 `registerTo(MetaTransformPluginBuilder&)`。
     - 插件在其中调用 `MetaTransformPluginBuilder::RegisterCHIRPluginCallback` 注册一个回调：
       - 回调的逻辑是：在未来构建 `CHIRPluginManager` 时，向其中 `AddMetaTransform(std::make_unique<plugin_name>(builder))`。

4. **资源管理与异常防护**
   - 成功加载的插件句柄添加到 `CompilerInstance`，在编译结束统一关闭。
   - 打开动态库或获取符号失败会抛出异常，并转换为编译诊断。

#### 4.2 CHIR 阶段构建与执行插件

1. **构建 CHIR 插件管理器**
   - 在 CHIR 生成后，`ToCHIR::PerformPlugin` 调用：

     ```cpp
     CHIRPluginManager chirPluginManager =
         ci.metaTransformPluginBuilder.BuildCHIRPluginManager(builder);
     ```

   - `BuildCHIRPluginManager`：
     - 创建一个空的 `CHIRPluginManager`。
     - 依次执行所有注册的回调，每个回调都可以：
       - 使用当前 `CHIR::CHIRBuilder` 创建具体的 `MetaTransform<CHIR::Func>` / `MetaTransform<CHIR::Package>` 实例。
       - 调用 `AddMetaTransform` 将其实例化并加入管理器。

2. **执行插件变换**
   - `ToCHIR::PerformPlugin` 调用：

     ```cpp
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

   - 执行策略：
     - 筛选出 `IsForCHIR()` 的插件。
     - 若为函数级：
       - 遍历 `Package.GetGlobalFuncs()`，对每个函数调用 `Run(*func)`。
     - 若为包级：
       - 直接对 `package` 调用 `Run(package)`。

3. **异常与 IR 校验**
   - 插件执行通常被 try/catch 包裹：
     - 捕获任何异常，报告 `plugin_throws_exception` 诊断。
     - 防止外部插件破坏编译器内部状态。
   - 若开启 `IRCheckerAfterPlugin`：
     - 在至少有一个插件运行且执行成功后，触发 IRCheck：
       - 对“插件变换后的 CHIR”进行语义一致性和结构合法性校验。
       - 避免插件生成非法 IR 影响后续优化和代码生成。

---

### 五、代码层面的优势

- **1）强类型与自动分类**
  - 利用模板 + `if constexpr` 自动推断 `MetaTransformKind`，减少手工配置，避免错标函数级/包级。
  - 通过 `MetaTransformConcept` 统一管理，既有强类型，又可在运行时按类别调度。

- **2）清晰的生命周期与职责分离**
  - Loader：只负责动态库加载与版本/符号校验。
  - Builder：记录回调，不直接持有 IR 上下文。
  - Manager：负责具体实例的存储和遍历。
  - 执行管线：只面向抽象接口 `Run(DeclT&)`，不关心插件加载细节。

- **3）安全防护完善**
  - 版本号一致性校验，防止旧插件在新编译器上运行导致 ABI/语义问题。
  - 异常捕获 + 诊断，隔离第三方插件的崩溃风险。
  - IRCheck 作为“防守式编程”的最后一道防线，避免非法 IR 流入后端。

- **4）开发体验友好**
  - `CHIR_PLUGIN(plugin_name)` 宏隐藏了绝大部分样板代码：
    - 动态库导出符号。
    - 版本号透传。
    - 注册回调和实例化逻辑。
  - 插件作者几乎只需要：
    - 继承 `MetaTransform<CHIR::Func>` 或 `MetaTransform<CHIR::Package>`。
    - 实现 `Run` 函数。
    - 在插件源文件末尾写一行 `CHIR_PLUGIN(MyPlugin)`。

- **5）可扩展性良好**
  - 通过扩展 `MetaKind` 可以支持针对 AST、其它 IR（如 BCHIR）等不同阶段的插件。
  - 对现有插件无侵入，方便未来演进。

---

### 六、功能价值总结

- **领域化优化与变换**
  - 不同行业/业务可在 CHIR 阶段实现特定模式匹配和替换：
    - 例如自动内联特定框架 API、将高层 DSL 语义映射为底层高效指令序列。
- **跨团队、跨项目的扩展能力**
  - 各团队可独立维护自己的插件动态库：
    - 不需要修改或 fork 主仓。
    - 升级编译器时只需保证版本兼容，维护成本低。
- **调试与可观测性增强**
  - 插件可以在编译期注入日志、性能计数器、埋点等：
    - 在不侵入业务代码的前提下，增强运行时可观测性。
- **安全与合规保障**
  - 在 IR 层实现静态规则校验、敏感 API 扫描、安全桩代码注入：
    - 满足金融、车载、政企等高合规行业的监管诉求。

---

### 七、商业场景应用示例与价值

- **1）金融风控 DSL 编译加速**
  - 场景：
    - 银行/证券有自研风控 DSL，需要在生产环境中高效执行。
  - 插件方案：
    - 在 CHIR 插件中，将 DSL 生成的中间调用模式识别出来，替换为专门优化过的 CHIR 模式（如特定的内联、批量处理指令）。
  - 价值：
    - 降低多级解释/转换开销，提升风控规则执行性能。
    - 在编译期即可对规则进行一致性与安全性审计。

- **2）车载/工业领域的安全审计与防护**
  - 场景：
    - 车载 ECU、工业控制系统要求严格的安全与功能安全（如 ISO 26262）。
  - 插件方案：
    - 函数级插件对所有调用安全关键 API 的函数：
      - 自动注入运行时检查、日志记录或冗余校验码。
    - 包级插件对整个包执行安全策略一致性检查。
  - 价值：
    - 把安全策略“固化”在编译期，减少人工审查漏洞。
    - 满足审计和合规要求，减少线下故障排查成本。

- **3）SaaS 多租户性能优化**
  - 场景：
    - 多租户 SaaS 平台中，需要针对特定大客或热点模块进行高强度优化。
  - 插件方案：
    - 函数级插件在编译期识别热点函数，并针对特定硬件能力（SIMD、加密指令等）生成优化代码路径。
    - 或自动加入缓存逻辑、短路逻辑。
  - 价值：
    - 无需为每个客户维护一套 fork 版代码。
    - 通过插件按需增强性能，实现“热租户”优化。

- **4）AIOps 及运维可观测性增强**
  - 场景：
    - 云平台/大型分布式系统需要统一的指标采集与调试能力。
  - 插件方案：
    - 在 CHIR 层统一为关键函数注入指标上报/trace id 传播逻辑。
    - 根据环境配置选择开启/关闭不同层次的埋点。
  - 价值：
    - 不修改业务代码即可获得统一、标准化的可观测性。
    - 支持灰度、A/B 实验，以及快速问题定位和回滚。

- **5）跨语言互操作与遗留系统迁移**
  - 场景：
    - 企业希望用仓颉接入既有 C/C++/Rust 组件，同时逐步替换旧系统。
  - 插件方案：
    - 在 CHIR 插件中自动为特定类型或调用模式生成跨语言桥接代码：
      - 包括 FFI 封装、错误码转换、资源生命周期管理等。
  - 价值：
    - 降低迁移门槛，用编译器插件统一管理“边界代码”，避免业务侧重复造轮子。

---

### 八、小结

- **MetaTransformation** 架构通过 **Loader / Builder / Manager / Transform** 四层解耦，实现了安全、可扩展、强类型的 CHIR 插件体系。
- 其核心数据结构（`MetaTransformKind`、`MetaTransformConcept`、`MetaTransform<DeclT>`、`MetaTransformPluginManager`、`MetaTransformPluginBuilder`、`MetaTransformPluginInfo`）构成了一套清晰的插件协议。
- 在实现层面，通过动态库加载、版本校验、异常隔离和 IRCheck 等机制，保证插件生态不会损害编译器主流程的稳定性。
- 在商业应用层面，它为领域优化、安全合规、可观测性和跨语言演进提供了高价值的基础设施，是编译器产品化与生态化的重要支撑点。


