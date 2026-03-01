/*****************************************************************************
 * chan2026 - 精确三类买卖点计算
 * 移植自 chan.py: BuySellPoint/BSPointList.py
 *
 * 数据流：
 *   BiPoint[] + SegPoint[] + CZSList + CMACD → CBSPointList → CBSPoint[]
 *
 * Phase 4: 一类买卖点 (T1 趋势背驰 + T1P 盘整背驰)
 * Phase 5: 二类买卖点 (T2 + T2S) ✅
 * Phase 6: 三类买卖点 (T3A + T3B) ✅
 *
 * 配置参数来自 ChanConfig.h:
 *   - BS1_PEAK:            一类买卖点是否取极值
 *   - DIVERGENCE_RATE:     背驰比率阈值
 *   - MIN_ZS_CNT:          最小中枢数量
 *   - BSP1_ONLY_MULTIBI_ZS: 是否只计多笔中枢
 *   - CFG_MACD_ALGO:       MACD算法模式
 *   - BS_TYPE_1/1P/2/2S/3A/3B: 买卖点类型开关
 *****************************************************************************/

#ifndef __CBSPOINTLIST_H__
#define __CBSPOINTLIST_H__

#include "ChanConfig.h"
#include "BiDetector.h"
#include "SegDetector.h"
#include "CZSList.h"
#include "CMACD.h"
#include <vector>
#include <cmath>

// ==========================================================================
// 买卖点结构体
// ==========================================================================
struct CBSPoint {
    int biIdx;       // 买卖点所在笔索引（在 biPoints 中）
    int origIdx;     // 映射回原始K线索引
    float code;      // 输出编码：1/11/1.5/11.5/2/12/2.5/12.5/3/13/3.5/13.5
    bool is_buy;     // 是否为买点
    int relBsp1BiIdx; // 关联一类买卖点的笔索引 (-1=无)

    CBSPoint() : biIdx(-1), origIdx(-1), code(0), is_buy(false), relBsp1BiIdx(-1) {}
};

// ==========================================================================
// 买卖点列表（对应 Python BuySellPoint/BSPointList.py → CBSPointList）
// ==========================================================================
class CBSPointList {
public:
    std::vector<CBSPoint> bspList;      // 所有买卖点
    std::vector<int> bsp1BiIndices;     // 一类买卖点的笔索引列表（供 Phase5/6 查找用）

    void clear() {
        bspList.clear();
        bsp1BiIndices.clear();
    }

    // ==================================================================
    // cal: 核心入口（对应 Python CBSPointList.cal）
    // ==================================================================
    void cal(const std::vector<BiPoint>& biPoints,
             const std::vector<SegPoint>& segPoints,
             const CKLineCombiner& combiner,
             const CMACD& macd,
             const CZSList& zslist) {
        clear();
        if (segPoints.empty() || biPoints.size() < 4) return;

        // Phase 4: 一类买卖点
        cal_seg_bs1point(biPoints, segPoints, combiner, macd, zslist);

        // Phase 5: 二类买卖点
        cal_seg_bs2point(biPoints, segPoints, combiner);

        // Phase 6: 三类买卖点
        cal_seg_bs3point(biPoints, segPoints, combiner, zslist);
    }

    // ==================================================================
    // 将买卖点映射到输出数组（供 Func5 使用）
    // 同一位置有多个BSP时，保留码值最小的（优先级最高）
    // ==================================================================
    void fillOutput(int nCount, float* pOut) const {
        for (int i = 0; i < nCount; i++) pOut[i] = 0;
        for (int i = 0; i < (int)bspList.size(); i++) {
            const CBSPoint& bsp = bspList[i];
            if (bsp.origIdx >= 0 && bsp.origIdx < nCount) {
                float existing = pOut[bsp.origIdx];
                if (existing == 0 || bsp.code < existing) {
                    pOut[bsp.origIdx] = bsp.code;
                }
            }
        }
    }

    // ==================================================================
    // 将所有买卖点输出到数组（供 Func11 调试使用）
    // 格式: pOut[0]=count, pOut[1+2*i]=origIdx, pOut[2+2*i]=code
    // ==================================================================
    void fillOutputAll(int nCount, float* pOut) const {
        for (int i = 0; i < nCount; i++) pOut[i] = 0;
        int cnt = (int)bspList.size();
        int maxSlots = (nCount - 1) / 2;
        if (cnt > maxSlots) cnt = maxSlots;
        pOut[0] = (float)cnt;
        for (int i = 0; i < cnt; i++) {
            pOut[1 + 2*i] = (float)bspList[i].origIdx;
            pOut[2 + 2*i] = bspList[i].code;
        }
    }

private:

