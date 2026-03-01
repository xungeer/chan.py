/*****************************************************************************
 * chan2026 - 笔识别算法
 * 移植自 chan.py: Bi/BiList.py
 *
 * 配置参数来自 ChanConfig.h:
 *   - BI_ALGO:           笔识别算法 (normal/fx)
 *   - BI_STRICT:         笔严格模式
 *   - BI_FX_CHECK:       分型有效性检查模式
 *   - BI_END_IS_PEAK:    笔端点是否必须是区间极值
 *   - BI_ALLOW_SUB_PEAK: 是否允许次高/次低点
 *   - GAP_AS_KL:         跳空缺口处理
 *****************************************************************************/

#ifndef __BI_DETECTOR_H__
#define __BI_DETECTOR_H__

#include "KLineCombiner.h"
#include <vector>
#include <cmath>

// 笔的顶底点信息
struct BiPoint {
    int   klcIdx;       // 合并K线序列中的索引
    int   origIdx;      // 对应原始K线序列的索引（用于输出）
    int   dir;          // +1=顶, -1=底
    float value;        // 顶取high, 底取low
};

// 笔识别器：参照 BiList.py 的 update_bi / try_update_end / satisfy_bi_span 逻辑
class CBiDetector {
public:
    std::vector<BiPoint> biPoints;
    const float* pOrigHigh;   // 原始K线的最高价数组（用于峰值查找）
    const float* pOrigLow;    // 原始K线的最低价数组
    int nOrigCount;            // 原始K线数量

    CBiDetector() : pOrigHigh(nullptr), pOrigLow(nullptr), nOrigCount(0) {}

    void clear() {
        biPoints.clear();
    }

    // 参照 BiList.py satisfy_bi_span
    // 根据 BI_ALGO, BI_STRICT, GAP_AS_KL 配置决定笔的跨度要求
    bool satisfyBiSpan(int startKlcIdx, int endKlcIdx, const CKLineCombiner& combiner) {
#if BI_ALGO == 1
        // bi_algo = "fx": 不检查跨度，只要有效分型即可成笔
        (void)startKlcIdx;
        (void)endKlcIdx;
        (void)combiner;
        return true;
#else
        // bi_algo = "normal": 需要检查跨度
        int span = combiner.getKlcSpan(startKlcIdx, endKlcIdx);
#if BI_STRICT
        // 严格模式: 跨度 >= 4（对应5根合并K线含端点）
        return span >= 4;
#else
        // 宽松模式: 跨度 >= 3 且中间独立K线 >= 3
        // DLL中简化处理：使用合并K线数量近似
        if (span < 3) return false;
        // 统计中间的独立K线数量
        int unitKlCnt = 0;
        for (int i = startKlcIdx + 1; i < endKlcIdx; i++) {
            if (i < (int)combiner.klcList.size()) {
                // 每根合并K线至少包含1根独立K线
                unitKlCnt += (combiner.klcList[i].endIdx - combiner.klcList[i].startIdx + 1);
            }
        }
        return span >= 3 && unitKlCnt >= 3;
#endif
#endif
    }

    // 检查笔端点是否为区间极值
    // 参照 BiList.py end_is_peak()
    // 配置: BI_END_IS_PEAK
    bool endIsPeak(int startKlcIdx, int endKlcIdx, FxType startFx, const CKLineCombiner& combiner) {
#if !BI_END_IS_PEAK
        (void)startKlcIdx;
        (void)endKlcIdx;
        (void)startFx;
        (void)combiner;
        return true;  // 不检查
#else
        if (startFx == FX_BOTTOM) {
            // 底->顶: 检查终点是否是区间最高点
            float endHigh = combiner.klcList[endKlcIdx].high;
            for (int i = startKlcIdx + 1; i < endKlcIdx; i++) {
                if (i < (int)combiner.klcList.size() && combiner.klcList[i].high > endHigh) {
                    return false;  // 中间有更高的点
                }
            }
        } else if (startFx == FX_TOP) {
            // 顶->底: 检查终点是否是区间最低点
            float endLow = combiner.klcList[endKlcIdx].low;
            for (int i = startKlcIdx + 1; i < endKlcIdx; i++) {
                if (i < (int)combiner.klcList.size() && combiner.klcList[i].low < endLow) {
                    return false;  // 中间有更低的点
                }
            }
        }
        return true;
#endif
    }

    // 综合判断是否可以成笔
    // 参照 BiList.py can_make_bi()
    bool canMakeBi(int startKlcIdx, int endKlcIdx, FxType startFx, const CKLineCombiner& combiner) {
        // 1. 跨度检查
        if (!satisfyBiSpan(startKlcIdx, endKlcIdx, combiner)) {
            return false;
        }

        // 2. 分型有效性检查 (根据 BI_FX_CHECK 配置)
        if (!combiner.checkFxValid(startKlcIdx, endKlcIdx, startFx)) {
            return false;
        }

        // 3. 端点极值检查 (根据 BI_END_IS_PEAK 配置)
        if (!endIsPeak(startKlcIdx, endKlcIdx, startFx, combiner)) {
            return false;
        }

        return true;
    }

