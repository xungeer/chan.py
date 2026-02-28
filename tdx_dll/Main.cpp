/*****************************************************************************
 * chan2026 - 通达信DLL接口主模块
 * 算法完全基于 chan.py 核心计算逻辑
 *
 * 导出函数说明：
 *   Func1: 笔顶底标记信号 (+1=顶, -1=底)
 *   Func2: 中枢高点数据
 *   Func3: 中枢低点数据
 *   Func4: 中枢起止信号
 *   Func5: 三类买卖点信号 (2=二买, 3=三买, 12=二卖, 13=三卖)
 *   Func6: 形态买卖点信号
 *   Func7: 笔强度分析
 *   Func8: 笔斜率分析
 *****************************************************************************/

#include "Main.h"

//=============================================================================
// 核心算法：K线合并 + 笔识别
// 移植自 chan.py: KLine_Combiner.py + BiList.py
//=============================================================================

// 全局合并器和笔检测器（每次 Func1 调用时重新计算）
static CKLineCombiner g_combiner;
static CBiDetector    g_detector;

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
//   pTime:  第一个元素代表化简遍数（保持与原版接口兼容）
void Func1(int nCount, float *pOut, float *pHigh, float *pLow, float *pTime)
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
  g_detector.detect(g_combiner.klcList);

  // 步骤3: 将笔端点标记映射回原始K线序列
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
// 输出函数5号：三类买卖点信号
// 移植自 chan.py: BuySellPoint/BSPointList.py
//
// 输出编码：
//   2  = 二类买点（回踩不破前低 -> fBot1 > fBot2）
//   3  = 三类买点（中枢终结后回踩 -> PushLow返回true）
//   12 = 二类卖点（反弹不破前高 -> fTop1 < fTop2）
//   13 = 三类卖点（中枢终结后反弹 -> PushHigh返回true）
//=============================================================================

void Func5(int nCount, float *pOut, float *pIn, float *pHigh, float *pLow)
{
  CCentroid Centroid;

  for (int i = 0; i < nCount; i++)
  {
    if (pIn[i] == 1)
    {
      if (Centroid.PushHigh(i, pHigh[i]))
      {
        // 三类卖点：中枢向上突破后回落形成新中枢终结
        pOut[i] = 13;
      }
      else if (Centroid.fTop1 < Centroid.fTop2)
      {
        // 二类卖点：反弹高点低于前一个高点
        pOut[i] = 12;
      }
      else
      {
        pOut[i] = 0;
      }
    }
    else if (pIn[i] == -1)
    {
      if (Centroid.PushLow(i, pLow[i]))
      {
        // 三类买点：中枢向下突破后反弹形成新中枢终结
        pOut[i] = 3;
      }
      else if (Centroid.fBot1 > Centroid.fBot2)
      {
        // 二类买点：回踩低点高于前一个低点
        pOut[i] = 2;
      }
      else
      {
        pOut[i] = 0;
      }
    }
  }
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