    // ==================================================================
    // cal_seg_bs1point: 遍历每个线段计算一类买卖点
    // 对应 Python BSPointList.cal_seg_bs1point (L157-161)
    // ==================================================================
    void cal_seg_bs1point(const std::vector<BiPoint>& biPoints,
                          const std::vector<SegPoint>& segPoints,
                          const CKLineCombiner& combiner,
                          const CMACD& macd,
                          const CZSList& zslist) {
        for (int si = 0; si < (int)segPoints.size(); si++) {
            cal_single_bs1point(si, biPoints, segPoints, combiner, macd, zslist);
        }
    }

    // ==================================================================
    // cal_single_bs1point: 单个线段内判断一类买卖点
    // 对应 Python BSPointList.cal_single_bs1point (L163-173)
    // ==================================================================
    void cal_single_bs1point(int segIdx,
                              const std::vector<BiPoint>& biPoints,
                              const std::vector<SegPoint>& segPoints,
                              const CKLineCombiner& combiner,
                              const CMACD& macd,
                              const CZSList& zslist) {
        const SegPoint& seg = segPoints[segIdx];
        int numBi = (int)biPoints.size() - 1;

        // 线段方向: seg.dir, 买点=下降线段末尾, 卖点=上升线段末尾
        // Python: BSP_CONF = self.config.GetBSConfig(seg.is_down())
        // DLL 中买卖点共享同一套 ChanConfig.h 配置

        // 获取线段内中枢列表
        std::vector<int> zsIndices;
        zslist.getZsForSeg(segIdx, zsIndices);

        // Python: zs_cnt = seg.get_multi_bi_zs_cnt() if BSP_CONF.bsp1_only_multibi_zs
        //         else len(seg.zs_lst)
        int zs_cnt = 0;
#if BSP1_ONLY_MULTIBI_ZS
        zs_cnt = zslist.getMultiBiZsCnt(segIdx);
#else
        zs_cnt = (int)zsIndices.size();
#endif

        // is_target_bsp: 中枢数量约束
        // Python: BSP_CONF.min_zs_cnt <= 0 or zs_cnt >= BSP_CONF.min_zs_cnt
        bool is_target_bsp = (MIN_ZS_CNT <= 0 || zs_cnt >= MIN_ZS_CNT);

        // 判断走 treat_bsp1 还是 treat_pz_bsp1
        // Python L167-173:
        //   if len(seg.zs_lst) > 0 and
        //      not seg.zs_lst[-1].is_one_bi_zs() and
        //      ((seg.zs_lst[-1].bi_out and seg.zs_lst[-1].bi_out.idx >= seg.end_bi.idx)
        //       or seg.zs_lst[-1].bi_lst[-1].idx >= seg.end_bi.idx) and
        //      seg.end_bi.idx - seg.zs_lst[-1].get_bi_in().idx > 2:
        //       → treat_bsp1
        //   else:
        //       → treat_pz_bsp1
        bool go_bsp1 = false;
        if (!zsIndices.empty()) {
            const CZS& lastZs = zslist.zsList[zsIndices.back()];
            if (!lastZs.isOneBiZs()) {
                bool cond_out = (lastZs.biOutIdx >= 0 && lastZs.biOutIdx >= seg.biIdx) ||
                                (!lastZs.biLst.empty() && lastZs.biLst.back() >= seg.biIdx);
                int biInIdx = lastZs.biInIdx >= 0 ? lastZs.biInIdx : lastZs.beginBiIdx;
                bool cond_depth = (seg.biIdx - biInIdx) > 2;
                go_bsp1 = cond_out && cond_depth;
            }
        }

        if (go_bsp1) {
            treat_bsp1(segIdx, is_target_bsp, biPoints, segPoints, combiner, macd, zslist, zsIndices);
        } else {
            treat_pz_bsp1(segIdx, is_target_bsp, biPoints, segPoints, combiner, macd);
        }
    }

