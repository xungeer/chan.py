# 通达信缠论 DLL (chan2026.dll) 开发与复现知识库

## 1. 项目概述与设计原则
该项目旨在将纯 Python 实现的缠论分析系统（`chan.py`）其输出的底层形态学结果提供给通达信(TDX)使用。
为达成此功能且不破坏原项目的清晰结构，制定了以下**核心设计原则（Zero-Intrusion）**：
- 所有针对通达信的 C++ 算法挂载代码、相关头文件、一键构建脚本、生成产品（DLL本身及头文件），均**完全封装于 `tdx_plugin` 目录下**。
- `chan.py` 的原有核心项目代码（如 `Bi`, `Seg`, `Main.py` 等）坚决不允许修改与破坏，确保 Python 系统和外部调用的独立性。

## 2. 核心代码与目录结构 
整个项目结构遵循将外挂功能完全隔离的原则：
```text
c:\Users\luyu4\chan.py\
 ├── main.py (包含核心参数 config_dict，不对通达信做任何修改)
 ├── tdx_plugin/ (一切通达信挂载相关的项目逻辑都必须在此处)
 │    ├── build_tdx_dll.py      —— 自动化构建脚本，也是本插件的核心入口工具。
 │    ├── tdx_config.h          —— 构建脚本根据 main.py 提取出的 C++ 预编译宏头文件（动态生成）。
 │    ├── TdxPluginHeader.h     —— 通达信插件开发标准的 8 个 Func 定义头。
 │    ├── chan2026_main.cpp     —— 用独立 C++ 实现的具体算法骨干与底层处理逻辑。
 │    ├── chan2026_formula.txt  —— 需导入至通达信使用的画线图表公式及跨周期引用的文本。
 │    ├── chan2026.dll          —— 最终编译完成的纯正输出物（32位dll）。
 │    ├── readme.md             —— 给用户的简单使用说明。
 │    └── reference/            —— 存放旧版/开源 C++ 参考代码（如 Main_cpp.txt, FxIndicator_h.txt），供查阅之用。
```

**目录卫生与整洁要求**：
- **编译产物隔离**：所有的编译临时中间文件（如 `.obj`, `.exp`, `.lib`）以及最终定型的产物 `chan2026.dll`，**只能**出现在 `/tdx_plugin` 目录下。
- **根目录纯净**：严禁在 `chan.py` 的根目录下遗留或堆砌上述 C++ 相关的构建垃圾以及冗余参照脚本文件，一旦发现应当立刻归档甚至自动清理，务必确保 Python 主工程整洁。

## 3. 核心接口与通信
这套 C++ 内核主要实现对底层 K 线序列的横向扫描计算（详见底层 `CalculateChanElements` 通用分析函数）。它向上导出 `RegisterTdxFunc` 回调供通达信识别。同时内部遵循以下通达信标准的 8 个挂载接口设计（Func 号数映射）：
- **Func1**：笔标记扫描与合并过滤算法（确保 `1` 和 `-1` 在 `DRAWLINE` 函数中能连续成对出现）。支持以下编译期配置：
  - `CFG_BI_ALGO_FX`：当 `bi_algo="fx"` 时定义，**跳过 Parse2 跨度化简**（不检查笔跨度），与 Python `can_make_bi` 中 `satisfy_span = True` 对齐。
  - `CFG_BI_STRICT`：当 `bi_algo="normal"` 时，控制 Parse2 的跨度阈值（`1` -> span>=4 严格模式，`0` -> span>=3 宽松模式）。
  - `CFG_BI_FX_CHECK_LOSS` / `CFG_BI_FX_CHECK_STRICT` / `CFG_BI_FX_CHECK_HALF`：分型有效性检查模式，通过 `BiValidateFx` 后处理函数实现，对齐 Python `KLine.check_fx_valid`。
- **Func2 / Func3**：中枢的高点 `ZS_H` 与低点 `ZS_L`（即提取出的 `[ZD, ZG]` 区间）。若启用 `CFG_ZS_COMBINE`，输出的是合并后中枢的区间。
- **Func4**：中枢结构（开局/结尾）定点信号提取 `ZS_FLAG`（通过跳跃迭代距离避免两个本级中枢端点标记被意外覆盖）。合并后中枢的起止标记会被更新。
- **Func5 / Func6**：对应三类买卖点标记提取（如1买为 `1`，1卖为 `11`）以及线段的高低点标记分析。买卖点基于合并后中枢重新计算。
- **Func7 / Func8**：跨周期的降维映射还原，配合 `FBASE_CHAN.BI#MIN_xx` 处理次级别的跨级笔呈现。

