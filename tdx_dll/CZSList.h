/*****************************************************************************
 * chan2026 - 线段内中枢计算
 * 移植自 chan.py: ZS/ZS.py + ZS/ZSList.py + KLine/KLine_List.py
 *
 * 数据流：
 *   BiPoint[] + SegPoint[] → CZSList → CZS[]
 *   →（Phase 4-6 买卖点判断使用）
 *
 * 与现有 CCentroid 的关系：
 *   - CCentroid 保留但不再被 Func2/3/4 使用
 *   - Func2/3/4 已改为使用 CZSList，与买卖点(Func5)使用同一套中枢

 *
 * 配置参数来自 ChanConfig.h:
 *   - ZS_COMBINE:      是否进行中枢合并
 *   - ZS_COMBINE_MODE: 中枢合并模式 (0=zs, 1=peak)
 *   - ONE_BI_ZS:       是否允许单笔中枢
 *   - DIVERGENCE_RATE:  背驰比率阈值
 *****************************************************************************/

#ifndef __CZSLIST_H__
#define __CZSLIST_H__

#include "ChanConfig.h"
#include "BiDetector.h"
#include "SegDetector.h"
#include "CMACD.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cfloat>

// ==========================================================================
// 中枢结构体（对应 Python ZS/ZS.py → CZS）
//
// 用笔索引代替 Python 的对象引用
// ==========================================================================
struct CZS {
    int beginBiIdx;       // 中枢起始笔索引（在 biPoints 中）
    int endBiIdx;         // 中枢结束笔索引
    float high;           // 中枢上沿：min(bi._high()) of 构成笔
    float low;            // 中枢下沿：max(bi._low()) of 构成笔
    float peak_high;      // 中枢内笔的最高点
    float peak_low;       // 中枢内笔的最低点
    bool is_sure;         // 是否确认

    // 买卖点计算所需属性（在 updateZsInSeg 中设置）
    int biInIdx;          // 进中枢笔索引（beginBiIdx - 1）, -1=无
    int biOutIdx;         // 出中枢笔索引（endBiIdx + 1），-1=未出
    std::vector<int> biLst; // 中枢内笔索引列表 [beginBiIdx..endBiIdx]

    // 所属线段的索引（在 updateZsInSeg 中设置）
    int segIdx;           // 在 segPoints 中的索引, -1=无

    CZS() : beginBiIdx(-1), endBiIdx(-1), high(0), low(0),
            peak_high(-FLT_MAX), peak_low(FLT_MAX), is_sure(true),
            biInIdx(-1), biOutIdx(-1), segIdx(-1) {}

    // ---- 初始化 ----

    // 从笔列表初始化（对应 Python CZS.__init__）
    void init(const std::vector<int>& lst,
              const std::vector<BiPoint>& biPoints,
              const CKLineCombiner& combiner,
              bool _is_sure) {
        if (lst.empty()) return;

        beginBiIdx = lst[0];
        is_sure = _is_sure;
        biInIdx = -1;
        biOutIdx = -1;
        biLst.clear();
        segIdx = -1;

        // update_zs_range: low = max(bi._low()), high = min(bi._high())
        updateRange(lst, biPoints, combiner);

        // update_zs_end for each item
        peak_high = -FLT_MAX;
        peak_low = FLT_MAX;
        for (int biIdx : lst) {
            updateEnd(biIdx, biPoints, combiner);
        }
    }

    // ---- 中枢区间计算 ----

    // updateRange: 对应 Python CZS.update_zs_range
    // low = max(bi._low()), high = min(bi._high())
    void updateRange(const std::vector<int>& lst,
                     const std::vector<BiPoint>& biPoints,
                     const CKLineCombiner& combiner) {
        low = -FLT_MAX;
        high = FLT_MAX;
        for (int biIdx : lst) {
            float bHigh, bLow;
            EigenElement::getBiHighLow(biIdx, biPoints, combiner, bHigh, bLow);
            if (bLow > low) low = bLow;     // max(bi._low())
            if (bHigh < high) high = bHigh;  // min(bi._high())
        }
    }

    // ---- 中枢终点更新 ----

