# 支持仓颉语言服务 lsp 增量分析方案

## 背景

当前lsp调用内核的接口进行语义分析时每次都是全量处理得到带有语义的ast。

目前每次全量重新执行，耗时较久，以kei场景中的诊断功能为例，一次编译耗时xxx, 其中 parse 占x%, ConditionCompile 占x%, ImportPackage 占 x%， MacroExpand占 x%, sema 占 50%

```mermaid
pie title 编译耗时分布（示例）
    "Sema" : 50
    "Parse" : 15
    "ConditionCompile" : 10
    "ImportPackage" : 15
    "MacroExpand" : 10
```

## 方案设计与分析

仓颉语言服务有非常多种业务场景，但总的可以分为代码诊断类和补全联想两类场景，前者需要执行到语义分析，后者则可以复用带有语义的ast后，再执行展开前的阶段即可

代码诊断类流程：

1. 语义分析获取带有语义的ast
Parse->ConditionCompile->ImportPackage->MacroExpand->Sema

补全联想类流程：

1. 查找缓存的带有语义的ast，若没有缓存的带有语义的ast则执行代码诊断类流程获取带有语义的ast
Parse->ConditionCompile->ImportPackage->MacroExpand->Sema

2. 获取带有语义的ast后，重新执行语法解析、条件编译、包导入获取不带语义的ast用于补全联想。
Parse->ConditionCompile->ImportPackage(可选，未修改导入语句部分不需要执行)

```mermaid
graph TD
    subgraph "代码诊断类流程"
        A1[Parse] --> B1[ConditionCompile]
        B1 --> C1[ImportPackage]
        C1 --> D1[MacroExpand]
        D1 --> E1[Sema]
        E1 --> F1[带语义的AST]
    end
    
    subgraph "补全联想类流程"
        A2{有缓存的<br/>带语义AST?}
        A2 -->|否| B2[执行代码诊断类流程]
        A2 -->|是| C2[复用缓存的AST]
        B2 --> C2
        C2 --> D2[Parse]
        D2 --> E2[ConditionCompile]
        E2 --> F2[ImportPackage<br/>可选]
        F2 --> G2[不带语义的AST<br/>用于补全]
    end
```

主要的分析流程中 Parse，ConditionCompile，MacroExpand 可以较自然的按照文件的维度进行增量处理，ImportPackage 可以较自然的按包进行增量处理，Sema 以文件或包级别的角度，无法决策上一次哪些分析结果可以复用，则至少需要按 Decl 维度进行分析。

```mermaid
graph LR
    subgraph "增量处理粒度"
        A[Parse<br/>按文件维度] 
        B[ConditionCompile<br/>按文件维度]
        C[MacroExpand<br/>按文件维度]
        D[ImportPackage<br/>按包维度]
        E[Sema<br/>至少按Decl维度<br/>复杂度高，收益低]
    end
    
    A --> B --> D --> C --> E
    
    style E fill:#ffcccc
    style A fill:#ccffcc
    style B fill:#ccffcc
    style C fill:#ccffcc
    style D fill:#ffffcc
```

其中 Sema 以 Decl 进行分析需要更多的Decl级别的差异分析步骤，缓存处理（ast相比编译器增量还需要缓存Body用于复用、诊断的缓存和Decl的绑定，Ty类型的复用等）。该方案复杂程度高，工作量大，易出错，且易受后期Sema特性变更或bugfix的影响，给后续的开发造成额外的工作负担，更可能存在识别不到需要适配或难以适配的场景，导致增量分析的质量难以保证或者特性交付受到影响，额外引入的耗时在一些场景下可能会超出增量减少的耗时，故认为Sema 在lsp场景下的收益不高。

故本方案主要针对 Parse，ConditionCompile，ImportPackage，MacroExpand 阶段的增量进行设计，Sema 仍进行几乎全量的分析（一些解糖和typeManger已分配的类型可以尝试复用）。

### 增量编译流程

以包为编译单元。