### 3.1 over_seg 中枢检测算法 (zs_algo)
`CalculateChanElements` 内部实现了完整的 `over_seg` 中枢算法（对齐 Python `ZSList.update_overseg_zs`），逐笔处理，使用以下核心逻辑：
- **`try_add_to_end` 延伸**：当已有中枢且空闲列表为空时，检查当前笔及下一笔是否仍在 [ZD, ZG] 范围内（`zs_overlap`）。若是，延伸中枢（更新 `end_idx/end_pt/peak` 值，**ZD/ZG 不变**），使中枢可跨越初始3笔。
- **in-range 跳过**：当前笔在中枢范围内但无法延伸时，跳过该笔不加入空闲列表。
- **方向检查**：构建中枢时，首笔方向必须与所属线段方向相反（逆势笔），否则跳过。通过 `seg_dirs[]` 和 `get_parent_seg_dir()` 实现。
- **3笔重叠构建**：空闲列表积累到3笔后，取最后3笔检查 `min(high) > max(low)`，满足且 `begin_bi.idx > 0` 则形成中枢。
- 使用 `bi_high_fn(k)` / `bi_low_fn(k)` 辅助函数获取笔 k（`points[k]→points[k+1]`）的价格区间。

### 3.2 中枢合并机制 (zs_combine)
`struct ZSInfo` 存储每个中枢的完整信息（`start_idx`, `end_idx`, `zg`, `zd`, `peak_high`, `peak_low`, `seg_idx`）。中枢检测完成后，根据编译宏执行合并：
- **`CFG_ZS_COMBINE = 1`**：启用合并（对应 `main.py` 中 `zs_combine: True`）。
- **`CFG_ZS_COMBINE_MODE = "peak"`**：按 `peak_high/peak_low` 区间重叠判断是否合并（严格大于）。
- **`CFG_ZS_COMBINE_MODE = "zs"`**：按 `zg/zd` 中枢区间重叠判断（含等于）。
- **同段约束**：`seg_idx` 不同的中枢不可合并（对齐 Python `begin_bi.seg_idx != zs2.begin_bi.seg_idx`）。
- 合并操作：扩展 `zd = min`, `zg = max`, `peak_low = min`, `peak_high = max`，`end` 取后者。反复迭代直至无法继续合并。
- 合并后从 `vector<ZSInfo>` 重新生成 `zs_h`, `zs_l`, `zs_flag`, `bsp` 四个输出通道。

**重要公式陷阱与经验法则**：
- **中枢闭合问题**：不能仅仅用一句 `STICKLINE` 绘制，这只会得到两根竖线。必须由两条 `DRAWLINE(ZS_FLAG=1, H, ZS_FLAG=2, H)` 画横向上下表皮，并与 `STICKLINE` 的左右垂直皮结合，才能形成真正长方形全闭包体。
- **背景与色彩兼容**：通达信自带基础色在自定义白底（`#FCFCFC`）上极度刺眼或不可见。本项目的 `chan2026_formula.txt` 全面上调至 16进制格式调用法 `COLORBBGGRR` （如朱红 `COLOR3C4CE7`）以保证美观和无缝嵌合底图。

## 4. 具体绘图元素说明 (Visual Elements)
在 `chan2026_formula.txt` 中，通过调用 `chan2026.dll` 实现了以下五类关键绘图元素，专门针对自定义白底（`#FCFCFC`）做了色彩抗锯齿与防眩晕的高级适配：

### 4.1 本级别笔 (Bi)
- **获取方式**：`Func 1` (`TDXDLL2(1, H, L, 0)`) 进行顶底分型跳跃扫描。
- **视觉展现**：细实线 (`LINETHICK1`) 连接顶底。
- **色彩设计**：浅灰色 (`COLORA0A0A0`)，作为最底层的骨架脉络，不抢占视觉焦点。

### 4.2 本级别线段 (Segment)
- **获取方式**：`Func 6` (`TDXDLL2(6, BI, H, L)`)，基于笔合并的结果推导。
- **视觉展现**：加粗实线 (`LINETHICK2`) 连接段高低点。
- **色彩设计**：朱色 (`COLOR3C4CE7`)（注：通达信取色为 BGR格式），使得线段级别的走势在图面上清晰锐利。

