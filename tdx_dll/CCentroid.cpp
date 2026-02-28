/*****************************************************************************
 * chan2026 - 中枢算法实现
 * 移植自 chan.py: ZS/ZS.py + ZS/ZSList.py
 *
 * 核心逻辑参照 ZSList.py 的 try_construct_zs 和 ZS.py 的中枢定义：
 *   - 中枢 = 至少3笔有重叠区间（min_high > max_low）
 *   - high = min(各笔_high), low = max(各笔_low)
 *   - 中枢终结 = 新笔突破中枢区间（high < low 反转）
 *
 * 配置参数来自 ChanConfig.h:
 *   - ZS_COMBINE:      终结后是否尝试与前中枢合并
 *   - ZS_COMBINE_MODE: 合并判断用中枢区间(0) or 峰值区间(1)
 *   - MIN_ZS_CNT:      通过 nZsCnt 记录中枢数量
 *****************************************************************************/

#include "CCentroid.h"

CCentroid::CCentroid()
{
  this->bValid = false;
  this->nTop1  = 0;
  this->nTop2  = 0;
  this->nBot1  = 0;
  this->nBot2  = 0;
  this->fTop1  = 0;
  this->fTop2  = 0;
  this->fBot1  = 0;
  this->fBot2  = 0;
  this->nLines = 0;
  this->nStart = 0;
  this->nEnd   = 0;
  this->fHigh  = 0;
  this->fLow   = 0;
  this->fPHigh = 0;
  this->fPLow  = 0;
  this->nZsCnt = 0;

  this->fZsPeakHigh    = 0;
  this->fZsPeakLow     = 0;
  this->fPrevZsHigh    = 0;
  this->fPrevZsLow     = 0;
  this->fPrevZsPeakHigh = 0;
  this->fPrevZsPeakLow  = 0;
  this->bHasPrevZs     = false;
}

CCentroid::~CCentroid()
{
}

// 中枢终结后尝试与前一个中枢合并
// 参照 Python: ZSList.py try_combine / ZSConfig.need_combine
bool CCentroid::TryCombineWithPrev()
{
#if !ZS_COMBINE
  return false;  // 不合并
#else
  if (!bHasPrevZs) return false;

#if ZS_COMBINE_MODE == 1
  // peak 模式: 使用中枢内笔的最高/最低点判断重叠
  // 如果当前中枢的峰值区间与前中枢有重叠，则合并
  if (fZsPeakHigh >= fPrevZsPeakLow && fPrevZsPeakHigh >= fZsPeakLow) {
    return true;  // 有重叠，合并
  }
#else
  // zs 模式 (默认): 使用中枢的高低区间判断重叠
  if (fHigh >= fPrevZsLow && fPrevZsHigh >= fLow) {
    return true;  // 有重叠，合并
  }
#endif

  return false;
#endif
}

// 推入高点并计算中枢状态
bool CCentroid::PushHigh(int nIndex, float fValue)
{
  if (bValid == true)
  {
    nLines++;
    fPHigh = fHigh;
    fPLow  = fLow;
    // 更新峰值跟踪
    if (fValue > fZsPeakHigh) fZsPeakHigh = fValue;
  }
  else
  {
    nLines = 0;
  }

  // 更新顶点位置信息
  nTop2 = nTop1;
  fTop2 = fTop1;
  nTop1 = nIndex;
  fTop1 = fValue;

  // 非中枢模式：尝试构建新中枢
  if (bValid == false)
  {
    // 更新 fHigh (中枢高边界 = min of tops)
    if (fTop1 < fTop2)
    {
      fHigh = fTop1;
    }
    else
    {
      fHigh = fTop2;
    }

    // 中枢识别：fHigh > fLow 意味着存在重叠区间
    if (fHigh > fLow)
    {
      // 确定中枢起点
      if (fBot1 < fBot2)
      {
        nStart = nBot2;
      }
      else
      {
        nStart = nTop2;
      }
      bValid = true;
      // 初始化峰值跟踪
      fZsPeakHigh = (fTop1 > fTop2) ? fTop1 : fTop2;
      fZsPeakLow  = (fBot1 < fBot2) ? fBot1 : fBot2;
    }
  }
  // 已在中枢中：尝试延伸或终结
  else
  {
    // 更新中枢高边界（取更小值，收窄区间）
    if (fHigh > fTop1)
    {
      fHigh = fTop1;
    }

    // 中枢终结检测：区间反转
    if (fHigh < fLow)
    {
      // 保存当前中枢信息用于合并判断
      float savedHigh = fPHigh;
      float savedLow  = fPLow;
      float savedPeakHigh = fZsPeakHigh;
      float savedPeakLow  = fZsPeakLow;
      int   savedEnd = nTop2;

      fHigh  = fTop1;
      fLow   = fBot1;
      nEnd   = nTop2;
      bValid = false;

      if (nLines > 2)
      {
        // 检查是否需要与前中枢合并
#if ZS_COMBINE
        if (bHasPrevZs && TryCombineWithPrev())
        {
          // 合并：延续前中枢的起点，更新中枢边界
          // 合并后不计为新中枢，而是扩展前中枢
          fPrevZsPeakHigh = (savedPeakHigh > fPrevZsPeakHigh) ? savedPeakHigh : fPrevZsPeakHigh;
          fPrevZsPeakLow  = (savedPeakLow < fPrevZsPeakLow) ? savedPeakLow : fPrevZsPeakLow;
          fPrevZsHigh = (savedHigh > fPrevZsHigh) ? fPrevZsHigh : savedHigh;
          fPrevZsLow  = (savedLow < fPrevZsLow) ? fPrevZsLow : savedLow;
        }
        else
        {
          // 不合并：记录当前中枢为前中枢
          fPrevZsHigh     = savedHigh;
          fPrevZsLow      = savedLow;
          fPrevZsPeakHigh = savedPeakHigh;
          fPrevZsPeakLow  = savedPeakLow;
          bHasPrevZs      = true;
          nZsCnt++;
        }
#else
        nZsCnt++;
#endif
        return true;
      }
    }
  }

  return false;
}