lsp：语言服务
ci：编译器实例

1. lsp 记录当前包源码文件和上游包的变更状态。
2. ci 根据变更状态，分析需要重编的文件和导入的包
3. 根据分析结果，对缓存的ast，诊断信息，进行裁剪，去除需要重编的File和导入的包后重新执行Parse->ConditionCompile->ImportPackage->MacroExpand->Sema，将重新编译的文件和导入的上游包重新编译，得到最终的ast，诊断信息（过程中进行缓存，具体缓存方案见下文）。

```mermaid
sequenceDiagram
    participant LSP as LSP语言服务
    participant CI as 编译器实例(CI)
    participant Cache as 缓存系统
    
    Note over LSP: 监控文件变更
    LSP->>LSP: 1. 记录当前包源码文件<br/>和上游包的变更状态
    
    LSP->>CI: 2. 请求增量编译
    CI->>CI: 分析需要重编的文件<br/>和导入的包
    
    CI->>Cache: 3. 裁剪缓存的AST和诊断信息
    Cache-->>CI: 返回裁剪后的缓存
    
    CI->>CI: 执行增量编译流程:<br/>Parse->ConditionCompile-><br/>ImportPackage->MacroExpand->Sema
    
    CI->>Cache: 缓存新的AST和诊断信息
    CI-->>LSP: 返回最终的AST和诊断信息
```

#### lsp 记录当前包源码文件和上游包的变更状态

需要缓存的信息

1.当前包各源码文件的变更状态（ChangeState）；
2.上游包的变更状态（ChangeState）：

enum ChangeState {
    NO_CHANGE, CHANGED, ADD, DELETED, ...
}

```mermaid
stateDiagram-v2
    [*] --> NO_CHANGE: 首次编译时标记为变更
    NO_CHANGE --> CHANGED: 文件内容修改
    NO_CHANGE --> ADD: 新增文件
    NO_CHANGE --> DELETED: 删除文件
    CHANGED --> NO_CHANGE: 编译完成后
    ADD --> NO_CHANGE: 编译完成后
    DELETED --> [*]: 从缓存中移除
```

**注意：**

> 第一次编译时，都要标记成变更状态。

##### 当前包源码的状态和缓存

- 记录文件的相对于上一次分析的变更状态

##### 上游包的状态

###### cjo的状态

- std依赖的cjo缓存在lsp生命周期内不变（发生改变需要重启lsp）
- bin依赖的cjo缓存在lsp生命周期内不变（发生改变需要重启lsp）
- 源码依赖，在上游包发生修改时，cjo也发生改变时需要刷新变更状态。

```mermaid
graph TD
    A[上游包变更状态] --> B{依赖类型}
    B -->|std依赖| C[cjo状态不变<br/>需重启LSP]
    B -->|bin依赖| D[cjo状态不变<br/>需重启LSP]
    B -->|源码依赖| E{cjo是否变更?}
    E -->|是| F[刷新变更状态]
    E -->|否| G[保持状态]
```

###### 源码的状态

- 特别的，对于源码依赖的宏包，一些场景修改宏的实现不会导致cjo变化，但是会导致so发生变化进而会影响展开结果，在正常场景下用户点击构建按钮，lsp会重启，不会影响增量编译结果，但用户若是在命令行中重编，不会触发lsp重启，但可能会刷新宏展开结果，故还需要额外记录宏包的源码变更状态。

```mermaid
graph LR
    A[宏包源码变更] --> B{cjo是否变更?}
    B -->|是| C[刷新变更状态]
    B -->|否| D{so是否变更?}
    D -->|是| E[记录源码变更状态<br/>影响宏展开结果]
    D -->|否| F[保持状态]
    
    style E fill:#ffcccc
```

### 重编范围分析方案

