# TDX DLL 功能规格

## 精确三类买卖点 (P0)

在 `tdx_dll` 中实现与 Python `chan.py` 一致的三类买卖点信号（1/1P/2/2S/3a/3b）。

### 数据流

```
K线数据(H/L/C) → K线合并 → 笔识别 → MACD计算
                                      ↓
                              线段识别(特征序列分型)
                                      ↓
                              线段内中枢(按seg分段)
                                      ↓
                    一类BSP(背驰) → 二类BSP(回撤) → 三类BSP(中枢突破)
```

### 导出函数

| Func | 输入 | 输出 | 状态 |
|---|---|---|---|
| Func1 | HIGH, LOW, CLOSE | 笔标记 (+1/-1) | ✅ 已实现 |
| Func2 | 笔标记, HIGH, LOW | 中枢高边界 | ✅ 已实现 |
| Func3 | 笔标记, HIGH, LOW | 中枢低边界 | ✅ 已实现 |
| Func4 | 笔标记, HIGH, LOW | 中枢起止信号 | ✅ 已实现 |
| Func5 | 笔标记, HIGH, LOW | 精确买卖点信号 | ✅ T1/T1P/T2/T2S/T3A/T3B |
| Func6 | 笔标记, HIGH, LOW | 形态买卖点 | ✅ 已实现 |
| Func7 | 笔标记, HIGH, LOW | 笔强度 | ✅ 已实现 |
| Func8 | 笔标记, HIGH, LOW | 笔斜率 | ✅ 已实现 |
| Func9 | 笔标记, HIGH, LOW | 线段标记 (+2/-2) | ✅ 已实现 |
| Func10 | 笔标记, -, mode | MACD/DIF/DEA | ✅ 调试用 |
| Func11 | 笔标记, HIGH, LOW | BSP完整列表 | ✅ 调试用(含多类型) |

### Func5 买卖点输出编码（目标）

| 编码 | 含义 | 对应 Python |
|---|---|---|
| 1 / 11 | 一类买点 / 一类卖点 | BSP_TYPE.T1 |
| 1.5 / 11.5 | 盘整背驰买点 / 卖点 | BSP_TYPE.T1P |
| 2 / 12 | 二类买点 / 二类卖点 | BSP_TYPE.T2 |
| 2.5 / 12.5 | 类二买点 / 类二卖点 | BSP_TYPE.T2S |
| 3 / 13 | 三类a买点 / 三类a卖点 | BSP_TYPE.T3A |
| 3.5 / 13.5 | 三类b买点 / 三类b卖点 | BSP_TYPE.T3B |

### 内部模块

| 模块 | 文件 | 代码量 | 对应 Python | 状态 |
|---|---|---|---|---|
| K线合并 | `KLineCombiner.h` | 283行 | `Combiner/KLine_Combiner.py` | ✅ |
| 笔识别 | `BiDetector.h` | 252行 | `Bi/BiList.py` | ✅ |
| MACD | `CMACD.h` | ~160行 | `Math/MACD.py` + `Bi.cal_macd_metric` | ✅ |
| 线段识别 | `SegDetector.h` | ~1000行 | `Seg/SegListChan.py` + `EigenFX.py` | ✅ |
| 线段内中枢 | `CZSList.h` | 607行 | `ZS/ZS.py` + `ZS/ZSList.py` | ✅ |
| 流式中枢 | `CCentroid.h/cpp` | 340行 | `ZS/ZS.py` + `ZS/ZSList.py` (流式实现, Func2-4用) | ✅ |
| 买卖点 | `CBSPointList.h` | 788行 | `BuySellPoint/BSPointList.py` | ✅ T1/T1P/T2/T2S/T3A/T3B |
| 配置 | `ChanConfig.h` | ~200行 | `ChanConfig.py` | ✅ |
| DLL接口 | `FxIndicator.h` | 35行 | — | ✅ |

### 编译环境

```
g++.exe   : C:\Users\luyu4\anaconda3\Library\mingw-w64\bin\g++.exe
make      : C:\Users\luyu4\anaconda3\Library\mingw-w64\bin\mingw32-make.exe
编译命令  : mingw32-make -f Makefile
标准      : C++11 (-std=c++11)
```

### 通达信公式调用示例

```
{缠论笔标记}
BI:=TDXDLL3(1, HIGH, LOW, CLOSE);
{线段标记}
SEG:=TDXDLL3(9, BI, HIGH, LOW);
{买卖点}
BSP:=TDXDLL3(5, BI, HIGH, LOW);
{中枢高低}
ZS_H:=TDXDLL3(2, BI, HIGH, LOW);
ZS_L:=TDXDLL3(3, BI, HIGH, LOW);
{MACD调试}
MACD_VAL:=TDXDLL3(10, BI, 0, 0);
```