    // updateEnd: 对应 Python CZS.update_zs_end
    void updateEnd(int biIdx,
                   const std::vector<BiPoint>& biPoints,
                   const CKLineCombiner& combiner) {
        endBiIdx = biIdx;
        float bHigh, bLow;
        EigenElement::getBiHighLow(biIdx, biPoints, combiner, bHigh, bLow);
        if (bLow < peak_low) peak_low = bLow;
        if (bHigh > peak_high) peak_high = bHigh;
    }

    // ---- 判断方法 ----

    // isOneBiZs: 对应 Python CZS.is_one_bi_zs
    bool isOneBiZs() const {
        return beginBiIdx == endBiIdx;
    }

    // inRange: 对应 Python CZS.in_range
    // has_overlap(self.low, self.high, item._low(), item._high())
    bool inRange(float biHigh, float biLow) const {
        return high > biLow && biHigh > low;
    }

    // inRangeBi: 用笔索引判断
    bool inRangeBi(int biIdx,
                   const std::vector<BiPoint>& biPoints,
                   const CKLineCombiner& combiner) const {
        float bHigh, bLow;
        EigenElement::getBiHighLow(biIdx, biPoints, combiner, bHigh, bLow);
        return inRange(bHigh, bLow);
    }

    // ---- 扩展中枢 ----

    // tryAddToEnd: 对应 Python CZS.try_add_to_end
    bool tryAddToEnd(int biIdx,
                     const std::vector<BiPoint>& biPoints,
                     const CKLineCombiner& combiner) {
        float bHigh, bLow;
        EigenElement::getBiHighLow(biIdx, biPoints, combiner, bHigh, bLow);
        if (!inRange(bHigh, bLow)) {
            return false;
        }
        if (isOneBiZs()) {
            // Python: self.update_zs_range([self.begin_bi, item])
            std::vector<int> lst;
            lst.push_back(beginBiIdx);
            lst.push_back(biIdx);
            updateRange(lst, biPoints, combiner);
        }
        updateEnd(biIdx, biPoints, combiner);
        return true;
    }

    // ---- 中枢合并 ----

    // combine: 对应 Python CZS.combine
    // 返回 true 表示合并成功
    bool combine(CZS& other, const std::vector<SegPoint>& segPoints) {
        if (other.isOneBiZs()) return false;

        // Python: if self.begin_bi.seg_idx != zs2.begin_bi.seg_idx: return False
        // C++: 检查两个中枢是否在同一线段内
        if (segIdx != other.segIdx) return false;

#if ZS_COMBINE_MODE == 0
        // zs 模式：检查中枢区间重叠
        // has_overlap(self.low, self.high, zs2.low, zs2.high, equal=True)
        if (!(other.high >= low && high >= other.low)) return false;
        doCombine(other);
        return true;
#elif ZS_COMBINE_MODE == 1
        // peak 模式：检查 peak 区间重叠
        // has_overlap(self.peak_low, self.peak_high, zs2.peak_low, zs2.peak_high)
        if (peak_high > other.peak_low && other.peak_high > peak_low) {
            doCombine(other);
            return true;
        }
        return false;
#else
        return false;
#endif
    }

    // doCombine: 对应 Python CZS.do_combine
    void doCombine(CZS& other) {
        low = std::min(low, other.low);
        high = std::max(high, other.high);
        peak_low = std::min(peak_low, other.peak_low);
        peak_high = std::max(peak_high, other.peak_high);
        endBiIdx = other.endBiIdx;
        biOutIdx = other.biOutIdx;
    }

    // ---- 买卖点辅助方法 ----

    // endBiBreak: 对应 Python CZS.end_bi_break
    // 检查出中枢笔是否突破中枢
    bool endBiBreak(const std::vector<BiPoint>& biPoints,
                    const CKLineCombiner& combiner,
                    int outBiIdx_override = -1) const {
        int checkIdx = (outBiIdx_override >= 0) ? outBiIdx_override : biOutIdx;
        if (checkIdx < 0 || checkIdx >= (int)biPoints.size()) return false;

        // 获取笔方向和值
        // Python: (end_bi.is_down() and end_bi._low() < self.low) or
        //         (end_bi.is_up() and end_bi._high() > self.high)
        float bHigh, bLow;
        EigenElement::getBiHighLow(checkIdx, biPoints, combiner, bHigh, bLow);

        // 笔方向：biPoints[i].dir==-1(底) → UP笔, dir==+1(顶) → DOWN笔
        int biDir = (biPoints[checkIdx].dir == -1) ? 1 : -1;
        return (biDir == -1 && bLow < low) || (biDir == 1 && bHigh > high);
    }