```mermaid
flowchart TD
    Start([开始重编范围分析]) --> A[文件变更检查]
    
    A --> A1{文件变更/删除?}
    A1 -->|是| A2[删除缓存中对应File节点]
    A1 -->|否| A3[检查导入的宏包是否变更]
    A3 -->|是| A2
    A3 -->|否| B[跳过Parse]
    A2 --> B1[重新Parse变更/删除/新增文件]
    B1 --> B2[替换/添加到缓存AST]
    B --> B3[复用缓存中的报错信息]
    B2 --> B3
    
    B3 --> C[条件编译阶段]
    C --> C1{文件变更/删除/新增?}
    C1 -->|是| C2[重新执行条件编译]
    C1 -->|否| C3[跳过条件编译]
    C2 --> D
    C3 --> D
    
    D[包依赖分析] --> D1[分析包的依赖关系]
    D1 --> D2{包是否变更?}
    D2 -->|是| D3[标记为NeedReload]
    D2 -->|否| D4[检查下游包引用]
    D4 --> D5{下游包引用上游包?}
    D5 -->|是| D6[标记为NeedUpdateDueUpChange]
    D5 -->|否| D7[检查上游包子类引用]
    D7 --> D8{上游包有子类引用?}
    D8 -->|是| D9[标记为NeedUpdateDueDownChange]
    D8 -->|否| E
    D3 --> E
    D6 --> E
    D9 --> E
    
    E[包导入流程] --> E1{包需要重新加载?}
    E1 -->|否| E2[复用importedPackages的AST]
    E1 -->|是| E3[重新加载包]
    E2 --> E4[更新TypeManager]
    E3 --> E4
    E4 --> E5[删除需要重编的文件/包中的Ty]
    E5 --> E6[刷新新增/NeedReload/NeedUpdate包的引用]
    
    E6 --> F[宏展开流程]
    F --> F1{文件变更/删除/新增?}
    F1 -->|是| F2[重新执行宏展开]
    F1 -->|否| F3[复用缓存中的报错信息]
    F2 --> G
    F3 --> G
    
    G[Sema阶段] --> G1[擦除复用的File节点中的<br/>Ty, target, CHECK_VISITED等Sema信息]
    G1 --> G2[重新执行Sema流程]
    G2 --> End([结束])
    
    style D3 fill:#ffcccc
    style D6 fill:#ffffcc
    style D9 fill:#ffffcc
    style G1 fill:#ccccff
```

详细步骤说明：

- 若存在文件变更/删除，则在缓存的ast中删除对应的File节点，对于没发生文件变更的，计算每个File节点导入的宏包，若存在导入的宏包发生了变更，当作文件变更处理。重新 Parse 变更/删除/新增的文件，将其替换/添加到缓存的ast中，没有重新解析的File节点将缓存中对应的报错重新报出。
- 对于存在变更/删除/新增的文件重新执行条件编译，其他文件跳过条件编译处理。
- 根据上一次编译记录的包的依赖关系（当前包以及所有依赖包的下游包和上游包信息）和lsp记录的上游包cjo的变更状态，将变更的包记为 NeedReload 其下游包/上游包标记为 NeedUpdateDueUpChange/NeedUpdateDueDownChange（下游包可能存在引用上游包的情况需要刷新，需要重新计算Ty和target, 上游包类型中记录了些直接子类的引用信息）。

```mermaid
graph TD
    subgraph "包依赖关系示例"
        A[当前包<br/>CurrentPkg] --> B[上游包A<br/>UpstreamA]
        A --> C[上游包B<br/>UpstreamB]
        B --> D[上游包C<br/>UpstreamC]
        C --> D
        E[下游包A<br/>DownstreamA] --> A
        F[下游包B<br/>DownstreamB] --> A
    end
    
    subgraph "变更影响分析"
        G[包变更] --> H{包类型}
        H -->|当前包| I[NeedReload]
        H -->|上游包| J[NeedReload<br/>+ NeedUpdateDueUpChange]
        H -->|下游包| K[NeedUpdateDueDownChange]
    end
    
    style I fill:#ffcccc
    style J fill:#ffffcc
    style K fill:#ffffcc
```

