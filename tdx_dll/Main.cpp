/*****************************************************************************
 * chan2026 - 通达信DLL接口主模块
 * 算法完全基于 chan.py 核心计算逻辑
 *
 * 导出函数说明：
 *   Func1: 笔顶底标记信号 (+1=顶, -1=底)
 *   Func2: 中枢高点数据
 *   Func3: 中枢低点数据
 *   Func4: 中枢起止信号
 *   Func5: 精确三类买卖点信号 (T1/T1P/T2/T2S/T3A/T3B)
 *   Func6: 形态买卖点信号
 *   Func7: 笔强度分析
 *   Func8: 笔斜率分析
 *****************************************************************************/

#include "Main.h"

//=============================================================================
// 核心算法：K线合并 + 笔识别
// 移植自 chan.py: KLine_Combiner.py + BiList.py
//=============================================================================

// 全局合并器、笔检测器、MACD计算器和线段检测器（每次 Func1 调用时重新计算）
static CKLineCombiner g_combiner;
static CBiDetector    g_detector;
static CMACD          g_macd;
static CSegDetector   g_segDetector;
static CZSList        g_zslist;
static CBSPointList   g_bsplist;

// Func1 计算笔标记，结果存入 pOut 数组
// pOut[i] = +1 表示第i根K线是笔的顶点
// pOut[i] = -1 表示第i根K线是笔的底点
// pOut[i] = 0  表示非笔端点
//
// 参数：
//   nCount: K线数量
//   pOut:   输出数组
//   pHigh:  最高价数组
//   pLow:   最低价数组
//   pClose: 收盘价数组（用于 MACD 计算）
//   通达信公式: "chan2026.dll"(1, HIGH, LOW, CLOSE)
void Func1(int nCount, float *pOut, float *pHigh, float *pLow, float *pClose)
{
  // 初始化输出
  for (int i = 0; i < nCount; i++)
  {
    pOut[i] = 0;
  }

  if (nCount < 5) return;

  // 步骤1: K线合并（参照 KLine_Combiner.py）
  g_combiner.process(nCount, pHigh, pLow);

  // 步骤2: 笔识别（参照 BiList.py）
  g_detector.detect(g_combiner, pHigh, pLow, nCount);

  // 步骤3: MACD 计算（参照 Math/MACD.py）
  g_macd.clear();
  if (pClose != NULL) {
    for (int i = 0; i < nCount; i++) {
      g_macd.add(pClose[i]);
    }
  }

  // 步骤4: 线段识别（参照 Seg/SegListChan.py）
  g_segDetector.detect(g_detector.biPoints, g_combiner);

  // 步骤5: 线段内中枢计算（参照 ZS/ZSList.py）
  g_zslist.cal_bi_zs(g_detector.biPoints, g_segDetector.segPoints, g_combiner);

  // 步骤6: 精确买卖点计算（参照 BuySellPoint/BSPointList.py）
  g_bsplist.cal(g_detector.biPoints, g_segDetector.segPoints, g_combiner, g_macd, g_zslist);

  // 步骤7: 将笔端点标记映射回原始K线序列
  for (int i = 0; i < (int)g_detector.biPoints.size(); i++)
  {
    const BiPoint& bp = g_detector.biPoints[i];
    if (bp.origIdx >= 0 && bp.origIdx < nCount)
    {
      pOut[bp.origIdx] = (float)bp.dir;
    }
  }
}

//=============================================================================
// 输出函数2号：中枢高点数据
// 移植自 chan.py: ZS/ZSList.py cal_bi_zs
//=============================================================================

void Func2(int nCount, float *pOut, float *pIn, float *pHigh, float *pLow)
{
  CCentroid Centroid;

  for (int i = 0; i < nCount; i++)
  {
    if (pIn[i] == 1)
    {
      // 遇到笔顶点，推入中枢算法
      if (Centroid.PushHigh(i, pHigh[i]))
      {
        // 中枢终结，在区段内填充中枢高边界
        for (int j = Centroid.nStart; j <= Centroid.nEnd; j++)
        {
          pOut[j] = Centroid.fPHigh;
        }
      }
    }
    else if (pIn[i] == -1)
    {
      // 遇到笔底点，推入中枢算法
      if (Centroid.PushLow(i, pLow[i]))
      {
        for (int j = Centroid.nStart; j <= Centroid.nEnd; j++)
        {
          pOut[j] = Centroid.fPHigh;
        }
      }
    }

    // 尾部未完成中枢处理
    if (Centroid.bValid && (Centroid.nLines >= 2) && (i == nCount - 1))
    {
      for (int j = Centroid.nStart; j < nCount; j++)
      {
        pOut[j] = Centroid.fHigh;
      }
    }
  }
}