    // isDivergence: 对应 Python CZS.is_divergence
    // 比较进中枢笔 vs 出中枢笔的 MACD 指标
    // 返回 (是否背驰, 背驰比率)
    bool isDivergence(const std::vector<BiPoint>& biPoints,
                      const CKLineCombiner& combiner,
                      const CMACD& macd,
                      int outBiIdx_override = -1,
                      float* outRatio = nullptr) const {
        int checkOutIdx = (outBiIdx_override >= 0) ? outBiIdx_override : biOutIdx;

        // endBiBreak 检查
        if (!endBiBreak(biPoints, combiner, checkOutIdx)) {
            if (outRatio) *outRatio = 0;
            return false;
        }
        if (biInIdx < 0 || checkOutIdx < 0) {
            if (outRatio) *outRatio = 0;
            return false;
        }

        // 进中枢笔的 MACD metric (is_reverse=false)
        int inStart = biPoints[biInIdx].origIdx;
        int inEnd = (biInIdx + 1 < (int)biPoints.size()) ?
                    biPoints[biInIdx + 1].origIdx : inStart;
        bool inIsDown = (biPoints[biInIdx].dir == 1); // 顶→底 = DOWN
        float inMetric = macd.cal_metric(inStart, inEnd, inIsDown, false);

        // 出中枢笔的 MACD metric (is_reverse=true)
        int outStart = biPoints[checkOutIdx].origIdx;
        int outEnd = (checkOutIdx + 1 < (int)biPoints.size()) ?
                     biPoints[checkOutIdx + 1].origIdx : outStart;
        bool outIsDown = (biPoints[checkOutIdx].dir == 1); // 顶→底 = DOWN
        float outMetric = macd.cal_metric(outStart, outEnd, outIsDown, true);

        float ratio = (inMetric > 1e-10f) ? outMetric / inMetric : 0;
        if (outRatio) *outRatio = ratio;

        if (DIVERGENCE_RATE > 100.0f) {
            // 保送模式
            return true;
        } else {
            return outMetric <= DIVERGENCE_RATE * inMetric;
        }
    }

    // outBiIsPeak: 对应 Python CZS.out_bi_is_peak
    // 返回出中枢笔是否为极值点
    bool outBiIsPeak(const std::vector<BiPoint>& biPoints,
                     const CKLineCombiner& combiner,
                     int endBiIdxLimit,
                     float* outPeakRate = nullptr) const {
        if (biOutIdx < 0) {
            if (outPeakRate) *outPeakRate = 0;
            return false;
        }
        if (biLst.empty()) {
            if (outPeakRate) *outPeakRate = 0;
            return false;
        }

        float biOutH, biOutL;
        EigenElement::getBiHighLow(biOutIdx, biPoints, combiner, biOutH, biOutL);
        int biOutDir = (biPoints[biOutIdx].dir == -1) ? 1 : -1; // UP or DOWN
        float biOutEndVal = biPoints[biOutIdx + 1 < (int)biPoints.size() ?
                            biOutIdx + 1 : biOutIdx].value;

        float peakRate = FLT_MAX;
        for (int bIdx : biLst) {
            if (bIdx > endBiIdxLimit) break;
            float bH, bL;
            EigenElement::getBiHighLow(bIdx, biPoints, combiner, bH, bL);
            if ((biOutDir == -1 && bL < biOutL) ||
                (biOutDir == 1 && bH > biOutH)) {
                if (outPeakRate) *outPeakRate = 0;
                return false;
            }
            float bEndVal = biPoints[bIdx + 1 < (int)biPoints.size() ?
                            bIdx + 1 : bIdx].value;
            float r = (biOutEndVal != 0) ?
                      fabsf(bEndVal - biOutEndVal) / fabsf(biOutEndVal) : FLT_MAX;
            if (r < peakRate) peakRate = r;
        }

        if (outPeakRate) *outPeakRate = peakRate;
        return true;
    }