    // ==================================================================
    // treat_bsp1: 一类买卖点 — 趋势背驰 (BSP_TYPE.T1)
    // 对应 Python BSPointList.treat_bsp1 (L175-184)
    // ==================================================================
    void treat_bsp1(int segIdx, bool is_target_bsp,
                    const std::vector<BiPoint>& biPoints,
                    const std::vector<SegPoint>& segPoints,
                    const CKLineCombiner& combiner,
                    const CMACD& macd,
                    const CZSList& zslist,
                    const std::vector<int>& zsIndices) {
#if !BS_TYPE_1
        return; // T1 未启用
#endif
        const SegPoint& seg = segPoints[segIdx];
        const CZS& lastZs = zslist.zsList[zsIndices.back()];

        // Python L177: break_peak, _ = last_zs.out_bi_is_peak(seg.end_bi.idx)
        float peakRate = 0;
        bool break_peak = lastZs.outBiIsPeak(biPoints, combiner, seg.biIdx, &peakRate);

        // Python L178-179: if BSP_CONF.bs1_peak and not break_peak: is_target_bsp = False
#if BS1_PEAK
        if (!break_peak) {
            is_target_bsp = false;
        }
#endif

        // Python L180: is_diver, divergence_rate = last_zs.is_divergence(BSP_CONF, out_bi=seg.end_bi)
        float divRatio = 0;
        bool is_diver = lastZs.isDivergence(biPoints, combiner, macd, seg.biIdx, &divRatio);

        // Python L181-182: if not is_diver: is_target_bsp = False
        if (!is_diver) {
            is_target_bsp = false;
        }

        // Python L184: self.add_bs(bs_type=BSP_TYPE.T1, bi=seg.end_bi, ...)
        // Python add_bs: T1/T1P 始终创建，is_target_bsp 仅控制是否加入 store
        if (is_target_bsp) {
            addBSPoint(seg.biIdx, biPoints, 1.0f, segPoints); // T1 输出
        }
        // 无论是否 target，都记录一类买卖点位置（供 T2/T2S/T3 查找用）
        bsp1BiIndices.push_back(seg.biIdx);
    }

    // ==================================================================
    // treat_pz_bsp1: 一类买卖点 P — 盘整背驰 (BSP_TYPE.T1P)
    // 对应 Python BSPointList.treat_pz_bsp1 (L186-205)
    // ==================================================================
    void treat_pz_bsp1(int segIdx, bool is_target_bsp,
                       const std::vector<BiPoint>& biPoints,
                       const std::vector<SegPoint>& segPoints,
                       const CKLineCombiner& combiner,
                       const CMACD& macd) {
#if !BS_TYPE_1P
        return; // T1P 未启用
#endif
        const SegPoint& seg = segPoints[segIdx];
        int numBi = (int)biPoints.size() - 1;

        // last_bi = seg.end_bi (索引 seg.biIdx)
        int lastBiIdx = seg.biIdx;
        // pre_bi = bi_list[last_bi.idx - 2]
        int preBiIdx = lastBiIdx - 2;
        if (preBiIdx < 0 || preBiIdx >= numBi) return;

        // Python L189: if last_bi.seg_idx != pre_bi.seg_idx: return
        // C++: 检查 pre_bi 是否在同一线段内
        if (preBiIdx < seg.startBiIdx) return;

        // Python L191: if last_bi.dir != seg.dir: return
        // 笔方向: biPoints[lastBiIdx].dir==-1(底) → UP笔(+1), ==+1(顶) → DOWN笔(-1)
        int lastBiDir = (biPoints[lastBiIdx].dir == -1) ? 1 : -1;
        if (lastBiDir != seg.dir) return;

        // 获取 last_bi 和 pre_bi 的 high/low
        float lastH, lastL, preH, preL;
        EigenElement::getBiHighLow(lastBiIdx, biPoints, combiner, lastH, lastL);
        EigenElement::getBiHighLow(preBiIdx, biPoints, combiner, preH, preL);

        // Python L193-196: 创新低/创新高检查
        bool isDown = (lastBiDir == -1); // DOWN 笔 = is_down
        if (isDown && lastL > preL) return;    // 下降笔要求创新低
        if (!isDown && lastH < preH) return;   // 上升笔要求创新高

        // MACD metric 计算
        // Python L197: in_metric = pre_bi.cal_macd_metric(BSP_CONF.macd_algo, is_reverse=False)
        int preStart = biPoints[preBiIdx].origIdx;
        int preEnd = (preBiIdx + 1 < (int)biPoints.size()) ?
                     biPoints[preBiIdx + 1].origIdx : preStart;
        bool preIsDown = (biPoints[preBiIdx].dir == 1); // 顶→底 = DOWN
        float inMetric = macd.cal_metric(preStart, preEnd, preIsDown, false);

        // Python L198: out_metric = last_bi.cal_macd_metric(BSP_CONF.macd_algo, is_reverse=True)
        int lastStart = biPoints[lastBiIdx].origIdx;
        int lastEnd = (lastBiIdx + 1 < (int)biPoints.size()) ?
                      biPoints[lastBiIdx + 1].origIdx : lastStart;
        bool lastIsDown = (biPoints[lastBiIdx].dir == 1);
        float outMetric = macd.cal_metric(lastStart, lastEnd, lastIsDown, true);

        // Python L199: is_diver = out_metric <= DIVERGENCE_RATE * in_metric
        bool is_diver;
        if (DIVERGENCE_RATE > 100.0f) {
            is_diver = true; // 保送模式
        } else {
            is_diver = outMetric <= DIVERGENCE_RATE * inMetric;
        }

        if (!is_diver) {
            is_target_bsp = false;
        }

        // Python L205: self.add_bs(bs_type=BSP_TYPE.T1P, bi=last_bi, ...)
        // Python add_bs: T1/T1P 始终创建，is_target_bsp 仅控制是否加入 store
        if (is_target_bsp) {
            addBSPoint(lastBiIdx, biPoints, 1.5f, segPoints); // T1P 输出
        }
        // 无论是否 target，都记录一类买卖点位置（供 T2/T2S/T3 查找用）
        bsp1BiIndices.push_back(lastBiIdx);
    }