// 推入低点并计算中枢状态
// 逻辑与 PushHigh 对称
bool CCentroid::PushLow(int nIndex, float fValue)
{
  if (bValid == true)
  {
    nLines++;
    fPLow  = fLow;
    fPHigh = fHigh;
    // 更新峰值跟踪
    if (fValue < fZsPeakLow) fZsPeakLow = fValue;
  }
  else
  {
    nLines = 0;
  }

  // 更新底点位置信息
  nBot2 = nBot1;
  fBot2 = fBot1;
  nBot1 = nIndex;
  fBot1 = fValue;

  // 非中枢模式：尝试构建新中枢
  if (bValid == false)
  {
    // 更新 fLow (中枢低边界 = max of bots)
    if (fBot1 > fBot2)
    {
      fLow = fBot1;
    }
    else
    {
      fLow = fBot2;
    }

    // 中枢识别
    if (fHigh > fLow)
    {
      if (fTop1 > fTop2)
      {
        nStart = nTop2;
      }
      else
      {
        nStart = nBot2;
      }
      bValid = true;
      // 初始化峰值跟踪
      fZsPeakHigh = (fTop1 > fTop2) ? fTop1 : fTop2;
      fZsPeakLow  = (fBot1 < fBot2) ? fBot1 : fBot2;
    }
  }
  // 已在中枢中
  else
  {
    // 更新中枢低边界（取更大值，收窄区间）
    if (fLow < fBot1)
    {
      fLow = fBot1;
    }

    // 中枢终结检测
    if (fHigh < fLow)
    {
      // 保存当前中枢信息
      float savedHigh = fPHigh;
      float savedLow  = fPLow;
      float savedPeakHigh = fZsPeakHigh;
      float savedPeakLow  = fZsPeakLow;

      fHigh  = fTop1;
      fLow   = fBot1;
      nEnd   = nBot2;
      bValid = false;

      if (nLines > 2)
      {
#if ZS_COMBINE
        if (bHasPrevZs && TryCombineWithPrev())
        {
          fPrevZsPeakHigh = (savedPeakHigh > fPrevZsPeakHigh) ? savedPeakHigh : fPrevZsPeakHigh;
          fPrevZsPeakLow  = (savedPeakLow < fPrevZsPeakLow) ? savedPeakLow : fPrevZsPeakLow;
          fPrevZsHigh = (savedHigh > fPrevZsHigh) ? fPrevZsHigh : savedHigh;
          fPrevZsLow  = (savedLow < fPrevZsLow) ? fPrevZsLow : savedLow;
        }
        else
        {
          fPrevZsHigh     = savedHigh;
          fPrevZsLow      = savedLow;
          fPrevZsPeakHigh = savedPeakHigh;
          fPrevZsPeakLow  = savedPeakLow;
          bHasPrevZs      = true;
          nZsCnt++;
        }
#else
        nZsCnt++;
#endif
        return true;
      }
    }
  }

  return false;
}