//=============================================================================
// 输出函数3号：中枢低点数据
//=============================================================================

void Func3(int nCount, float *pOut, float *pIn, float *pHigh, float *pLow)
{
  CCentroid Centroid;

  for (int i = 0; i < nCount; i++)
  {
    if (pIn[i] == 1)
    {
      if (Centroid.PushHigh(i, pHigh[i]))
      {
        for (int j = Centroid.nStart; j <= Centroid.nEnd; j++)
        {
          pOut[j] = Centroid.fPLow;
        }
      }
    }
    else if (pIn[i] == -1)
    {
      if (Centroid.PushLow(i, pLow[i]))
      {
        for (int j = Centroid.nStart; j <= Centroid.nEnd; j++)
        {
          pOut[j] = Centroid.fPLow;
        }
      }
    }

    // 尾部未完成中枢处理
    if (Centroid.bValid && (Centroid.nLines >= 2) && (i == nCount - 1))
    {
      for (int j = Centroid.nStart; j < nCount; j++)
      {
        pOut[j] = Centroid.fLow;
      }
    }
  }
}

//=============================================================================
// 输出函数4号：中枢起点、终点信号
//=============================================================================

void Func4(int nCount, float *pOut, float *pIn, float *pHigh, float *pLow)
{
  CCentroid Centroid;

  for (int i = 0; i < nCount; i++)
  {
    if (pIn[i] == 1)
    {
      if (Centroid.PushHigh(i, pHigh[i]))
      {
        pOut[Centroid.nStart] = 1;
        pOut[Centroid.nEnd]   = 2;
      }
    }
    else if (pIn[i] == -1)
    {
      if (Centroid.PushLow(i, pLow[i]))
      {
        pOut[Centroid.nStart] = 1;
        pOut[Centroid.nEnd]   = 2;
      }
    }

    // 尾部未完成中枢处理
    if (Centroid.bValid && (Centroid.nLines >= 2) && (i == nCount - 1))
    {
      pOut[Centroid.nStart] = 1;
      pOut[nCount-1]        = 2;
    }
  }
}

//=============================================================================
// 输出函数5号：精确三类买卖点信号（每类含2种子类型，共T1/T1P/T2/T2S/T3A/T3B）
// 移植自 chan.py: BuySellPoint/BSPointList.py
//
// 内部数据流: 笔→线段→中枢→MACD→买卖点（在 Func1 中已完成计算）
//
// 输出编码：
//   1    = 一类买点 (趋势背驰)
//   1.5  = 一类P买点 (盘整背驰)
//   2    = 二类买点
//   2.5  = 类二买点
//   3    = 三类a买点 (中枢在一类后)
//   3.5  = 三类b买点 (中枢在一类前)
//   11   = 一类卖点
//   11.5 = 一类P卖点
//   12   = 二类卖点
//   12.5 = 类二卖点
//   13   = 三类a卖点
//   13.5 = 三类b卖点
//
// 需要先调用 Func1 计算笔（Func1 内部同时计算线段+中枢+买卖点），
// 通达信公式: BSP:="chan2026.dll"(5, BISIGNAL, HIGH, LOW);
//=============================================================================

void Func5(int nCount, float *pOut, float *pIn, float *pHigh, float *pLow)
{
  (void)pIn;
  (void)pHigh;
  (void)pLow;

  g_bsplist.fillOutput(nCount, pOut);
}

//=============================================================================
// 输出函数6号：形态买卖点信号
// 基于 chan.py 笔/中枢的形态学分析
// 通过连续笔的高低点趋势和力度递减来判断趋势背驰
//=============================================================================