    // ==================================================================
    // ===  Phase 5: 二类买卖点  =========================================
    // ==================================================================

    // 辅助: 获取笔终点值（对应 Python bi.get_end_val）
    // 笔 biIdx 的终点 = biPoints[biIdx + 1].value
    static float getBiEndVal(int biIdx,
                             const std::vector<BiPoint>& biPoints) {
        if (biIdx + 1 < (int)biPoints.size()) {
            return biPoints[biIdx + 1].value;
        }
        return biPoints[biIdx].value;
    }

    // 辅助: 获取笔起点值（对应 Python bi.get_begin_val）
    static float getBiBeginVal(int biIdx,
                               const std::vector<BiPoint>& biPoints) {
        return biPoints[biIdx].value;
    }

    // 辅助: 获取笔振幅（对应 Python bi.amp = abs(end_val - begin_val)）
    static float getBiAmp(int biIdx,
                          const std::vector<BiPoint>& biPoints) {
        return fabsf(getBiEndVal(biIdx, biPoints) - getBiBeginVal(biIdx, biPoints));
    }

    // 辅助: 类二买卖点是否突破一类买卖点（对应 Python bsp2s_break_bsp1）
    // bsp2s_bi.is_down() and bsp2s_bi._low() < break_bi._low()
    // or bsp2s_bi.is_up() and bsp2s_bi._high() > break_bi._high()
    static bool bsp2sBreakBsp1(int bsp2sBiIdx, int breakBiIdx,
                               const std::vector<BiPoint>& biPoints,
                               const CKLineCombiner& combiner) {
        int biDir = (biPoints[bsp2sBiIdx].dir == -1) ? 1 : -1;
        float sH, sL, bH, bL;
        EigenElement::getBiHighLow(bsp2sBiIdx, biPoints, combiner, sH, sL);
        EigenElement::getBiHighLow(breakBiIdx, biPoints, combiner, bH, bL);
        return (biDir == -1 && sL < bL) || (biDir == 1 && sH > bH);
    }

    // 辅助: 检查笔上是否已存在买卖点（对应 Python bsp_store_flat_dict.get）
    bool hasBspOnBi(int biIdx) const {
        for (int i = 0; i < (int)bspList.size(); i++) {
            if (bspList[i].biIdx == biIdx) return true;
        }
        return false;
    }

    // 辅助: 获取笔所属线段索引（简单搜索）
    static int getBiSegIdx(int biIdx, const std::vector<SegPoint>& segPoints) {
        for (int si = (int)segPoints.size() - 1; si >= 0; si--) {
            if (biIdx >= segPoints[si].startBiIdx && biIdx <= segPoints[si].biIdx) {
                return si;
            }
        }
        // 可能在最后一段之后（尾部虚段）
        if (!segPoints.empty() && biIdx > segPoints.back().biIdx) {
            return (int)segPoints.size(); // 虚段索引
        }
        return -1;
    }

    // ==================================================================
    // cal_seg_bs2point: 遍历每个线段计算二类买卖点
    // 对应 Python BSPointList.cal_seg_bs2point (L207-214)
    // ==================================================================
    void cal_seg_bs2point(const std::vector<BiPoint>& biPoints,
                          const std::vector<SegPoint>& segPoints,
                          const CKLineCombiner& combiner) {
#if !BS_TYPE_2 && !BS_TYPE_2S
        return; // T2 和 T2S 都未启用
#endif
        for (int si = 0; si < (int)segPoints.size(); si++) {
            treat_bsp2(si, biPoints, segPoints, combiner);
        }
    }