- 执行包导入流程. (1) 在处理导包语句时（对于不需要重新加载的包直接复用原来importedPackages的ast）(2) 处理完导包语句，将所有依赖预导入后，对typeManger进行更新，将需要重编的文件,重新导入的包和本次没有导入包中的Ty删除，对新增或者标记NeedReload/NeedUpdateDueUpChange/NeedUpdateDueDownChange，刷新引用。
- 执行宏展开流程，对于存在编/删除/新增的文件重新执行宏展开，没有重新展开的File节点将缓存中对应的报错重新报出。
- 对于复用了File节点的ast 擦除Ty，target，CHECK_VISITED 等Sema信息，重新执行Sema流程（需新增一些重入处理）。

### ast/ty等缓存方案

分析结果的缓存：

每次分析的结果直接缓存在内存中，和CompilerInstance的生命周期一致。

```mermaid
graph TB
    subgraph "CompilerInstance 缓存结构"
        A[pkgs<br/>包AST缓存]
        B[srcPkgs<br/>源码包AST缓存]
        C[diagnosticEngine<br/>诊断信息缓存]
        D[typeManager<br/>类型管理器]
    end
    
    subgraph "typeManager 缓存策略"
        D --> E[allocatedTys<br/>已分配类型]
        E --> F{根据ty->Decl<br/>所在文件/包<br/>选择性保留}
    end
    
    style A fill:#ccffcc
    style B fill:#ccffcc
    style C fill:#ffffcc
    style D fill:#ccccff
```

#### 补全场景

对于补全场景，直接利用上一次的CompilerInstance 中的 pkgs，srcPkgs 以及 diagnosticEngine 中的缓存（需新增 cache）

```mermaid
graph LR
    A[补全请求] --> B{有缓存的<br/>CompilerInstance?}
    B -->|是| C[复用pkgs, srcPkgs<br/>diagnosticEngine缓存]
    B -->|否| D[执行完整编译流程]
    D --> C
    C --> E[返回结果]
    
    style C fill:#ccffcc
```

#### 诊断场景

对于诊断分析类场景，需要缓存Sema前的ast。

- 方案1 对于Sema后的ast进行Sema信息擦除（Ty，target，CHECK_VISITED，解糖？等）
- 方案2 对于Sema前的ast进行clone
（pkgs 和 srcPkgs（是拷贝sema前的ast还是擦除Sema的信息，类型，解糖，CHECK_VISITED等信息）。

```mermaid
graph TD
    A[诊断场景] --> B{选择缓存方案}
    
    B -->|方案1| C[Sema后的AST]
    C --> C1[擦除Sema信息:<br/>Ty, target, CHECK_VISITED, 解糖等]
    C1 --> D[得到Sema前的AST]
    
    B -->|方案2| E[Sema前的AST]
    E --> E1[Clone AST]
    E1 --> D
    
    D --> F[缓存到CompilerInstance]
    
    style C1 fill:#ffcccc
    style E1 fill:#ccffcc
```

#### 相同部分

typeManger（allocatedTys中根据ty->Decl所在文件/包选择性保留）

## 方案实现交付策略

渐进式：

```mermaid
gantt
    title 增量分析方案实施路线图
    dateFormat YYYY-MM
    section 第一阶段
    补全场景Parse增量      :a1, 2024-01, 1M
    补全场景ConditionCompile增量 :a2, after a1, 1M
    补全场景ImportPackage增量   :a3, after a2, 2M
    section 第二阶段
    诊断场景Parse增量      :b1, after a3, 1M
    诊断场景ConditionCompile增量 :b2, after b1, 1M
    诊断场景ImportPackage增量   :b3, after b2, 2M
    诊断场景MacroExpand增量    :b4, after b3, 2M
```

详细实施计划：

1. 先对补全场景获取不带语义 ast 流程中的Parse->ConditionCompile->ImportPackage 增量分析进行支持
2. 后支持诊断场景的 Parse->ConditionCompile->ImportPackage->MacroExpand 阶段的增量
