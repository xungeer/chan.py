# chan2026.dll 编译与使用说明

## 1. 核心构架实现
根据实现方案的要求，`chan2026.dll` 采用纯 C++ 实现，避免了引入外部依赖（例如 Python 解释器）。
本项目已经完成如下部分：
- **`build_tdx_dll.py`**：Python 构建脚本，可直接解析 `main.py` 中的 `config_dict`，并生成 `tdx_plugin/tdx_config.h` 的常量头文件（含原始值宏和派生布尔宏，如 `CFG_BI_ALGO_FX`），然后自动调用 `cl.exe` 工具链编译整个 DLL。
- **`tdx_plugin/chan2026_main.cpp`**：实现了纯 C++ 版本的核心算法，包括笔扫描判断、化简、分型有效性验证、`over_seg` 中枢检测（含延伸逻辑）、中枢合并、线段检测和买卖点提取。
- **`tdx_plugin/TdxPluginHeader.h`**：提供了符合通达信插件标准的 `RegisterTdxFunc` 回调。

## 2. 编译过程
1. 在命令行或 IDE 中修改 `main.py` 中的参数 (`config_dict`)。 
2. 使用系统配置好的 MSVC 2022 工具链（已准备就绪）。
3. 执行如下命令可一键重新编译 DLL 文件：
   ```powershell
   python build_tdx_dll.py
   ```
4. 编译成功后，工作目录或输出目录中生成的 `chan2026.dll` 即为打包好的最新 DLL 文件，且它**内置了你在 python 中设置的参数配置**。

## 3. 在通达信中挂载和使用
1. 将 `chan2026.dll` 复制到通达信的 `T0002\dlls\` 目录，将其命名绑定为第 2 号 DLL (`TDXDLL2`)。
2. 打开通达信的公式管理器，导入/新建 `tdx_plugin\chan2026_formula.txt` 中定义的两个公式：
   - 辅助参考公式 **`FBASE_CHAN`**
   - 主图显示公式 **`缠论画图2026`**
3. 在任意日线图 / 30分钟图通过敲击主图公式名即可调用该 DLL 的 `C++` 计算引擎并渲染出对应周期的走势图景。

## 4. 当前已实现核心功能 (v1.0)
- **底层形态计算**：成功打通 `chan2026_main.cpp`，实现了纯 C++ 环境下的精确分型判断、笔的合并、三笔成段 (SEG)、以及中枢矩阵 (ZS) 和买卖点 (BSP) 提取。
- **over_seg 中枢算法 (zs_algo)**：完整对齐 Python `ZSList.update_overseg_zs` 逻辑。中枢检测跨线段进行，支持 `try_add_to_end` 延伸（新笔在 [ZD,ZG] 范围内时自动扩展中枢跨度），并包含方向检查（首笔必须为逆势笔）。
- **中枢合并 (zs_combine)**：支持 `CFG_ZS_COMBINE` 和 `CFG_ZS_COMBINE_MODE` 两个编译期配置项。当 `zs_combine=True` 时，同一线段内的相邻中枢按 `peak`（peak 区间重叠）或 `zs`（中枢区间重叠）模式自动合并为更大的中枢。合并后买卖点基于合并后中枢重新计算。
- **笔配置参数 (bi_algo/bi_strict/bi_fx_check)**：完整支持三个笔相关配置参数：
  - `bi_algo="fx"`：跳过 Parse2 跨度化简，与 Python `can_make_bi` 中 `satisfy_span=True` 对齐。
  - `bi_strict`：控制 Parse2 跨度阈值（严格 span>=4，宽松 span>=3），仅当 `bi_algo="normal"` 时生效。
  - `bi_fx_check`：通过 `BiValidateFx` 后处理函数实现，支持 LOSS/STRICT/HALF 三种分型有效性检查模式，对齐 Python `KLine.check_fx_valid`。
- **现代化白底公式配色**：`chan2026_formula.txt` 中内置了专为白色背景 (`#FCFCFC`) 优美的 16 进制颜色编码 `COLOR+BBGGRR`。中枢使用了雅致青灰边的极淡底纹框，笔段色彩兼顾高对比与美感，三类买卖点具有明确颜色区分。
- **多级别跨周期引用**：主图公式内置了对 1/5/15/30/60分钟 和日线的原生挂载引用（调用 `Func7` 映射点）。不仅能在当前图表绘制本级别，也可叠加下级别（如5分钟）的笔轨迹。

## 5. 后续开发事项
当前 C++ 已完整对齐 `over_seg` 中枢算法、`zs_combine` 中枢合并逻辑，以及 `bi_algo`/`bi_strict`/`bi_fx_check` 笔配置参数。段的划分为基于笔重叠关系推导的基础有效版本。如果需要根据原生 `chan.py` 对严格线段划分规则 (FX) 或特殊变异中枢进行像素级还原，可继续在 `chan2026_main.cpp` 中核心函数 `CalculateChanElements` 层进行扩展。
框架、图表公式绑定与编译工作流目前已完全打通。