    // isInside: 对应 Python CZS.is_inside(seg)
    // 检查中枢是否在给定线段内
    bool isInside(int segStartBiIdx, int segEndBiIdx) const {
        return (beginBiIdx >= segStartBiIdx && beginBiIdx <= segEndBiIdx);
    }
};


// ==========================================================================
// 中枢列表（对应 Python ZS/ZSList.py → CZSList）
//
// 按线段分段计算中枢，用于精确买卖点判断
// ==========================================================================
class CZSList {
public:
    std::vector<CZS> zsList;

    void clear() {
        zsList.clear();
        freeLst.clear();
    }

    // ======================================================================
    // cal_bi_zs: 核心入口（对应 Python CZSList.cal_bi_zs）
    // ======================================================================
    void cal_bi_zs(const std::vector<BiPoint>& biPoints,
                   const std::vector<SegPoint>& segPoints,
                   const CKLineCombiner& combiner) {
        clear();

        int numBi = (int)biPoints.size() - 1; // 笔的数量

#if ZS_ALGO == 1
        // ---- over_seg: 跨线段算法 ----
        // 对应 Python: elif self.config.zs_algo == "over_seg":
        //   self.clear_free_lst()
        //   begin_bi_idx = self.zs_lst[-1].end_bi.idx+1 if self.zs_lst else 0
        //   for bi in bi_lst[begin_bi_idx:]:
        //       self.update_overseg_zs(bi)
        clearFreeLst();
        int beginBiIdx = zsList.empty() ? 0 : zsList.back().endBiIdx + 1;
        for (int biIdx = beginBiIdx; biIdx < numBi; biIdx++) {
            updateOversegZs(biIdx, biPoints, combiner, segPoints);
        }
#elif ZS_ALGO == 2
        // ---- auto: 已确认线段用normal, 未确认线段用over_seg ----
        // 对应 Python ZSList.py L112-127: elif self.config.zs_algo == "auto":
        {
            // exist_sure_seg: 是否存在任何已确认线段
            bool existSureSeg = false;
            for (int si = 0; si < (int)segPoints.size(); si++) {
                if (segPoints[si].is_sure) { existSureSeg = true; break; }
            }

            bool sureSeen = false;   // sure_seg_appear
            bool switchedToOverSeg = false;

            for (int si = 0; si < (int)segPoints.size(); si++) {
                const SegPoint& seg = segPoints[si];
                if (seg.is_sure) sureSeen = true;

                // Python: if seg.is_sure or (not sure_seg_appear and exist_sure_seg):
                //             → normal方式处理该线段
                if (seg.is_sure || (!sureSeen && existSureSeg)) {
                    clearFreeLst();
                    addZsFromBiRange(biPoints, combiner, segPoints,
                                     seg.startBiIdx, seg.biIdx, seg.dir, seg.is_sure, si);
                } else {
                    // Python: else → over_seg方式，从 seg.start_bi 开始遍历所有后续笔
                    clearFreeLst();
                    for (int biIdx = seg.startBiIdx; biIdx < numBi; biIdx++) {
                        updateOversegZs(biIdx, biPoints, combiner, segPoints);
                    }
                    switchedToOverSeg = true;
                    break;  // over_seg 已处理完所有后续笔，退出线段循环
                }
            }

            // 如果所有线段都是 sure 且没有切换到 over_seg，
            // 处理最后线段之后的尾部笔（同 normal 模式）
            if (!switchedToOverSeg && !segPoints.empty()) {
                clearFreeLst();
                int tailStart = segPoints.back().biIdx + 1;
                if (tailStart < numBi) {
                    int tailDir = (segPoints.back().dir == 1) ? -1 : 1;
                    addZsFromBiRange(biPoints, combiner, segPoints,
                                     tailStart, numBi - 1, tailDir, false, -1);
                }
            }
        }
#else
        // ---- normal: 按线段分段算法 ----
        // 遍历每个线段
        for (int si = 0; si < (int)segPoints.size(); si++) {
            const SegPoint& seg = segPoints[si];
            clearFreeLst();

            // 线段内的笔范围: [seg.startBiIdx, seg.biIdx]
            addZsFromBiRange(biPoints, combiner, segPoints,
                             seg.startBiIdx, seg.biIdx, seg.dir, seg.is_sure, si);
        }

        // 处理未生成新线段的尾部笔
        if (!segPoints.empty()) {
            clearFreeLst();
            int tailStart = segPoints.back().biIdx + 1;
            if (tailStart < numBi) {
                int tailDir = (segPoints.back().dir == 1) ? -1 : 1;
                addZsFromBiRange(biPoints, combiner, segPoints,
                                 tailStart, numBi - 1, tailDir, false, -1);
            }
        }
#endif

        // 设置 bi_in / bi_out / bi_lst（对应 Python update_zs_in_seg）
        updateZsInSeg(biPoints, segPoints, combiner);
    }