    // ==================================================================
    // treat_bsp2: 二类买卖点判断
    // 对应 Python BSPointList.treat_bsp2 (L216-242)
    // ==================================================================
    void treat_bsp2(int segIdx,
                    const std::vector<BiPoint>& biPoints,
                    const std::vector<SegPoint>& segPoints,
                    const CKLineCombiner& combiner) {
        int numBi = (int)biPoints.size() - 1;

        // Python L217: if len(seg_list) > 1
        if ((int)segPoints.size() <= 1) return;

        const SegPoint& seg = segPoints[segIdx];
        int bsp1BiIdx = seg.biIdx; // bsp1_bi = seg.end_bi

        // Python L221: if bsp1_bi.idx + 2 >= len(bi_list): return
        if (bsp1BiIdx + 2 >= numBi) return;

        int breakBiIdx = bsp1BiIdx + 1;  // break_bi
        int bsp2BiIdx = bsp1BiIdx + 2;   // bsp2_bi

        // Python L232: if BSP_CONF.bsp2_follow_1 and (not bsp1_bi or bsp1_bi.idx not in ...)
#if BSP2_FOLLOW_1
        if (!hasBspOnBi(bsp1BiIdx)) return;
#endif

        // Python L234: retrace_rate = bsp2_bi.amp() / break_bi.amp()
        float breakAmp = getBiAmp(breakBiIdx, biPoints);
        float bsp2Amp = getBiAmp(bsp2BiIdx, biPoints);
        if (breakAmp < 1e-10f) return; // 防止除零
        float retraceRate = bsp2Amp / breakAmp;

        // Python L235-236: bsp2_flag = retrace_rate <= BSP_CONF.max_bs2_rate
        bool bsp2Flag = (retraceRate <= MAX_BS2_RATE);

        if (bsp2Flag) {
#if BS_TYPE_2
            addBSPoint(bsp2BiIdx, biPoints, 2.0f, segPoints, bsp1BiIdx); // T2
#endif
        }
#if BSP2S_FOLLOW_2
        else {
            return; // Python L238-239: elif BSP_CONF.bsp2s_follow_2: return
        }
#endif

        // Python L240-242: T2S 检查
#if BS_TYPE_2S
        treat_bsp2s(segIdx, bsp2BiIdx, breakBiIdx, bsp1BiIdx,
                    biPoints, segPoints, combiner);
#endif
    }

    // ==================================================================
    // treat_bsp2s: 类二买卖点判断
    // 对应 Python BSPointList.treat_bsp2s (L244-277)
    // ==================================================================
    void treat_bsp2s(int segIdx, int bsp2BiIdx, int breakBiIdx, int bsp1BiIdx,
                     const std::vector<BiPoint>& biPoints,
                     const std::vector<SegPoint>& segPoints,
                     const CKLineCombiner& combiner) {
        int numBi = (int)biPoints.size() - 1;
        int bias = 2;
        float overlapLow = 0, overlapHigh = 0;
        bool overlapInit = false;

        // Python: while bsp2_bi.idx + bias < len(bi_list)
        while (bsp2BiIdx + bias < numBi) {
            int bsp2sBiIdx = bsp2BiIdx + bias;

            // Python L258: max_bsp2s_lv 检查
#if MAX_BSP2S_LV > 0
            if (bias / 2 > MAX_BSP2S_LV) break;
#endif

            // Python L260: 线段边界检查
            // bsp2s_bi.seg_idx != bsp2_bi.seg_idx and ...
            int bsp2sSegIdx = getBiSegIdx(bsp2sBiIdx, segPoints);
            int bsp2SegIdx = getBiSegIdx(bsp2BiIdx, segPoints);
            if (bsp2sSegIdx != bsp2SegIdx) {
                // Python: 如果跨线段且非最后一段或者跨了2段以上
                if (bsp2sSegIdx < (int)segPoints.size() - 1 ||
                    bsp2sSegIdx - bsp2SegIdx >= 2 ||
                    (bsp2SegIdx >= 0 && bsp2SegIdx < (int)segPoints.size() &&
                     segPoints[bsp2SegIdx].is_sure)) {
                    break;
                }
            }

            // 获取 bsp2s_bi 的 high/low
            float sH, sL;
            EigenElement::getBiHighLow(bsp2sBiIdx, biPoints, combiner, sH, sL);

            // Python L262-268: 重叠区间检查
            if (bias == 2) {
                float bsp2H, bsp2L;
                EigenElement::getBiHighLow(bsp2BiIdx, biPoints, combiner, bsp2H, bsp2L);
                // has_overlap(bsp2_bi._low(), bsp2_bi._high(), bsp2s_bi._low(), bsp2s_bi._high())
                if (!(sH > bsp2L && bsp2H > sL)) break; // 无重叠
                overlapLow = (bsp2L > sL) ? bsp2L : sL;    // max
                overlapHigh = (bsp2H < sH) ? bsp2H : sH;   // min
                overlapInit = true;
            } else {
                if (!overlapInit) break;
                // has_overlap(_low, _high, bsp2s_bi._low(), bsp2s_bi._high())
                if (!(sH > overlapLow && overlapHigh > sL)) break;
            }

            // Python L270: bsp2s_break_bsp1 检查
            if (bsp2sBreakBsp1(bsp2sBiIdx, breakBiIdx, biPoints, combiner)) break;

            // Python L272: retrace_rate
            float breakAmp = getBiAmp(breakBiIdx, biPoints);
            if (breakAmp < 1e-10f) break;
            float bsp2sEndVal = getBiEndVal(bsp2sBiIdx, biPoints);
            float breakEndVal = getBiEndVal(breakBiIdx, biPoints);
            float retraceRate = fabsf(bsp2sEndVal - breakEndVal) / breakAmp;

            // Python L273: if retrace_rate > BSP_CONF.max_bs2_rate: break
            if (retraceRate > MAX_BS2_RATE) break;

            // Python L276: add T2S
            addBSPoint(bsp2sBiIdx, biPoints, 2.5f, segPoints, bsp1BiIdx); // T2S
            bias += 2;
        }
    }

