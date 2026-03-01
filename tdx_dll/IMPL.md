# TDX DLL 开发进展

## Phase 1: MACD 指标计算 ✅ (2026-03-01)

- [x] `CMACD.h`：EMA 递推 + area/peak/half_area/diff 区间指标
- [x] `Main.cpp`：Func1 接收 CLOSE 参数，全局 g_macd 计算
- [x] `Func10`：MACD 调试输出（mode=0/1/2 → MACD/DIF/DEA）
- [x] 验证：200 根 K 线 Python↔DLL 对比通过（误差 < 5e-5）

## Phase 2: 线段识别 ✅ (2026-03-01)

- [x] `SegDetector.h`：特征序列(EigenElement) + 分型(CEigenFX) + 线段端点识别(CSegDetector)，~530行
- [x] `ChanConfig.h`：新增 `LEFT_SEG_METHOD` 配置（peak/all）
- [x] `Main.cpp`：Func1 内调用 `g_segDetector.detect()`，新增 `Func9` 线段标记输出(+2/-2)
- [x] `Makefile`：升级 `-std=c++11`
- [x] 编译验证通过

## Phase 3: 线段内中枢 ✅ (2026-03-01)

- [x] `CZSList.h`：607行，包含完整的 CZS 和 CZSList 实现
  - [x] `CZS`：中枢结构体，含 high/low/peak_high/peak_low/biInIdx/biOutIdx/bi_lst
  - [x] `CZS.isDivergence()`：MACD 背驰判断（对应 Python `ZS.is_divergence`）
  - [x] `CZS.outBiIsPeak()`：出中枢笔极值判断
  - [x] `CZS.combine()`：中枢合并（zs/peak 两种模式）
  - [x] `CZS.endBiBreak()`：出中枢笔突破检测
  - [x] `CZSList.cal_bi_zs()`：按线段分段计算中枢
  - [x] `CZSList.updateZsInSeg()`：设置 bi_in/bi_out/bi_lst
  - [x] `CZSList.getFirstMultiBiZs()` / `getFinalMultiBiZs()` / `getMultiBiZsCnt()`
  - [x] `CZSList.tryConstructZs()` / `tryCombine()`
- [x] `Main.h`：引入 `CZSList.h`
- [x] 验证：已通过 Phase 7 端到端验证覆盖（买卖点匹配间接验证 ZS 正确性）

## Phase 4: 一类买卖点 ✅ (2026-03-01)

- [x] `CBSPointList.h`：~250行，包含 T1（趋势背驰）+ T1P（盘整背驰）完整实现
  - [x] `CBSPoint`：买卖点结构体（biIdx/origIdx/code/is_buy）
  - [x] `CBSPointList.cal()`：核心入口，遍历线段计算买卖点
  - [x] `CBSPointList.treat_bsp1()`：趋势背驰判断（调用 `CZS.isDivergence()`）
  - [x] `CBSPointList.treat_pz_bsp1()`：盘整背驰判断（MACD metric 对比）
  - [x] `CBSPointList.fillOutput()`：映射到通达信 pOut 数组
- [x] `Main.h`：引入 `CBSPointList.h`
- [x] `Main.cpp`：
  - [x] 新增全局 `g_zslist` + `g_bsplist`
  - [x] Func1 内调用 `g_zslist.cal_bi_zs()` + `g_bsplist.cal()`
  - [x] Func5 重写：从 `g_bsplist.fillOutput()` 输出精确编码 (1/11/1.5/11.5)
- [x] 编译验证通过

## Phase 5: 二类买卖点 ✅ (2026-03-01)

- [x] `ChanConfig.h`：新增 `BSP2S_FOLLOW_2` + `MAX_BSP2S_LV` 配置宏
- [x] `CBSPointList.h`：新增 ~200行，完整实现 T2 + T2S
- [x] `CBSPointList.h`：新增 ~200行，T2 + T2S 完整实现
  - [x] `cal_seg_bs2point()`：入口函数
  - [x] `treat_bsp2()`：T2 判断（回撤率 vs `MAX_BS2_RATE`）
  - [x] `treat_bsp2s()`：T2S 判断（类二迭代搜索，含重叠/突破/回撤检查）
  - [x] 辅助函数：`getBiEndVal`/`getBiAmp`/`bsp2sBreakBsp1`/`hasBspOnBi`/`getBiSegIdx`