void Func6(int nCount, float *pOut, float *pIn, float *pHigh, float *pLow)
{
  float fTop1 = 0, fTop2 = 0, fTop3 = 0, fTop4 = 0;
  float fBot1 = 0, fBot2 = 0, fBot3 = 0, fBot4 = 0;

  for (int i = 0; i < nCount; i++)
  {
    if (pIn[i] == 1)
    {
      fTop4 = fTop3;
      fTop3 = fTop2;
      fTop2 = fTop1;
      fTop1 = pHigh[i];

      // 趋势力度递减判断（参照 chan.py 背驰理论）
      if (fTop2 > 0 && fTop3 > 0 && fTop4 > 0 &&
          ((fBot1 - fTop2)/fTop2 > (fBot2 - fTop3)/fTop3) &&
          ((fBot2 - fTop3)/fTop3 > (fBot3 - fTop4)/fTop4))
      {
        // 形态1：连续下降趋势
        if ((fBot1 < fBot2) && (fTop2 < fTop3) &&
            (fBot2 < fBot3) && (fTop3 < fTop4))
        {
          pOut[i] = 1;
          continue;
        }
        // 形态2：趋势背驰型
        if ((fBot1 > fBot2) && (fTop2 > fTop3) && (fBot2 < fBot3) &&
            (fTop3 < fTop4) && (fBot1 < fTop3))
        {
          pOut[i] = 2;
          continue;
        }
        // 形态3：中枢扩展型
        if ((fBot1 > fTop3) && (fBot2 > fBot3) && (fTop3 > fTop4))
        {
          pOut[i] = 3;
          continue;
        }
      }
    }
    else if (pIn[i] == -1)
    {
      fBot4 = fBot3;
      fBot3 = fBot2;
      fBot2 = fBot1;
      fBot1 = pLow[i];

      if (fTop1 > 0 && fTop2 > 0 && fTop3 > 0 &&
          ((fBot1 - fTop1)/fTop1 > (fBot2 - fTop2)/fTop2) &&
          ((fBot2 - fTop2)/fTop2 > (fBot3 - fTop3)/fTop3))
      {
        if ((fBot1 < fBot2) && (fTop1 < fTop2) &&
            (fBot2 < fBot3) && (fTop2 < fTop3))
        {
          pOut[i] = 1;
          continue;
        }
        if ((fBot1 > fBot2) && (fTop1 > fTop2) && (fBot2 < fBot3) &&
            (fTop2 < fTop3) && (fBot1 < fTop2))
        {
          pOut[i] = 2;
          continue;
        }
        if ((fBot1 > fTop2) && (fBot2 > fBot3) && (fTop2 > fTop3))
        {
          pOut[i] = 3;
          continue;
        }
      }
    }
    else
    {
      pOut[i] = 0;
    }
  }
}

//=============================================================================
// 输出函数7号：笔强度分析指标
// 基于 chan.py 笔的涨跌幅计算（参照 Bi.py amp 属性）
//=============================================================================

void Func7(int nCount, float *pOut, float *pIn, float *pHigh, float *pLow)
{
  int nPrevTop = 0, nPrevBot = 0;

  for (int i = 0; i < nCount; i++)
  {
    // 记录上一个笔端点位置
    if (i > 0 && pIn[i-1] == 1)
    {
      nPrevTop = i - 1;
    }
    else if (i > 0 && pIn[i-1] == -1)
    {
      nPrevBot = i - 1;
    }

    // 上升笔：计算涨幅百分比
    if (pIn[i] == 1)
    {
      if (pLow[nPrevBot] > 0)
        pOut[i] = (pHigh[i] - pLow[nPrevBot]) / pLow[nPrevBot] * 100;
    }
    // 下降笔：计算跌幅百分比
    else if (pIn[i] == -1)
    {
      if (pHigh[nPrevTop] > 0)
        pOut[i] = (pLow[i] - pHigh[nPrevTop]) / pHigh[nPrevTop] * 100;
    }
  }
}

//=============================================================================
// 输出函数8号：笔斜率分析指标
// 基于 chan.py 笔的单位K线涨跌幅
//=============================================================================