### 4.3 本级别中枢 (Zhongshu)
- **获取方式**：`Func 2/3/4` 分别获取中枢高低点及进出端点标记。
- **视觉展现**：闭合矩形框。使用 `DRAWLINE` 绘制水平上下沿，`STICKLINE` 绘制垂直左右边界，合并闭环。
- **色彩设计**：雅致青灰色 (`COLORC19C72`)，作为极其优美的底纹框标示震荡区间。

### 4.4 跨级别递归笔 (Sub-level Bi Mapping)
- **获取方式**：借助辅助公式 `FBASE_CHAN` 及跨周期引用（例如 `#MIN5`），经 `Func 7` 降维对齐至当前图表。
- **视觉展现**：虚线 (`DOTLINE`)。
- **色彩设计**：橙黄色 (`COLORDB9834`)，用于在大级别图表中叠加观察次级别走势的内部结构。

### 4.5 三类买卖点标记 (BSP Signals)
- **获取方式**：`Func 5` 提纯底层买卖点信息（如1买为1，1卖为11）。
- **视觉展现**：在对应极值K线下方或上方输出文字 `DRAWTEXT`。
- **色彩设计**：买点采用冷色系暗红/亮蓝调（1买: `COLOR2B39C0`, 2买: `COLORAD448E`, 3买: `COLOR0054D3`），卖点采用偏暖绿/大地色系（1卖: `COLOR60AE27`, 2卖: `COLORB98029`, 3卖: `COLOR503E2C`）。

## 5. 核心工作流解析与复现指南 (Agent)
如果要由后续的 Agent 基于此过程增加功能或重新生成 dll，请严格遵守以下核心流程：

### 第一步：参数注入（Python 到 C++ 的降维映射）
Agent 首先要执行 `cd tdx_plugin && python build_tdx_dll.py`。
脚本机制：它会退回上一级读取 `../main.py` 的源码 `config_dict`，利用正则清洗提纯配置字典中的所有配置项，并在 `tdx_plugin` 文件夹生成对应的 C++ 头文件（`tdx_config.h`），以宏的形式（`#define CFG_xx`）发送设置，保证两端的“单一事实来源（Single Source of Truth）”。
此外，脚本还会根据配置值自动生成**派生布尔宏**（如 `CFG_BI_ALGO_FX`、`CFG_BI_FX_CHECK_LOSS`），方便 C++ 中使用 `#ifdef` 进行编译期分支选择，避免运行时字符串比较。

### 第二步：识别构建环境（强制要求32位 MSVC x86 编译）
**通达信架构限制：必须加载经典的 32位（x86）DLL。**
在自动构建脚本中，严格依赖系统中的 `vcvars32.bat` 进行环境预初始化，之后通过 `cl.exe /LD /EHsc /O2 /I. /Fe:chan2026.dll "chan2026_main.cpp" user32.lib` 执行编译。
**警告：严禁调用 `vcvars64.bat` 或使用64位指令集**，如果错用为 64位 会导致 DLL 生成后被宿主拒绝引发崩溃闪退。

### 第三步：单元测试与部署验证
若 Agent 对框架作出改动，编译后须执行以下校验命令（单元测试步骤）：
1. **导出函数单元检测**：
   在 Developer CMD 中：`dumpbin /EXPORTS chan2026.dll`。必须明确看到名称为 `RegisterTdxFunc` （即通达信要求的基础导出函数名）而且不包含 C++ 的符号乱码。
2. **x86 防闪退靶向检测**：
   在 Developer CMD 中：`dumpbin /HEADERS chan2026.dll | findstr machine`。若输出包含 `14C machine (x86)` 和 `32 bit word machine` 则代表其能被 Tdx 正常调用执行。

## 6. 拓展接力点
如果希望之后对算法进行演进优化：
1. 请勿修改原 `chan.py` 内部任何文件。
2. 直接编辑 `tdx_plugin/chan2026_main.cpp` 中对应的 `Func` 实现体。`over_seg` 中枢算法和 `zs_combine` 合并已完成实现，可进一步参照原始 Python 内 `Seg/Bi` 对特征序列复杂的边界化处理来细化 C++ 版骨架的健壮度。
3. 改完后在 `tdx_plugin` 目录运行 `python build_tdx_dll.py` 触发生成和测试流即可。
4. 新增配置参数时，只需在 `main.py` 的 `config_dict` 中添加，`build_tdx_dll.py` 会自动生成对应的 `CFG_xxx` 宏到 `tdx_config.h`，然后在 C++ 中通过 `#if CFG_xxx` 或 `#ifdef CFG_xxx` 引用即可。对于需要编译期分支的字符串类参数（如 `bi_algo`、`bi_fx_check`），应在 `build_tdx_dll.py` 的 `generate_header` 中追加派生布尔宏（如 `CFG_BI_ALGO_FX`），避免在 C++ 中做运行时字符串比较。