    // 按线段索引获取中枢列表
    void getZsForSeg(int segIdx, std::vector<int>& outZsIndices) const {
        outZsIndices.clear();
        for (int i = 0; i < (int)zsList.size(); i++) {
            if (zsList[i].segIdx == segIdx) {
                outZsIndices.push_back(i);
            }
        }
    }

    // 获取线段内第一个多笔中枢（对应 Python Seg.get_first_multi_bi_zs）
    int getFirstMultiBiZs(int segIdx) const {
        for (int i = 0; i < (int)zsList.size(); i++) {
            if (zsList[i].segIdx == segIdx && !zsList[i].isOneBiZs()) {
                return i;
            }
        }
        return -1;
    }

    // 获取线段内最后一个多笔中枢（对应 Python Seg.get_final_multi_bi_zs）
    int getFinalMultiBiZs(int segIdx) const {
        for (int i = (int)zsList.size() - 1; i >= 0; i--) {
            if (zsList[i].segIdx == segIdx && !zsList[i].isOneBiZs()) {
                return i;
            }
        }
        return -1;
    }

    // 获取线段内多笔中枢数量（对应 Python Seg.get_multi_bi_zs_cnt）
    int getMultiBiZsCnt(int segIdx) const {
        int cnt = 0;
        for (int i = 0; i < (int)zsList.size(); i++) {
            if (zsList[i].segIdx == segIdx && !zsList[i].isOneBiZs()) {
                cnt++;
            }
        }
        return cnt;
    }

    // 获取线段内所有多笔中枢的索引列表（对应 Python Seg.get_multi_bi_zs_lst）
    void getMultiBiZsLst(int segIdx, std::vector<int>& outIndices) const {
        outIndices.clear();
        for (int i = 0; i < (int)zsList.size(); i++) {
            if (zsList[i].segIdx == segIdx && !zsList[i].isOneBiZs()) {
                outIndices.push_back(i);
            }
        }
    }

private:
    std::vector<int> freeLst; // 未构成中枢的笔索引队列

    void clearFreeLst() {
        freeLst.clear();
    }

    // ==================================================================
    // addZsFromBiRange: 对应 Python CZSList.add_zs_from_bi_range
    // 从笔区间构建中枢，只处理线段反向笔
    // ==================================================================
    void addZsFromBiRange(const std::vector<BiPoint>& biPoints,
                          const CKLineCombiner& combiner,
                          const std::vector<SegPoint>& segPoints,
                          int startBiIdx, int endBiIdx,
                          int segDir, bool isSure, int segIdx) {
        int dealBiCnt = 0;
        for (int biIdx = startBiIdx; biIdx <= endBiIdx && biIdx < (int)biPoints.size() - 1; biIdx++) {
            // 笔方向：biPoints[i].dir==-1(底) → UP笔(+1), dir==+1(顶) → DOWN笔(-1)
            int biDir = (biPoints[biIdx].dir == -1) ? 1 : -1;

            // Python: if bi.dir == seg_dir: continue
            // 只处理反向笔
            if (biDir == segDir) continue;

            if (dealBiCnt < 1) {
                // 第一笔：直接 addToFreeLst
                // Python: 防止 try_add_to_end 执行到上一个线段的中枢里面去
                addToFreeLst(biIdx, biPoints, combiner, segPoints, isSure, segIdx);
                dealBiCnt++;
            } else {
                update(biIdx, biPoints, combiner, segPoints, isSure, segIdx);
            }
        }
    }

