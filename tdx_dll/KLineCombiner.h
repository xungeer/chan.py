/*****************************************************************************
 * chan2026 - K线合并算法
 * 移植自 chan.py: Combiner/KLine_Combiner.py
 *
 * 配置参数来自 ChanConfig.h:
 *   - BI_FX_CHECK: 影响分型有效性检查
 *   - GAP_AS_KL:   影响跳空缺口处理
 *****************************************************************************/

#ifndef __KLINE_COMBINER_H__
#define __KLINE_COMBINER_H__

#include <vector>
#include <cmath>
#include "ChanConfig.h"

// 方向枚举
enum KLineDir {
    KDIR_UP   =  1,
    KDIR_DOWN = -1,
    KDIR_COMBINE = 0   // 合并（包含关系）
};

// 分型枚举
enum FxType {
    FX_UNKNOWN = 0,
    FX_TOP     = 1,
    FX_BOTTOM  = -1
};

// 合并后的K线元素
struct CombinedKLine {
    int   idx;          // 在合并K线序列中的索引
    int   startIdx;     // 对应原始K线序列起始索引
    int   endIdx;       // 对应原始K线序列终止索引（包含）
    float high;         // 合并后高点
    float low;          // 合并后低点
    KLineDir dir;       // 合并方向
    FxType fx;          // 分型类型
    bool  hasGapWithNext; // 是否与下一根K线有跳空缺口
};

// K线合并器：参照 KLine_Combiner.py 的 test_combine 和 try_add 逻辑
class CKLineCombiner {
public:
    std::vector<CombinedKLine> klcList;

    void clear() {
        klcList.clear();
    }

    // 判断包含关系，参照 KLine_Combiner.py test_combine
    KLineDir testCombine(const CombinedKLine& cur, float newHigh, float newLow) {
        if (cur.high >= newHigh && cur.low <= newLow) {
            return KDIR_COMBINE;  // 当前包含新K线
        }
        if (cur.high <= newHigh && cur.low >= newLow) {
            return KDIR_COMBINE;  // 新K线包含当前
        }
        if (cur.high > newHigh && cur.low > newLow) {
            return KDIR_DOWN;
        }
        if (cur.high < newHigh && cur.low < newLow) {
            return KDIR_UP;
        }
        return KDIR_COMBINE;  // 不应该到这里
    }

    // 添加一根K线，参照 KLine_Combiner.py try_add
    void addKLine(int origIdx, float high, float low) {
        if (klcList.empty()) {
            CombinedKLine klc;
            klc.idx = 0;
            klc.startIdx = origIdx;
            klc.endIdx = origIdx;
            klc.high = high;
            klc.low = low;
            klc.dir = KDIR_UP;  // 初始方向
            klc.fx = FX_UNKNOWN;
            klc.hasGapWithNext = false;
            klcList.push_back(klc);
            return;
        }

        CombinedKLine& last = klcList.back();
        KLineDir combDir = testCombine(last, high, low);

        if (combDir == KDIR_COMBINE) {
            // 合并K线，参照 KLine_Combiner.py try_add 中的合并逻辑
            // 根据方向决定合并方式
            if (high == low && high == last.high) return;  // 一字K线处理
            if (high == low && low == last.low) return;

            if (last.dir == KDIR_UP) {
                last.high = (high > last.high) ? high : last.high;
                last.low  = (low > last.low) ? low : last.low;
            } else {
                last.high = (high < last.high) ? high : last.high;
                last.low  = (low < last.low) ? low : last.low;
            }
            last.endIdx = origIdx;
        } else {
            // 检测跳空缺口（GAP_AS_KL 配置使用）
            bool hasGap = false;
            if (combDir == KDIR_UP) {
                hasGap = (low > last.high);   // 向上跳空
            } else if (combDir == KDIR_DOWN) {
                hasGap = (high < last.low);   // 向下跳空
            }
            last.hasGapWithNext = hasGap;

            // 不合并，新建K线
            CombinedKLine klc;
            klc.idx = (int)klcList.size();
            klc.startIdx = origIdx;
            klc.endIdx = origIdx;
            klc.high = high;
            klc.low = low;
            klc.dir = combDir;
            klc.fx = FX_UNKNOWN;
            klc.hasGapWithNext = false;
            klcList.push_back(klc);

            // 更新分型：参照 KLine_Combiner.py update_fx
            if (klcList.size() >= 3) {
                int n = (int)klcList.size();
                CombinedKLine& prev = klcList[n - 3];
                CombinedKLine& mid  = klcList[n - 2];
                CombinedKLine& next = klcList[n - 1];

                if (prev.high < mid.high && next.high < mid.high &&
                    prev.low  < mid.low  && next.low  < mid.low) {
                    mid.fx = FX_TOP;
                }
                else if (prev.high > mid.high && next.high > mid.high &&
                         prev.low  > mid.low  && next.low  > mid.low) {
                    mid.fx = FX_BOTTOM;
                }
            }
        }
    }

    // 对整个K线序列进行合并处理
    void process(int nCount, float *pHigh, float *pLow) {
        clear();
        for (int i = 0; i < nCount; i++) {
            addKLine(i, pHigh[i], pLow[i]);
        }
    }