- [x] 编译验证通过

## Phase 6: 三类买卖点 ✅ (2026-03-01)

- [x] `CZSList.h`：新增 `getMultiBiZsLst()` 方法
- [x] `CBSPointList.h`：新增 ~230行，T3A + T3B 完整实现
  - [x] `cal_seg_bs3point()`：入口函数
  - [x] `treat_bsp3_after()`：T3A（中枢在一类买卖点之后，遍历 `next_seg` 多笔中枢）
  - [x] `treat_bsp3_before()`：T3B（中枢在一类买卖点之前，从 `bsp1+2` 同向笔搜索）
  - [x] 辅助函数：`bsp3Back2Zs`/`bsp3BreakZsPeak`/`calBsp3BiEndIdx`
  - [x] 配置参数：`STRICT_BSP3`/`BSP3_PEAK`/`BSP3A_MAX_ZS_CNT`/`BSP3_FOLLOW_1`
- [x] 编译验证通过
- [x] 验证：已通过 Phase 7 端到端验证（买卖点匹配=809，仅DLL=0）

## Phase 7: 集成验证 ✅ (2026-03-01)

> Phase 1-6 已全部完成（✅），端到端验证通过

- [x] Func5 已整合 笔→线段→中枢→MACD→买卖点 全链路
- [x] DLL 命名：`chan2026-full.dll`，绑定为通达信 3 号 DLL（TDXDLL3）
- [x] 通达信公式 `缠论分析.tne`：笔(蓝)/线段(暗金)/中枢(琥珀橙)/六类买卖点
- [x] 新增验证脚本 `test_dll_accuracy.py`（基于 TdxLocalAPI + 000300 日线）
- [x] BSP 过滤逻辑修复
  - [x] `addBSPoint` 多类型支持（同笔可有 T2+T3B 等组合）
  - [x] `fillOutput` 同位置取码值最小的，`fillOutputAll` 输出全部
  - [x] `Func11` 调试输出（完整BSP列表）
  - [x] ZS 算法对齐：normal/over_seg/auto 三种模式实现
- [x] **实测验证通过** (`python test_dll_accuracy.py`, 2026-03-01 18:17)
  - 测试数据：000300 日线，5135 根 K线
  - ✅ 笔端点：匹配=1838，仅Python=1（K[5134]）
  - ✅ 线段端点：匹配=258，仅Python=1（K[5132]）
  - ✅ 买卖点：匹配=809，仅Python=1（K[5134] T2），仅DLL=0
  - 差异均在数据末尾（K[5132-5134]），属于尾部未完成笔/线段的边界差异

## Phase 8: 中枢可视化一致化 ✅ (2026-03-01)

- [x] `Main.cpp`：Func2/3/4 数据源从 `CCentroid`（流式笔中枢）切换为 `g_zslist`（CZSList，线段内中枢）
  - [x] Func2：输出 `zs.high`（中枢上沿）
  - [x] Func3：输出 `zs.low`（中枢下沿）
  - [x] Func4：输出中枢起止信号（1/2）
  - [x] 跳过 `isOneBiZs()` 单笔中枢
  - [x] K线区间：`biPoints[beginBiIdx].origIdx` ~ `biPoints[endBiIdx+1].origIdx`
- [x] `CZSList.h`：更新注释，标注 CCentroid 不再被 Func2/3/4 使用
- [x] `SPEC.md`：更新 Func2/3/4 说明，标注"线段内"和"基于CZSList"
- [x] 验证：端到端测试通过（买卖点匹配=809，与改动前一致）

## 编译环境

```
g++  : C:\Users\luyu4\anaconda3\Library\mingw-w64\bin\g++.exe
make : C:\Users\luyu4\anaconda3\Library\mingw-w64\bin\mingw32-make.exe
命令 : mingw32-make -f Makefile (在 tdx_dll 目录)
DLL  : 64-bit (AMD64)
```