## 7. 核心算法修复与填坑记录 (Troubleshooting & Core Fixes)
在项目的迭代中，解决了一些通达信特有机制导致的绘图异常问题，后续开发必须充分注意这些“坑点”：

### 7.1 跨周期引用次级别信号丢失（散点/虚线错乱）问题
- **现象**：当在主图（如30分钟）使用 `SUB_BI_5M:="FBASE_CHAN.BI#MIN5";` 直接引用5分钟笔的脉冲信号（1或-1）时，画出的虚线结构连线会错乱并发生随机跳点跨越。
- **根本原因**：通达信的跨周期引用机制（如 `#MIN5`），在对齐大周期时，**仅提取并返回该大周期时间跨度内最后一根小周期K线的数值**。如果次级别（5分钟）的顶底部信号没有恰巧落在该大周期的最后一根K线上，主周期拿到的值就是 0，导致有价值的极值信号点被降维打击彻底丢弃。
- **解决规范**：
  1. 在辅助公式 `FBASE_CHAN` 中，不要只抛出脉冲，必须通过 `LAST_BI:= BARSLAST(BI<>0);` 和 `BI_VAL: REF(BI, LAST_BI);`，将其转换为**持续性状态值 (`BI_VAL`)** 向外广播。这样无论上层周期在时间轴何处抽样，取到的都是被保持住的最新走势方向（持续的 1 或 -1）。
  2. 在 C++ 的跨周期解码器（`Func7`）中，接收该连续状态流 `BI_VAL`。通过检测状态翻转（`current_state != last_state`），仅在前沿跳变时恢复出本周期的单点脉冲。
  3. 最后在主图公式里用 `DRAWLINE` 连结这些脉冲，实现完美的跨级别波动折线对齐。

### 7.2 线段划分(`SEG`)跨度过短且未能包裹真正极值点的问题
- **现象**：早期的段落粗略划分简单通过 `i += 3` 步进法强行连线。这使得绘出的线段违背了线段高低点反转原则，且常常游离未能包裹在局部走势的最高点、最低点上。
- **根本原因**：线段在被反向有效跌破/突破前，处于**不断延伸**的状态。期间创出的新高/新低，才是此段真正的顶和底；且只有在反向结构发生时（包含至少3笔的回撤/反抽），才能确认一段的终结和新一段的开始。
- **解决规范**：在 `CalculateChanElements` (C++) 的段划分逻辑中，逐个梳理有效的笔极值点（`1.0f` 或 `-1.0f`）：
  - 如果与当前线段方向**相反**：校验相对距离 `k - last_seg_k >= 3`（必须要求反向间隔含3笔以上），方可允许新线段诞生，记录新极值。
  - 如果与当前线段方向**相同**：这代表原线段在延续拓展。此时比较极值强度（例如针对向上的段落：判断 `pHigh[idx] > pHigh[last_seg_k]`）。若创新高，必须主动**擦除旧有的顶点标记，将段端点挪移更新至当前的真极值点处**。

### 7.3 笔配置参数 (bi_algo/bi_strict/bi_fx_check) 未生效问题
- **现象**：`tdx_config.h` 中正确生成了 `CFG_BI_ALGO "fx"`、`CFG_BI_STRICT 1`、`CFG_BI_FX_CHECK "loss"` 等宏，但 DLL 的笔行为与 Python 不一致。
- **根本原因**：C++ 字符串宏无法直接用于 `#if` 编译期分支。早期代码仅定义了原始值宏，未生成可用于 `#ifdef` 的布尔派生宏，导致算法代码无法读取配置。
- **解决规范**：
  1. 在 `build_tdx_dll.py` 的 `generate_header` 中，根据配置值追加派生布尔宏（如 `bi_algo=="fx"` -> `#define CFG_BI_ALGO_FX 1`）。
  2. C++ 中使用 `#ifdef CFG_BI_ALGO_FX` / `#ifndef CFG_BI_ALGO_FX` 进行分支。
  3. `Func1` 调用链：`Parse1` -> (非fx模式) `Parse2(threshold)` -> `BiValidateFx`。
  4. `BiValidateFx` 根据 `CFG_BI_FX_CHECK_LOSS` / `STRICT` / `HALF` 执行对应的分型有效性后处理，逐对验证相邻顶底端点，移除不满足条件的无效端点。