    // ==================================================================
    // update: 对应 Python CZSList.update
    // ==================================================================
    void update(int biIdx,
                const std::vector<BiPoint>& biPoints,
                const CKLineCombiner& combiner,
                const std::vector<SegPoint>& segPoints,
                bool isSure, int segIdx) {
        if (freeLst.empty() && tryAddToEnd(biIdx, biPoints, combiner)) {
            // 扩展成功 → 尝试合并
            tryCombine(segPoints);
            return;
        }
        addToFreeLst(biIdx, biPoints, combiner, segPoints, isSure, segIdx);
    }

    // ==================================================================
    // tryAddToEnd: 对应 Python CZSList.try_add_to_end
    // ==================================================================
    bool tryAddToEnd(int biIdx,
                     const std::vector<BiPoint>& biPoints,
                     const CKLineCombiner& combiner) {
        if (zsList.empty()) return false;
        return zsList.back().tryAddToEnd(biIdx, biPoints, combiner);
    }

    // ==================================================================
    // addToFreeLst: 对应 Python CZSList.add_to_free_lst
    // ==================================================================
    void addToFreeLst(int biIdx,
                      const std::vector<BiPoint>& biPoints,
                      const CKLineCombiner& combiner,
                      const std::vector<SegPoint>& segPoints,
                      bool isSure, int segIdx) {
        // 防止笔新高或新低的更新带来bug
        if (!freeLst.empty() && biIdx == freeLst.back()) {
            freeLst.pop_back();
        }
        freeLst.push_back(biIdx);

        // 尝试构建中枢
        CZS* newZs = tryConstructZs(biPoints, combiner, isSure, segIdx);
        if (newZs != nullptr) {
            // 禁止第一笔就是中枢的起点
            // Python: if res is not None and res.begin_bi.idx > 0
            if (newZs->beginBiIdx > 0) {
                zsList.push_back(*newZs);
                clearFreeLst();
                tryCombine(segPoints);
            }
            delete newZs;
        }
    }

    // ==================================================================
    // tryConstructZs: 对应 Python CZSList.try_construct_zs (normal)
    // 返回 new CZS（调用者负责删除）或 nullptr
    // ==================================================================
    CZS* tryConstructZs(const std::vector<BiPoint>& biPoints,
                        const CKLineCombiner& combiner,
                        bool isSure, int segIdx) {
        // 只处理 "normal" 算法
#if !ONE_BI_ZS
        if (freeLst.size() == 1) {
            return nullptr;
        }
        // 取最后两笔
        if (freeLst.size() < 2) return nullptr;
        std::vector<int> lst;
        lst.push_back(freeLst[freeLst.size() - 2]);
        lst.push_back(freeLst[freeLst.size() - 1]);
#else
        // one_bi_zs: 允许单笔
        std::vector<int>& lst = freeLst;
#endif

        // 计算 min_high 和 max_low
        float minHigh = FLT_MAX;
        float maxLow = -FLT_MAX;
        for (int biIdx : lst) {
            float bHigh, bLow;
            EigenElement::getBiHighLow(biIdx, biPoints, combiner, bHigh, bLow);
            if (bHigh < minHigh) minHigh = bHigh;
            if (bLow > maxLow) maxLow = bLow;
        }

        // 中枢成立条件: min_high > max_low
        if (minHigh <= maxLow) return nullptr;

        CZS* zs = new CZS();
        zs->init(lst, biPoints, combiner, isSure);
        zs->segIdx = segIdx;
        return zs;
    }

    // ==================================================================
    // over_seg 专用方法
    // ==================================================================

    // getBiSegDir: 获取笔所属线段的方向
    // 对应 Python: bi.parent_seg.dir
    // 通过 segPoints 查找包含 biIdx 的线段
    int getBiSegDir(int biIdx,
                    const std::vector<BiPoint>& biPoints,
                    const std::vector<SegPoint>& segPoints) const {
        // 从后往前查找包含 biIdx 的线段
        for (int si = (int)segPoints.size() - 1; si >= 0; si--) {
            if (biIdx >= segPoints[si].startBiIdx && biIdx <= segPoints[si].biIdx) {
                return segPoints[si].dir;
            }
        }
        // 不在任何线段内（尾部笔），返回0
        return 0;
    }