    // ==================================================================
    // ===  Phase 6: 三类买卖点  =========================================
    // ==================================================================

    // 辅助: bsp3_back2zs — 判断bsp3笔是否回到中枢区间内
    // 对应 Python bsp3_back2zs (L393-394)
    // 回到中枢 → true（不是有效三类买卖点）
    static bool bsp3Back2Zs(int bsp3BiIdx, const CZS& zs,
                            const std::vector<BiPoint>& biPoints,
                            const CKLineCombiner& combiner) {
        float bH, bL;
        EigenElement::getBiHighLow(bsp3BiIdx, biPoints, combiner, bH, bL);
        int biDir = (biPoints[bsp3BiIdx].dir == -1) ? 1 : -1;
        // 下降笔且low < zs.high → 回到中枢
        // 上升笔且high > zs.low → 回到中枢
        return (biDir == -1 && bL < zs.high) || (biDir == 1 && bH > zs.low);
    }

    // 辅助: bsp3_break_zspeak — 判断bsp3笔是否突破中枢内笔极值
    // 对应 Python bsp3_break_zspeak (L397-398)
    static bool bsp3BreakZsPeak(int bsp3BiIdx, const CZS& zs,
                                const std::vector<BiPoint>& biPoints,
                                const CKLineCombiner& combiner) {
        float bH, bL;
        EigenElement::getBiHighLow(bsp3BiIdx, biPoints, combiner, bH, bL);
        int biDir = (biPoints[bsp3BiIdx].dir == -1) ? 1 : -1;
        return (biDir == -1 && bH >= zs.peak_high) || (biDir == 1 && bL <= zs.peak_low);
    }

    // 辅助: cal_bsp3_bi_end_idx — 计算T3B搜索的截止笔索引
    // 对应 Python cal_bsp3_bi_end_idx (L401-413)
    static int calBsp3BiEndIdx(int nextSegIdx,
                               const std::vector<SegPoint>& segPoints,
                               const CZSList& zslist) {
        if (nextSegIdx < 0 || nextSegIdx >= (int)segPoints.size()) {
            return INT_MAX;
        }
        // Python: if seg.get_multi_bi_zs_cnt() == 0 and seg.next is None
        if (zslist.getMultiBiZsCnt(nextSegIdx) == 0 &&
            nextSegIdx == (int)segPoints.size() - 1) {
            return INT_MAX;
        }
        int end_bi_idx = segPoints[nextSegIdx].biIdx - 1;
        // 找 next_seg 中第一个多笔中枢的 bi_out
        std::vector<int> zsIndices;
        zslist.getZsForSeg(nextSegIdx, zsIndices);
        for (int i = 0; i < (int)zsIndices.size(); i++) {
            const CZS& zs = zslist.zsList[zsIndices[i]];
            if (zs.isOneBiZs()) continue;
            if (zs.biOutIdx >= 0) {
                end_bi_idx = zs.biOutIdx;
                break;
            }
        }
        return end_bi_idx;
    }