void Func8(int nCount, float *pOut, float *pIn, float *pHigh, float *pLow)
{
  int nPrevTop = 0, nPrevBot = 0;

  for (int i = 0; i < nCount; i++)
  {
    if (i > 0 && pIn[i-1] == 1)
    {
      nPrevTop = i - 1;
    }
    else if (i > 0 && pIn[i-1] == -1)
    {
      nPrevBot = i - 1;
    }

    // 上升笔斜率
    if (pIn[i] == 1)
    {
      int span = i - nPrevBot;
      if (span > 0)
        pOut[i] = (pHigh[i] - pLow[nPrevBot]) / span;
    }
    // 下降笔斜率
    else if (pIn[i] == -1)
    {
      int span = i - nPrevTop;
      if (span > 0)
        pOut[i] = (pLow[i] - pHigh[nPrevTop]) / span;
    }
  }
}

//=============================================================================
// 输出函数10号：MACD 调试输出
// 输出每根K线的 MACD 柱状图值（用于验证 MACD 计算正确性）
//
// 需要先调用 Func1 计算笔（Func1 内部同时计算 MACD），
// 然后用同样的数据调用 Func10 获取 MACD 值
//
// 输入参数:
//   pIn:  Func1 的笔标记输出（此函数不使用，仅为调用链传递）
//   pfINb: 未使用
//   pfINc: 控制输出内容: 0=MACD柱状图, 1=DIF, 2=DEA
//=============================================================================

void Func10(int nCount, float *pOut, float *pIn, float *pfINb, float *pfINc)
{
  (void)pIn;
  (void)pfINb;

  int mode = 0; // 默认输出 MACD
  if (pfINc != NULL && nCount > 0) {
    mode = (int)pfINc[0];
  }

  for (int i = 0; i < nCount; i++) {
    if (i < g_macd.size()) {
      switch (mode) {
        case 1:  pOut[i] = g_macd[i].DIF; break;
        case 2:  pOut[i] = g_macd[i].DEA; break;
        default: pOut[i] = g_macd[i].macd; break;
      }
    } else {
      pOut[i] = 0;
    }
  }
}

//=============================================================================
// 输出函数9号：线段标记信号
// 移植自 chan.py: Seg/SegListChan.py
//
// 输出编码：
//   +2 = 向上线段终点
//   -2 = 向下线段终点
//
// 需要先调用 Func1 计算笔（Func1 内部同时计算线段），
// 通达信公式: SEG:="chan2026.dll"(9, BISIGNAL, HIGH, LOW);
//=============================================================================

void Func9(int nCount, float *pOut, float *pIn, float *pHigh, float *pLow)
{
  (void)pIn;
  (void)pHigh;
  (void)pLow;

  for (int i = 0; i < nCount; i++) {
    pOut[i] = 0;
  }

  for (int i = 0; i < (int)g_segDetector.segPoints.size(); i++) {
    const SegPoint& sp = g_segDetector.segPoints[i];
    if (sp.origIdx >= 0 && sp.origIdx < nCount) {
      pOut[sp.origIdx] = (sp.dir == 1) ? 2.0f : -2.0f;
    }
  }
}

//=============================================================================
// 输出函数11号：买卖点调试输出（完整列表）
// 输出所有BSP条目（含同笔多类型），格式:
//   pOut[0] = 总条目数 N
//   pOut[1+2*i] = 原始K线索引
//   pOut[2+2*i] = 编码值
//
// 需要先调用 Func1 计算笔（Func1 内部同时计算买卖点），
// 通达信公式: DEBUG_BSP:="chan2026.dll"(11, BISIGNAL, HIGH, LOW);
//=============================================================================

void Func11(int nCount, float *pOut, float *pIn, float *pHigh, float *pLow)
{
  (void)pIn;
  (void)pHigh;
  (void)pLow;

  g_bsplist.fillOutputAll(nCount, pOut);
}

//=============================================================================
// DLL 函数注册表
//=============================================================================

static PluginTCalcFuncInfo Info[] =
{
  {1, &Func1},
  {2, &Func2},
  {3, &Func3},
  {4, &Func4},
  {5, &Func5},
  {6, &Func6},
  {7, &Func7},
  {8, &Func8},
  {9, &Func9},
  {10, &Func10},
  {11, &Func11},
  {0, NULL},
};

//=============================================================================
// 通达信 DLL 注册入口
//=============================================================================

BOOL RegisterTdxFunc(PluginTCalcFuncInfo **pInfo)
{
  if (*pInfo == NULL)
  {
    *pInfo = Info;
    return TRUE;
  }
  return FALSE;
}