    // getBiSegIdx: 获取笔所属线段的索引
    int getBiSegIdx(int biIdx,
                    const std::vector<SegPoint>& segPoints) const {
        for (int si = (int)segPoints.size() - 1; si >= 0; si--) {
            if (biIdx >= segPoints[si].startBiIdx && biIdx <= segPoints[si].biIdx) {
                return si;
            }
        }
        return -1;
    }

    // tryConstructZsOverSeg: 对应 Python try_construct_zs(zs_algo="over_seg")
    // 取最后3笔，检查首笔方向==所属线段方向则返回nullptr
    CZS* tryConstructZsOverSeg(const std::vector<BiPoint>& biPoints,
                               const CKLineCombiner& combiner,
                               const std::vector<SegPoint>& segPoints,
                               bool isSure) {
        // Python: if len(lst) < 3: return None
        if (freeLst.size() < 3) return nullptr;

        // lst = lst[-3:]
        std::vector<int> lst;
        lst.push_back(freeLst[freeLst.size() - 3]);
        lst.push_back(freeLst[freeLst.size() - 2]);
        lst.push_back(freeLst[freeLst.size() - 1]);

        // Python: if lst[0].dir == lst[0].parent_seg.dir:
        //             lst = lst[1:]
        //             return None
        int biDir0 = (biPoints[lst[0]].dir == -1) ? 1 : -1; // 笔方向
        int segDir0 = getBiSegDir(lst[0], biPoints, segPoints);
        if (segDir0 != 0 && biDir0 == segDir0) {
            return nullptr;
        }

        // 计算 min_high 和 max_low
        float minHigh = FLT_MAX;
        float maxLow = -FLT_MAX;
        for (int biIdx : lst) {
            float bHigh, bLow;
            EigenElement::getBiHighLow(biIdx, biPoints, combiner, bHigh, bLow);
            if (bHigh < minHigh) minHigh = bHigh;
            if (bLow > maxLow) maxLow = bLow;
        }

        // 中枢成立条件: min_high > max_low
        if (minHigh <= maxLow) return nullptr;

        CZS* zs = new CZS();
        zs->init(lst, biPoints, combiner, isSure);
        // over_seg: segIdx 由首笔所属线段决定
        zs->segIdx = getBiSegIdx(lst[0], segPoints);
        return zs;
    }

    // addToFreeLstOverSeg: over_seg 版 addToFreeLst
    // 使用 tryConstructZsOverSeg 替代 tryConstructZs
    void addToFreeLstOverSeg(int biIdx,
                             const std::vector<BiPoint>& biPoints,
                             const CKLineCombiner& combiner,
                             const std::vector<SegPoint>& segPoints,
                             bool isSure) {
        // 防止笔新高或新低的更新带来bug
        if (!freeLst.empty() && biIdx == freeLst.back()) {
            freeLst.pop_back();
        }
        freeLst.push_back(biIdx);

        // 尝试构建中枢 (over_seg 版)
        CZS* newZs = tryConstructZsOverSeg(biPoints, combiner, segPoints, isSure);
        if (newZs != nullptr) {
            if (newZs->beginBiIdx > 0) {
                zsList.push_back(*newZs);
                clearFreeLst();
                tryCombine(segPoints);
            }
            delete newZs;
        }
    }