    // ==================================================================
    // cal_seg_bs3point: 遍历每个线段计算三类买卖点
    // 对应 Python BSPointList.cal_seg_bs3point (L279-303)
    // ==================================================================
    void cal_seg_bs3point(const std::vector<BiPoint>& biPoints,
                          const std::vector<SegPoint>& segPoints,
                          const CKLineCombiner& combiner,
                          const CZSList& zslist) {
#if !BS_TYPE_3A && !BS_TYPE_3B
        return; // T3A 和 T3B 都未启用
#endif
        int numSeg = (int)segPoints.size();
        int numBi = (int)biPoints.size() - 1;

        for (int si = 0; si < numSeg; si++) {
            const SegPoint& seg = segPoints[si];

            // Python L286: if len(seg_list) > 1
            if (numSeg <= 1) continue;

            int bsp1BiIdx = seg.biIdx;
            int nextSegIdx = si + 1;

            // Python L299: bsp3_follow_1 检查
#if BSP3_FOLLOW_1
            if (!hasBspOnBi(bsp1BiIdx)) continue;
#endif

            // 查找关联的一类买卖点
            int relBsp1BiIdx = -1;
            for (int k = 0; k < (int)bsp1BiIndices.size(); k++) {
                if (bsp1BiIndices[k] == bsp1BiIdx) {
                    relBsp1BiIdx = bsp1BiIdx;
                    break;
                }
            }

            // T3A: 中枢在一类买卖点之后（next_seg 中的中枢）
#if BS_TYPE_3A
            if (nextSegIdx < numSeg) {
                treat_bsp3_after(nextSegIdx, bsp1BiIdx, relBsp1BiIdx,
                                 biPoints, segPoints, combiner, zslist);
            }
#endif

            // T3B: 中枢在一类买卖点之前（当前 seg 中的中枢）
#if BS_TYPE_3B
            treat_bsp3_before(si, nextSegIdx, bsp1BiIdx, relBsp1BiIdx,
                              biPoints, segPoints, combiner, zslist);
#endif
        }
    }

    // ==================================================================
    // treat_bsp3_after: 三类买卖点a — 中枢在一类买卖点之后
    // 对应 Python BSPointList.treat_bsp3_after (L305-344)
    // ==================================================================
    void treat_bsp3_after(int nextSegIdx, int bsp1BiIdx, int relBsp1BiIdx,
                          const std::vector<BiPoint>& biPoints,
                          const std::vector<SegPoint>& segPoints,
                          const CKLineCombiner& combiner,
                          const CZSList& zslist) {
        int numBi = (int)biPoints.size() - 1;
        int numSeg = (int)segPoints.size();
        const SegPoint& nextSeg = segPoints[nextSegIdx];

        // Python L315: first_zs = next_seg.get_first_multi_bi_zs()
        int firstZsIdx = zslist.getFirstMultiBiZs(nextSegIdx);
        if (firstZsIdx < 0) return;

        // Python L318: strict_bsp3 检查
#if STRICT_BSP3
        {
            const CZS& firstZs = zslist.zsList[firstZsIdx];
            int biInIdx = firstZs.biInIdx >= 0 ? firstZs.biInIdx : firstZs.beginBiIdx;
            if (biInIdx != bsp1BiIdx + 1) return;
        }
#endif

        // Python L322: bsp3a_max_zs_cnt
        int maxZsCnt = BSP3A_MAX_ZS_CNT;

        // Python L323: for zs_idx, zs in enumerate(next_seg.get_multi_bi_zs_lst())
        std::vector<int> multiBiZsIndices;
        zslist.getMultiBiZsLst(nextSegIdx, multiBiZsIndices);

        for (int zsIter = 0; zsIter < (int)multiBiZsIndices.size(); zsIter++) {
            if (zsIter >= maxZsCnt) break;

            const CZS& zs = zslist.zsList[multiBiZsIndices[zsIter]];

            // Python L326: if zs.bi_out is None or zs.bi_out.idx+1 >= len(bi_list)
            if (zs.biOutIdx < 0 || zs.biOutIdx + 1 >= numBi) break;

            int bsp3BiIdx = zs.biOutIdx + 1;

            // Python L329-334: parent_seg 检查
            int bsp3SegIdx = getBiSegIdx(bsp3BiIdx, segPoints);
            if (bsp3SegIdx != nextSegIdx) {
                if (nextSegIdx != numSeg - 1) break;
                // Python L333: if len(bsp3_bi.parent_seg.bi_list) >= 3: break
                if (bsp3SegIdx >= 0 && bsp3SegIdx < numSeg) {
                    int parentSegBiCnt = segPoints[bsp3SegIdx].biIdx -
                                         segPoints[bsp3SegIdx].startBiIdx + 1;
                    if (parentSegBiCnt >= 3) break;
                }
            }

            // Python L335: if bsp3_bi.dir == next_seg.dir: break
            int bsp3Dir = (biPoints[bsp3BiIdx].dir == -1) ? 1 : -1;
            if (bsp3Dir == nextSeg.dir) break;

            // Python L337: seg_idx 检查
            if (bsp3SegIdx != nextSegIdx && nextSegIdx < numSeg - 2) break;

            // Python L339: if bsp3_back2zs(bsp3_bi, zs): continue
            if (bsp3Back2Zs(bsp3BiIdx, zs, biPoints, combiner)) continue;

            // Python L341-343: bsp3_peak 检查
#if BSP3_PEAK
            if (!bsp3BreakZsPeak(bsp3BiIdx, zs, biPoints, combiner)) continue;
#endif

            // Python L344: add T3A
            addBSPoint(bsp3BiIdx, biPoints, 3.0f, segPoints, relBsp1BiIdx);
        }
    }

