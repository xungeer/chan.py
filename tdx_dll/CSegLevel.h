/*****************************************************************************
 * chan2026 - 线段级别计算（线段的线段 + 线段级中枢 + 线段级买卖点）
 * Phase 8: 对应 Python KLine_List.cal_seg_and_zs() 第二层
 *
 * 数据流:
 *   SegPoint[] → buildSegAsBi() → segBiPoints[]
 *                                    ↓
 *                         CSegDetector.detect() → segSegPoints[]
 *                                    ↓
 *                         CZSList.cal_bi_zs() → segZsList
 *                                    ↓
 *                         CBSPointList.calSeg() → segBspList
 *
 * 关键差异:
 *   - 线段级买卖点使用 slope metric（不依赖 MACD 数组）
 *   - bsp1_only_multibi_zs = false
 *   - 买点编码基数 = 21，卖点编码基数 = 31
 *****************************************************************************/

#ifndef __CSEGLEVEL_H__
#define __CSEGLEVEL_H__

#include "BiDetector.h"
#include "SegDetector.h"
#include "CZSList.h"
#include "CBSPointList.h"
#include "CMACD.h"
#include <vector>
#include <cmath>

class CSegLevel {
public:
    // 线段级计算结果
    std::vector<BiPoint> segBiPoints;     // SegPoint[] 转换为 BiPoint[] 格式
    CSegDetector segSegDetector;           // 线段的线段检测器
    CZSList segZsList;                     // 线段级中枢
    CBSPointList segBspList;               // 线段级买卖点

    void clear() {
        segBiPoints.clear();
        segSegDetector.clear();
        segZsList.clear();
        segBspList.clear();
        segCombiner.clear();
    }

    // ==================================================================
    // cal: 主入口
    // 对应 Python cal_seg_and_zs() 第二层:
    //   cal_seg(seg_list, segseg_list, ...)
    //   segzs_list.cal_bi_zs(seg_list, segseg_list)
    //   seg_bs_point_lst.cal(seg_list, segseg_list)
    // ==================================================================
    void cal(const std::vector<BiPoint>& biPoints,
             const std::vector<SegPoint>& segPoints,
             const CKLineCombiner& combiner,
             const CMACD& macd,
             int nCount) {
        clear();

        if (segPoints.size() < 2) return; // 至少需要2个线段端点

        // 步骤1: 将 SegPoint[] 转换为 BiPoint[] 格式
        buildSegAsBi(biPoints, segPoints, combiner);
        if (segBiPoints.size() < 4) return; // 至少4个端点才能识别线段的线段

        // 步骤2: 构建虚拟合并器
        buildSegCombiner(biPoints, segPoints, combiner, nCount);

        // 步骤3: 线段的线段识别（复用 CSegDetector）
        segSegDetector.detect(segBiPoints, segCombiner);

        // 步骤4: 线段级中枢（复用 CZSList）
        segZsList.cal_bi_zs(segBiPoints, segSegDetector.segPoints, segCombiner);

        // 步骤5: 线段级买卖点（使用 slope metric）
        segBspList.calSeg(segBiPoints, segSegDetector.segPoints,
                          segCombiner, macd, segZsList);
    }

    // ==================================================================
    // fillOutput: 线段级买卖点输出
    // 编码：买点基数=21, 卖点基数=31
    // ==================================================================
    void fillOutput(int nCount, float* pOut) const {
        // 注意：不清零 pOut（可能已有笔级数据）
        for (int i = 0; i < (int)segBspList.bspList.size(); i++) {
            const CBSPoint& bsp = segBspList.bspList[i];
            if (bsp.origIdx >= 0 && bsp.origIdx < nCount) {
                float existing = pOut[bsp.origIdx];
                if (existing == 0 || bsp.code < existing) {
                    pOut[bsp.origIdx] = bsp.code;
                }
            }
        }
    }

    // ==================================================================
    // fillOutputSeparate: 纯线段级买卖点输出（Func12用）
    // ==================================================================
    void fillOutputSeparate(int nCount, float* pOut) const {
        for (int i = 0; i < nCount; i++) pOut[i] = 0;
        for (int i = 0; i < (int)segBspList.bspList.size(); i++) {
            const CBSPoint& bsp = segBspList.bspList[i];
            if (bsp.origIdx >= 0 && bsp.origIdx < nCount) {
                float existing = pOut[bsp.origIdx];
                if (existing == 0 || bsp.code < existing) {
                    pOut[bsp.origIdx] = bsp.code;
                }
            }
        }
    }

