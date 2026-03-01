/*****************************************************************************
 * chan2026 - 中枢算法（Func2/3/4 专用，流式实现）
 * 移植自 chan.py: ZS/ZS.py + ZS/ZSList.py
 * 采用逐笔推入的流式计算方式（区别于 CZSList.h 的批量计算）
 *
 * 配置参数来自 ChanConfig.h:
 *   - ZS_COMBINE:      是否进行中枢合并
 *   - ZS_COMBINE_MODE: 中枢合并模式
 *****************************************************************************/

#ifndef __CCENTROID_H__
#define __CCENTROID_H__

#include "ChanConfig.h"

struct CCentroid
{
  bool  bValid;
  int   nTop1, nTop2, nBot1, nBot2;
  float fTop1, fTop2, fBot1, fBot2;
  int   nLines, nStart, nEnd;
  float fHigh, fLow, fPHigh, fPLow;
  int   nZsCnt;       // 已完成的中枢数量（用于 MIN_ZS_CNT 判断）

  // 中枢合并相关（ZS_COMBINE 配置使用）
  float fZsPeakHigh;  // 中枢内笔的最高点（ZS_COMBINE_MODE=peak 使用）
  float fZsPeakLow;   // 中枢内笔的最低点（ZS_COMBINE_MODE=peak 使用）
  float fPrevZsHigh;  // 前一个中枢的高边界（合并判断用）
  float fPrevZsLow;   // 前一个中枢的低边界（合并判断用）
  float fPrevZsPeakHigh; // 前一个中枢的峰值高点
  float fPrevZsPeakLow;  // 前一个中枢的峰值低点
  bool  bHasPrevZs;   // 是否有前一个中枢

  CCentroid();
  ~CCentroid();

  bool PushHigh(int nIndex, float fValue);
  bool PushLow (int nIndex, float fValue);

  // 中枢终结后的合并检查
  bool TryCombineWithPrev();
};

#endif