    // 分型有效性检查
    // 参照 Python: KLine.check_fx_valid() 方法
    // 根据 BI_FX_CHECK 配置选择不同的检查模式
    //
    // 参数:
    //   fxIdx1: 第一个分型在合并K线序列中的索引
    //   fxIdx2: 第二个分型在合并K线序列中的索引
    //   fxType1: 第一个分型类型 (FX_TOP 或 FX_BOTTOM)
    //
    // 返回: true = 两个分型之间可以成笔
    bool checkFxValid(int fxIdx1, int fxIdx2, FxType fxType1) const {
        if (fxIdx1 < 0 || fxIdx2 < 0) return false;
        if (fxIdx1 >= (int)klcList.size() || fxIdx2 >= (int)klcList.size()) return false;

        const CombinedKLine& fx1 = klcList[fxIdx1];
        const CombinedKLine& fx2 = klcList[fxIdx2];

        if (fxType1 == FX_TOP) {
            // 顶->底: 要求 fx1.high > fx2区域高点, fx2.low < fx1区域低点
            float fx2_high, fx1_low;

#if BI_FX_CHECK == 1
            // loss 模式: 只检查分型K线本身
            fx2_high = fx2.high;
            fx1_low  = fx1.low;
#elif BI_FX_CHECK == 2
            // half 模式: 检查分型前2个K线元素
            fx2_high = fx2.high;
            if (fxIdx2 > 0) {
                float prevHigh = klcList[fxIdx2 - 1].high;
                if (prevHigh > fx2_high) fx2_high = prevHigh;
            }
            fx1_low = fx1.low;
            if (fxIdx1 + 1 < (int)klcList.size()) {
                float nextLow = klcList[fxIdx1 + 1].low;
                if (nextLow < fx1_low) fx1_low = nextLow;
            }
#elif BI_FX_CHECK == 3
            // totally 模式: 要求完全不重叠
            return fx1.low > fx2.high;
#else
            // strict 模式 (默认): 考虑分型三元素
            fx2_high = fx2.high;
            if (fxIdx2 > 0) {
                float prevHigh = klcList[fxIdx2 - 1].high;
                if (prevHigh > fx2_high) fx2_high = prevHigh;
            }
            if (fxIdx2 + 1 < (int)klcList.size()) {
                float nextHigh = klcList[fxIdx2 + 1].high;
                if (nextHigh > fx2_high) fx2_high = nextHigh;
            }
            fx1_low = fx1.low;
            if (fxIdx1 > 0) {
                float prevLow = klcList[fxIdx1 - 1].low;
                if (prevLow < fx1_low) fx1_low = prevLow;
            }
            if (fxIdx1 + 1 < (int)klcList.size()) {
                float nextLow = klcList[fxIdx1 + 1].low;
                if (nextLow < fx1_low) fx1_low = nextLow;
            }
#endif
            return (fx1.high > fx2_high) && (fx2.low < fx1_low);
        }
        else if (fxType1 == FX_BOTTOM) {
            // 底->顶: 要求 fx1.low < fx2区域低点, fx2.high > fx1区域高点
            float fx2_low, fx1_high;

#if BI_FX_CHECK == 1
            // loss 模式
            fx2_low  = fx2.low;
            fx1_high = fx1.high;
#elif BI_FX_CHECK == 2
            // half 模式
            fx2_low = fx2.low;
            if (fxIdx2 > 0) {
                float prevLow = klcList[fxIdx2 - 1].low;
                if (prevLow < fx2_low) fx2_low = prevLow;
            }
            fx1_high = fx1.high;
            if (fxIdx1 + 1 < (int)klcList.size()) {
                float nextHigh = klcList[fxIdx1 + 1].high;
                if (nextHigh > fx1_high) fx1_high = nextHigh;
            }
#elif BI_FX_CHECK == 3
            // totally 模式
            return fx1.high < fx2.low;
#else
            // strict 模式 (默认)
            fx2_low = fx2.low;
            if (fxIdx2 > 0) {
                float prevLow = klcList[fxIdx2 - 1].low;
                if (prevLow < fx2_low) fx2_low = prevLow;
            }
            if (fxIdx2 + 1 < (int)klcList.size()) {
                float nextLow = klcList[fxIdx2 + 1].low;
                if (nextLow < fx2_low) fx2_low = nextLow;
            }
            fx1_high = fx1.high;
            if (fxIdx1 > 0) {
                float prevHigh = klcList[fxIdx1 - 1].high;
                if (prevHigh > fx1_high) fx1_high = prevHigh;
            }
            if (fxIdx1 + 1 < (int)klcList.size()) {
                float nextHigh = klcList[fxIdx1 + 1].high;
                if (nextHigh > fx1_high) fx1_high = nextHigh;
            }
#endif
            return (fx1.low < fx2_low) && (fx2.high > fx1_high);
        }

        return false;
    }

    // 获取两个合并K线之间的跨度
    // 参照 Python: BiList.get_klc_span()
    // GAP_AS_KL = true 时，跳空缺口也计入跨度
    int getKlcSpan(int startKlcIdx, int endKlcIdx) const {
        int span = endKlcIdx - startKlcIdx;
#if GAP_AS_KL
        // 跳空缺口计入跨度
        if (span >= 4) return span; // 加速运算
        for (int i = startKlcIdx; i < endKlcIdx && i < (int)klcList.size(); i++) {
            if (klcList[i].hasGapWithNext) {
                span++;
            }
        }
#endif
        return span;
    }
};

#endif