    // 基于合并后K线的分型检测笔
    // 参照 BiList.py 的核心逻辑
    void detect(const CKLineCombiner& combiner,
                const float* origHigh = nullptr, const float* origLow = nullptr, int origCount = 0) {
        clear();
        pOrigHigh = origHigh;
        pOrigLow = origLow;
        nOrigCount = origCount;

        const std::vector<CombinedKLine>& klcList = combiner.klcList;
        if (klcList.size() < 3) return;

        // 阶段1：找到第一个有效分型
        int firstFxIdx = -1;
        for (int i = 0; i < (int)klcList.size(); i++) {
            if (klcList[i].fx != FX_UNKNOWN) {
                firstFxIdx = i;
                break;
            }
        }
        if (firstFxIdx < 0) return;

        // 初始化状态
        BiPoint lastEnd;
        lastEnd.klcIdx = firstFxIdx;
        lastEnd.dir = (klcList[firstFxIdx].fx == FX_TOP) ? 1 : -1;
        lastEnd.origIdx = getOrigIdx(klcList[firstFxIdx], lastEnd.dir);
        lastEnd.value = (lastEnd.dir == 1) ? klcList[firstFxIdx].high : klcList[firstFxIdx].low;

        biPoints.push_back(lastEnd);

        // 阶段2：逐根扫描合并K线序列，寻找交替分型
        for (int i = firstFxIdx + 1; i < (int)klcList.size(); i++) {
            if (klcList[i].fx == FX_UNKNOWN) continue;

            BiPoint& last = biPoints.back();
            int lastDir = last.dir;

            if (klcList[i].fx == FX_TOP) {
                if (lastDir == 1) {
                    // 上一个也是顶，更新为更高的顶（参照 can_update_peak / try_update_end）
                    if (klcList[i].high >= last.value) {
                        last.klcIdx = i;
                        last.origIdx = getOrigIdx(klcList[i], 1);
                        last.value = klcList[i].high;
                    }
#if !BI_ALLOW_SUB_PEAK
                    // bi_allow_sub_peak=false: 检查是否可以通过删除最后一笔来更新
                    else if (biPoints.size() >= 2) {
                        // 尝试 update_peak 逻辑
                        BiPoint& prev = biPoints[biPoints.size() - 2];
                        if (prev.dir == 1 && klcList[i].high < prev.value) {
                            // 次高点可以更新
                            if (canMakeBi(prev.klcIdx, i, (FxType)prev.dir, combiner)) {
                                biPoints.pop_back(); // 删除最后一笔
                                BiPoint bp;
                                bp.klcIdx = i;
                                bp.dir = 1;
                                bp.origIdx = getOrigIdx(klcList[i], 1);
                                bp.value = klcList[i].high;
                                biPoints.push_back(bp);
                            }
                        }
                    }
#endif
                } else {
                    // 上一个是底，尝试生成新笔
                    if (canMakeBi(last.klcIdx, i, FX_BOTTOM, combiner)) {
                        if (klcList[i].high > last.value) {
                            BiPoint bp;
                            bp.klcIdx = i;
                            bp.dir = 1;
                            bp.origIdx = getOrigIdx(klcList[i], 1);
                            bp.value = klcList[i].high;
                            biPoints.push_back(bp);
                        }
                    }
                }
            }
            else if (klcList[i].fx == FX_BOTTOM) {
                if (lastDir == -1) {
                    // 上一个也是底，更新为更低的底
                    if (klcList[i].low <= last.value) {
                        last.klcIdx = i;
                        last.origIdx = getOrigIdx(klcList[i], -1);
                        last.value = klcList[i].low;
                    }
#if !BI_ALLOW_SUB_PEAK
                    // bi_allow_sub_peak=false: 次低点更新逻辑
                    else if (biPoints.size() >= 2) {
                        BiPoint& prev = biPoints[biPoints.size() - 2];
                        if (prev.dir == -1 && klcList[i].low > prev.value) {
                            if (canMakeBi(prev.klcIdx, i, (FxType)prev.dir, combiner)) {
                                biPoints.pop_back();
                                BiPoint bp;
                                bp.klcIdx = i;
                                bp.dir = -1;
                                bp.origIdx = getOrigIdx(klcList[i], -1);
                                bp.value = klcList[i].low;
                                biPoints.push_back(bp);
                            }
                        }
                    }
#endif
                } else {
                    // 上一个是顶，尝试生成新笔
                    if (canMakeBi(last.klcIdx, i, FX_TOP, combiner)) {
                        if (klcList[i].low < last.value) {
                            BiPoint bp;
                            bp.klcIdx = i;
                            bp.dir = -1;
                            bp.origIdx = getOrigIdx(klcList[i], -1);
                            bp.value = klcList[i].low;
                            biPoints.push_back(bp);
                        }
                    }
                }
            }
        }
    }

private:
    // 获取分型对应的原始K线索引
    // 对应 Python: end_klc.get_peak_klu(is_high=True/False)
    // 从合并K线的原始K线中找到实际的峰值K线
    // Python 从后往前遍历（lst[::-1]），找到 high==klc.high 或 low==klc.low 的K线
    int getOrigIdx(const CombinedKLine& klc, int dir) {
        if (pOrigHigh != nullptr && pOrigLow != nullptr) {
            if (dir == 1) {
                // 顶：找 high 最大的原始K线（从后往前，匹配 Python lst[::-1]）
                for (int i = klc.endIdx; i >= klc.startIdx; i--) {
                    if (i >= 0 && i < nOrigCount && pOrigHigh[i] == klc.high) {
                        return i;
                    }
                }
            } else {
                // 底：找 low 最小的原始K线（从后往前）
                for (int i = klc.endIdx; i >= klc.startIdx; i--) {
                    if (i >= 0 && i < nOrigCount && pOrigLow[i] == klc.low) {
                        return i;
                    }
                }
            }
        }
        return klc.endIdx;  // fallback
    }
};

#endif