    // updateOversegZs: 对应 Python CZSList.update_overseg_zs
    // over_seg 的逐笔更新逻辑
    void updateOversegZs(int biIdx,
                         const std::vector<BiPoint>& biPoints,
                         const CKLineCombiner& combiner,
                         const std::vector<SegPoint>& segPoints) {
        int numBi = (int)biPoints.size() - 1;

        // Python 条件1:
        // if len(self.zs_lst) and len(self.free_item_lst) == 0:
        //     if bi.next is None: return
        //     if bi.idx - self.zs_lst[-1].end_bi.idx <= 1
        //        and self.zs_lst[-1].in_range(bi.next)
        //        and self.zs_lst[-1].try_add_to_end(bi):
        //         return
        if (!zsList.empty() && freeLst.empty()) {
            // bi.next is None → biIdx + 1 >= numBi
            if (biIdx + 1 >= numBi) {
                return;
            }
            int nextBiIdx = biIdx + 1;
            if (biIdx - zsList.back().endBiIdx <= 1) {
                // in_range(bi.next)
                float nextH, nextL;
                EigenElement::getBiHighLow(nextBiIdx, biPoints, combiner, nextH, nextL);
                if (zsList.back().inRange(nextH, nextL)) {
                    // try_add_to_end(bi)
                    if (zsList.back().tryAddToEnd(biIdx, biPoints, combiner)) {
                        return;
                    }
                }
            }
        }

        // Python 条件2:
        // if len(self.zs_lst) and len(self.free_item_lst) == 0
        //    and self.zs_lst[-1].in_range(bi)
        //    and bi.idx - self.zs_lst[-1].end_bi.idx <= 1:
        //     return
        if (!zsList.empty() && freeLst.empty()) {
            float bH, bL;
            EigenElement::getBiHighLow(biIdx, biPoints, combiner, bH, bL);
            if (zsList.back().inRange(bH, bL) &&
                biIdx - zsList.back().endBiIdx <= 1) {
                return;
            }
        }

        // Python: self.add_to_free_lst(bi, bi.is_sure, zs_algo="over_seg")
        // bi.is_sure: DLL中所有笔默认 is_sure=true
        addToFreeLstOverSeg(biIdx, biPoints, combiner, segPoints, true);
    }

    // ==================================================================
    // tryCombine: 对应 Python CZSList.try_combine
    // 尝试合并最后两个中枢
    // ==================================================================
    void tryCombine(const std::vector<SegPoint>& segPoints) {
#if !ZS_COMBINE
        return;
#else
        while (zsList.size() >= 2 &&
               zsList[zsList.size() - 2].combine(zsList.back(), segPoints)) {
            zsList.pop_back(); // 合并后删除最后一个
        }
#endif
    }

    // ==================================================================
    // updateZsInSeg: 对应 Python KLine_List.py update_zs_in_seg
    // 设置每个中枢的 bi_in / bi_out / bi_lst，
    // 以及将中枢关联到所属线段
    // ==================================================================
    void updateZsInSeg(const std::vector<BiPoint>& biPoints,
                       const std::vector<SegPoint>& segPoints,
                       const CKLineCombiner& combiner) {
        int numBi = (int)biPoints.size() - 1;

        for (int zi = 0; zi < (int)zsList.size(); zi++) {
            CZS& zs = zsList[zi];

            // 设置 bi_in: 进中枢笔 = beginBiIdx - 1
            // Python: zs.set_bi_in(bi_list[zs.begin_bi.idx-1])
            if (zs.beginBiIdx > 0) {
                zs.biInIdx = zs.beginBiIdx - 1;
            }

            // 设置 bi_out: 出中枢笔 = endBiIdx + 1
            // Python: if zs.end_bi.idx+1 < len(bi_list):
            //             zs.set_bi_out(bi_list[zs.end_bi.idx+1])
            if (zs.endBiIdx + 1 < numBi) {
                zs.biOutIdx = zs.endBiIdx + 1;
            } else {
                zs.biOutIdx = -1;
            }

            // 设置 bi_lst: 中枢内笔列表
            // Python: zs.set_bi_lst(list(bi_list[zs.begin_bi.idx:zs.end_bi.idx+1]))
            zs.biLst.clear();
            for (int bIdx = zs.beginBiIdx; bIdx <= zs.endBiIdx && bIdx < numBi; bIdx++) {
                zs.biLst.push_back(bIdx);
            }

            // 确定所属线段
            // Python: if zs.is_inside(seg): seg.add_zs(zs)
            // segIdx 已在 addToFreeLst/tryConstructZs 中设置
            // 这里验证：如果 segIdx 有效，检查是否确实在线段内
            if (zs.segIdx >= 0 && zs.segIdx < (int)segPoints.size()) {
                const SegPoint& seg = segPoints[zs.segIdx];
                if (!zs.isInside(seg.startBiIdx, seg.biIdx)) {
                    zs.segIdx = -1; // 不在线段内，清除
                }
            }
        }
    }
};

#endif // __CZSLIST_H__