    // ==================================================================
    // calSlopeMetric: slope metric 计算
    // 对应 Python CSeg.Cal_MACD_slope()
    //
    // UP:   (end.high - begin.low) / end.high / span
    // DOWN: (begin.high - end.low) / begin.high / span
    // ==================================================================
    static float calSlopeMetric(int biIdx,
                                const std::vector<BiPoint>& biPoints) {
        if (biIdx < 0 || biIdx + 1 >= (int)biPoints.size()) return 0;

        int biDir = (biPoints[biIdx].dir == -1) ? 1 : -1; // UP or DOWN
        int startOrigIdx = biPoints[biIdx].origIdx;
        int endOrigIdx = biPoints[biIdx + 1].origIdx;
        float span = (float)(abs(endOrigIdx - startOrigIdx) + 1);
        if (span < 1) span = 1;

        if (biDir == 1) {
            // UP: begin=底, end=顶
            float beginLow = biPoints[biIdx].value;
            float endHigh  = biPoints[biIdx + 1].value;
            if (endHigh < 1e-10f) return 0;
            return (endHigh - beginLow) / endHigh / span;
        } else {
            // DOWN: begin=顶, end=底
            float beginHigh = biPoints[biIdx].value;
            float endLow    = biPoints[biIdx + 1].value;
            if (beginHigh < 1e-10f) return 0;
            return (beginHigh - endLow) / beginHigh / span;
        }
    }

private:
    CKLineCombiner segCombiner;  // 虚拟合并器

    // ==================================================================
    // buildSegAsBi: 将 SegPoint[] 转换为 BiPoint[] 格式
    //
    // 每个线段有两个端点（起始和结束），转换规则：
    // - 第一个线段的起始端点作为 segBiPoints[0]
    // - 每个线段的结束端点作为后续 segBiPoints
    // - dir: 端点类型 (+1=顶, -1=底)
    // - value: 端点对应的价格
    // - origIdx: 映射回原始K线索引
    // ==================================================================
    void buildSegAsBi(const std::vector<BiPoint>& biPoints,
                      const std::vector<SegPoint>& segPoints,
                      const CKLineCombiner& combiner) {
        segBiPoints.clear();

        // 第一个线段的起始端点
        const SegPoint& firstSeg = segPoints[0];
        BiPoint startBp;
        startBp.origIdx = biPoints[firstSeg.startBiIdx].origIdx;
        // 线段起点方向：上升线段起点=底(-1)，下降线段起点=顶(+1)
        startBp.dir = (firstSeg.dir == 1) ? -1 : 1;
        startBp.value = biPoints[firstSeg.startBiIdx].value;
        segBiPoints.push_back(startBp);

        // 每个线段的结束端点
        for (int si = 0; si < (int)segPoints.size(); si++) {
            const SegPoint& seg = segPoints[si];
            BiPoint endBp;
            // 线段终点K线索引
            if (seg.biIdx + 1 < (int)biPoints.size()) {
                endBp.origIdx = biPoints[seg.biIdx + 1].origIdx;
                endBp.value   = biPoints[seg.biIdx + 1].value;
            } else {
                endBp.origIdx = biPoints[seg.biIdx].origIdx;
                endBp.value   = biPoints[seg.biIdx].value;
            }
            // 线段终点方向：上升线段终点=顶(+1)，下降线段终点=底(-1)
            endBp.dir = (seg.dir == 1) ? 1 : -1;
            segBiPoints.push_back(endBp);
        }
    }

    // ==================================================================
    // buildSegCombiner: 构建虚拟合并器
    //
    // 每个"线段"→一个虚拟 CombinedKLine：
    // - high = 线段内最高价
    // - low  = 线段内最低价
    // - idx  = 在虚拟合并K线序列中的索引
    //
    // 注: CSegDetector 中 EigenElement::getBiHighLow() 实际上
    //     只使用 BiPoint.dir 和 BiPoint.value 来获取 high/low，
    //     不依赖 CKLineCombiner。但 CKLineCombiner 作为参数传入
    //     是必需的，所以构建一个兼容的虚拟合并器。
    // ==================================================================
    void buildSegCombiner(const std::vector<BiPoint>& biPoints,
                          const std::vector<SegPoint>& segPoints,
                          const CKLineCombiner& combiner,
                          int nCount) {
        segCombiner.clear();

        // 为每个线段创建一个虚拟合并K线
        // 虚拟相当于每个"笔"（线段）是一根合并K线
        for (int si = 0; si < (int)segPoints.size(); si++) {
            const SegPoint& seg = segPoints[si];

            // 线段覆盖的K线范围
            int kStart = biPoints[seg.startBiIdx].origIdx;
            int kEnd = (seg.biIdx + 1 < (int)biPoints.size()) ?
                        biPoints[seg.biIdx + 1].origIdx :
                        biPoints[seg.biIdx].origIdx;

            // 找线段内最高和最低价（遍历笔端点）
            float segHigh = -1e30f;
            float segLow  = 1e30f;
            for (int bi = seg.startBiIdx; bi <= seg.biIdx + 1 && bi < (int)biPoints.size(); bi++) {
                if (biPoints[bi].value > segHigh) segHigh = biPoints[bi].value;
                if (biPoints[bi].value < segLow)  segLow  = biPoints[bi].value;
            }

            CombinedKLine klc;
            klc.idx = si;
            klc.startIdx = kStart;
            klc.endIdx = kEnd;
            klc.high = segHigh;
            klc.low = segLow;
            klc.dir = (seg.dir == 1) ? KDIR_UP : KDIR_DOWN;
            klc.fx = FX_UNKNOWN;
            klc.hasGapWithNext = false;
            segCombiner.klcList.push_back(klc);
        }
    }
};

#endif // __CSEGLEVEL_H__