    // ==================================================================
    // treat_bsp3_before: 三类买卖点b — 中枢在一类买卖点之前
    // 对应 Python BSPointList.treat_bsp3_before (L346-374)
    // ==================================================================
    void treat_bsp3_before(int segIdx, int nextSegIdx, int bsp1BiIdx,
                           int relBsp1BiIdx,
                           const std::vector<BiPoint>& biPoints,
                           const std::vector<SegPoint>& segPoints,
                           const CKLineCombiner& combiner,
                           const CZSList& zslist) {
        int numBi = (int)biPoints.size() - 1;
        int numSeg = (int)segPoints.size();

        // Python L357: cmp_zs = seg.get_final_multi_bi_zs()
        int cmpZsIdx = zslist.getFinalMultiBiZs(segIdx);
        if (cmpZsIdx < 0) return;
        const CZS& cmpZs = zslist.zsList[cmpZsIdx];

        // Python L362: strict_bsp3 检查
#if STRICT_BSP3
        if (cmpZs.biOutIdx < 0 || cmpZs.biOutIdx != bsp1BiIdx) return;
#endif

        // Python L364: end_bi_idx = cal_bsp3_bi_end_idx(next_seg)
        int endBiIdx = (nextSegIdx < numSeg) ?
                       calBsp3BiEndIdx(nextSegIdx, segPoints, zslist) : INT_MAX;

        // Python L365: for bsp3_bi in bi_list[bsp1_bi.idx+2::2]
        for (int bsp3BiIdx = bsp1BiIdx + 2; bsp3BiIdx < numBi; bsp3BiIdx += 2) {
            // Python L366: if bsp3_bi.idx > end_bi_idx: break
            if (bsp3BiIdx > endBiIdx) break;

            // Python L369: seg_idx 检查
            int bsp3SegIdx = getBiSegIdx(bsp3BiIdx, segPoints);
            if (bsp3SegIdx != nextSegIdx && bsp3SegIdx < numSeg - 1) break;

            // Python L371: if bsp3_back2zs(bsp3_bi, cmp_zs): continue
            if (bsp3Back2Zs(bsp3BiIdx, cmpZs, biPoints, combiner)) continue;

            // Python L373: add T3B + break
            addBSPoint(bsp3BiIdx, biPoints, 3.5f, segPoints, relBsp1BiIdx);
            break;
        }
    }

    // ==================================================================
    // addBSPoint: 添加买卖点
    // baseCode: 1=T1, 1.5=T1P, 2=T2, 2.5=T2S, 3=T3A, 3.5=T3B
    // 买点编码 = baseCode, 卖点编码 = baseCode + 10
    // ==================================================================
    void addBSPoint(int biIdx,
                    const std::vector<BiPoint>& biPoints,
                    float baseCode,
                    const std::vector<SegPoint>& segPoints,
                    int relBsp1BiIdx = -1) {
        if (biIdx < 0 || biIdx >= (int)biPoints.size() - 1) return;

        // 判断买/卖: 下降笔 → 买点, 上升笔 → 卖点
        // biPoints[biIdx].dir: -1=底(笔终点为底) → UP笔 → 卖点
        //                      +1=顶(笔终点为顶) → DOWN笔 → 买点
        // Python: is_buy = bi.is_down()
        // 笔biIdx: biPoints[biIdx] → biPoints[biIdx+1]
        int biDir = (biPoints[biIdx].dir == -1) ? 1 : -1;
        bool is_buy = (biDir == -1); // DOWN笔的终点是底 → 买点

        CBSPoint bsp;
        bsp.biIdx = biIdx;
        bsp.is_buy = is_buy;
        bsp.code = is_buy ? baseCode : (baseCode + 10.0f);
        bsp.relBsp1BiIdx = relBsp1BiIdx;

        // 映射到原始K线索引：笔的终点
        if (biIdx + 1 < (int)biPoints.size()) {
            bsp.origIdx = biPoints[biIdx + 1].origIdx;
        } else {
            bsp.origIdx = biPoints[biIdx].origIdx;
        }

        // Python add_bs: 同笔已存在时追加类型(add_another_bsp_prop)
        // DLL: 直接添加新条目，允许同笔多类型
        // fillOutput 时取码值最小的，fillOutputAll 输出全部
        bspList.push_back(bsp);
    }
};

#endif // __CBSPOINTLIST_H__
