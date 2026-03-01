/*****************************************************************************
 * chan2026 - 线段识别算法
 * 移植自 chan.py: Seg/Eigen.py + Seg/EigenFX.py + Seg/SegListChan.py
 *                + Seg/SegListComm.py
 *
 * 核心数据流：
 *   BiPoint[] → 特征序列(EigenElement) → 分型(CEigenFX) → 线段(SegPoint)
 *
 * 配置参数来自 ChanConfig.h:
 *   - LEFT_SEG_METHOD: 残留线段处理方法
 *****************************************************************************/

#ifndef __SEG_DETECTOR_H__
#define __SEG_DETECTOR_H__

#include "KLineCombiner.h"
#include "BiDetector.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <climits>

// ==========================================================================
// 线段端点（对应 Python Seg.py 中 CSeg 的 start_bi/end_bi）
// ==========================================================================
struct SegPoint {
    int biIdx;       // biPoints 中的笔索引（线段终止笔）
    int startBiIdx;  // 线段起始笔索引
    int origIdx;     // 映射回原始K线索引（用于通达信输出）
    int dir;         // +1=上升线段, -1=下降线段
    bool is_sure;    // 是否确定线段
};

// ==========================================================================
// 特征序列元素（对应 Python Eigen.py → CEigen，继承 CKLine_Combiner[CBi]）
//
// 每个元素由1~N根同方向笔合并而成，记录最高/最低点
// DLL中用笔索引代替Python的对象引用链
// ==========================================================================
struct EigenElement {
    std::vector<int> biIdxList;   // 包含的笔索引列表
    float high;                    // 合并后高点
    float low;                     // 合并后低点
    int   klDir;                   // 合并方向: +1=UP, -1=DOWN
    int   fx;                      // 分型类型: 0=UNKNOWN, 1=TOP, -1=BOTTOM
    bool  gap;                     // 与前一元素是否有缺口

    EigenElement() : high(0), low(0), klDir(0), fx(0), gap(false) {}

    void init(int biIdx, float h, float l, int dir) {
        biIdxList.clear();
        biIdxList.push_back(biIdx);
        high = h;
        low  = l;
        klDir = dir;
        fx   = 0;
        gap  = false;
    }

    int size() const { return (int)biIdxList.size(); }

    int firstBiIdx() const { return biIdxList.front(); }
    int lastBiIdx()  const { return biIdxList.back(); }

    // 笔的high/low取值（对应 Python Combine_Item）
    // 在线段的特征序列中，笔 bi 的 high = bi._high(), low = bi._low()
    // Python: bi._high() = end_klc.high if is_up else begin_klc.high
    //         bi._low()  = begin_klc.low if is_up else end_klc.low
    // 但在DLL中，BiPoint只存 value(顶取high/底取low)和 dir
    // 需要通过 combiner 来获取笔的 high 和 low

    // test_combine: 对应 Python KLine_Combiner.test_combine
    // 判断新笔与当前元素的包含关系
    // exclude_included: true=线段特征序列分型(被包含返回INCLUDED), false=普通分型
    // allow_top_equal: 0=不启用, 1=顶相等不合并, -1=底相等不合并
    // 返回: 0=COMBINE, 1=UP, -1=DOWN, 2=INCLUDED
    int testCombine(float newHigh, float newLow, bool exclude_included, int allow_top_equal) {
        if (high >= newHigh && low <= newLow) {
            return 0; // COMBINE: 当前包含新
        }
        if (high <= newHigh && low >= newLow) {
            // 新包含当前
            if (allow_top_equal == 1 && high == newHigh && low > newLow) {
                return -1; // DOWN
            } else if (allow_top_equal == -1 && low == newLow && high < newHigh) {
                return 1; // UP
            }
            return exclude_included ? 2 : 0; // INCLUDED or COMBINE
        }
        if (high > newHigh && low > newLow) {
            return -1; // DOWN
        }
        if (high < newHigh && low < newLow) {
            return 1; // UP
        }
        return 0; // COMBINE (fallback)
    }

    // try_add: 对应 Python KLine_Combiner.try_add
    // 尝试将新笔合并到当前元素中
    // 返回合并方向: 0=已合并, 1=UP, -1=DOWN, 2=INCLUDED
    int tryAdd(int biIdx, float biHigh, float biLow,
               bool exclude_included = false, int allow_top_equal = 0) {
        int dir = testCombine(biHigh, biLow, exclude_included, allow_top_equal);
        if (dir == 0) {
            // 合并
            biIdxList.push_back(biIdx);
            if (klDir == 1) { // UP
                // 取高的high和高的low
                if (biHigh != biLow || biHigh != high) {
                    if (biHigh > high) high = biHigh;
                    if (biLow > low)   low = biLow;
                }
            } else { // DOWN
                // 取低的high和低的low
                if (biHigh != biLow || biLow != low) {
                    if (biHigh < high) high = biHigh;
                    if (biLow < low)   low = biLow;
                }
            }
        }
        return dir;
    }

    // update_fx: 对应 Python KLine_Combiner.update_fx
    // 检测当前元素(中间)是否构成顶/底分型
    void updateFx(const EigenElement& pre, const EigenElement& nxt,
                  bool exclude_included, int allow_top_equal) {
        if (exclude_included) {
            if (pre.high < high && nxt.high <= high && nxt.low < low) {
                if (allow_top_equal == 1 || nxt.high < high) {
                    fx = 1; // TOP
                }
            } else if (nxt.high > high && pre.low > low && nxt.low >= low) {
                if (allow_top_equal == -1 || nxt.low > low) {
                    fx = -1; // BOTTOM
                }
            }
        } else {
            if (pre.high < high && nxt.high < high && pre.low < low && nxt.low < low) {
                fx = 1; // TOP
            } else if (pre.high > high && nxt.high > high && pre.low > low && nxt.low > low) {
                fx = -1; // BOTTOM
            }
        }
    }

    // getPeakBiIdx: 对应 Python Eigen.GetPeakBiIdx
    // biDir = biPoints[firstBiIdx].dir: +1=顶(DOWN笔), -1=底(UP笔)
    // Python: bi_dir == UP → find low peak (is_high=False)
    //         bi_dir == DOWN → find high peak (is_high=True)
    // C++ mapping: biDir==-1(底=UP笔) → find low peak, biDir==1(顶=DOWN笔) → find high peak
    int getPeakBiIdx(int biDir, const std::vector<BiPoint>& biPoints,
                     const CKLineCombiner& combiner) const {
        for (int i = (int)biIdxList.size() - 1; i >= 0; i--) {
            int bIdx = biIdxList[i];
            float bHigh, bLow;
            getBiHighLow(bIdx, biPoints, combiner, bHigh, bLow);
            if (biDir == -1) {
                // 底端点 → UP笔 → 下降线段 → 找low最小的（Python: is_high=False）
                if (bLow == low) {
                    return bIdx - 1;
                }
            } else {
                // 顶端点 → DOWN笔 → 上升线段 → 找high最大的（Python: is_high=True）
                if (bHigh == high) {
                    return bIdx - 1;
                }
            }
        }
        return biIdxList.back() - 1;
    }

    // 获取笔的 high 和 low（对应 Python bi._high() 和 bi._low()）
    // 笔i = biPoints[i] → biPoints[i+1]
    // biPoints[i].dir == -1 (底) → UP笔: high = biPoints[i+1].value (顶), low = biPoints[i].value (底)
    // biPoints[i].dir == +1 (顶) → DOWN笔: high = biPoints[i].value (顶), low = biPoints[i+1].value (底)
    static void getBiHighLow(int biIdx, const std::vector<BiPoint>& biPoints,
                             const CKLineCombiner& combiner,
                             float& outHigh, float& outLow) {
        if (biIdx < 0 || biIdx >= (int)biPoints.size()) {
            outHigh = outLow = 0;
            return;
        }
        const BiPoint& bp = biPoints[biIdx];
        if (bp.dir == -1) {
            // 底端点 → UP笔(底→顶): low = bp.value(底), high = 下一个端点(顶)
            outLow = bp.value;
            if (biIdx + 1 < (int)biPoints.size()) {
                outHigh = biPoints[biIdx + 1].value;
            } else {
                outHigh = bp.value;
            }
        } else {
            // 顶端点 → DOWN笔(顶→底): high = bp.value(顶), low = 下一个端点(底)
            outHigh = bp.value;
            if (biIdx + 1 < (int)biPoints.size()) {
                outLow = biPoints[biIdx + 1].value;
            } else {
                outLow = bp.value;
            }
        }
    }
};

// ==========================================================================
// 特征序列分型检测器（对应 Python EigenFX.py → CEigenFX）
//
// 检测由反向笔构成的特征序列中的顶/底分型
// 上升线段 → 检测下降笔构成的特征序列顶分型
// 下降线段 → 检测上升笔构成的特征序列底分型
// ==========================================================================
struct CEigenFX {
    int segDir;                    // 线段方向: +1=上升, -1=下降
    bool exclude_included;         // 是否排除包含关系
    EigenElement ele[3];           // 三个特征序列元素
    bool eleValid[3];              // 元素是否有效
    std::vector<int> biList;       // 已添加的笔索引列表
    int lastEvidenceBiIdx;         // 最后的证据笔索引

    // 引用数据（在生命周期内保持有效）
    const std::vector<BiPoint>* pBiPoints;
    const CKLineCombiner* pCombiner;

    CEigenFX() : segDir(0), exclude_included(true), lastEvidenceBiIdx(-1),
                 pBiPoints(nullptr), pCombiner(nullptr) {
        eleValid[0] = eleValid[1] = eleValid[2] = false;
    }

    void init(int dir, bool excl_incl,
              const std::vector<BiPoint>& biPoints,
              const CKLineCombiner& combiner) {
        segDir = dir;
        exclude_included = excl_incl;
        eleValid[0] = eleValid[1] = eleValid[2] = false;
        biList.clear();
        lastEvidenceBiIdx = -1;
        pBiPoints = &biPoints;
        pCombiner = &combiner;
    }

    void clear() {
        eleValid[0] = eleValid[1] = eleValid[2] = false;
        biList.clear();
    }

    bool isUp()   const { return segDir == 1; }
    bool isDown() const { return segDir == -1; }

    int getKlDir() const {
        return isUp() ? 1 : -1;   // UP线段的特征序列方向=UP
    }

    // --- 三阶段处理 ---

    bool treatFirstEle(int biIdx, float biHigh, float biLow) {
        ele[0].init(biIdx, biHigh, biLow, getKlDir());
        eleValid[0] = true;
        return false;
    }

    bool treatSecondEle(int biIdx, float biHigh, float biLow) {
        int combDir = ele[0].tryAdd(biIdx, biHigh, biLow, exclude_included, 0);
        if (combDir != 0) { // 不能合并
            ele[1].init(biIdx, biHigh, biLow, getKlDir());
            eleValid[1] = true;
            // 检查: 前两元素不可能成为分形
            if ((isUp() && ele[1].high < ele[0].high) ||
                (isDown() && ele[1].low > ele[0].low)) {
                return doReset();
            }
        }
        return false;
    }

    bool treatThirdEle(int biIdx, float biHigh, float biLow) {
        lastEvidenceBiIdx = biIdx;
        int allow_top_equal = 0;
        if (exclude_included) {
            // Python: allow_top_equal = (1 if bi.is_down() else -1)
            // bi的方向: biPoints[biIdx].dir == -1(底) → UP笔 → is_down()=False → -1
            //           biPoints[biIdx].dir == +1(顶) → DOWN笔 → is_down()=True → 1
            int biDir = (*pBiPoints)[biIdx].dir;
            allow_top_equal = (biDir == 1) ? 1 : -1;  // 顶(DOWN笔)→1, 底(UP笔)→-1
        }
        int combDir = ele[1].tryAdd(biIdx, biHigh, biLow, false, allow_top_equal);
        if (combDir == 0) {
            return false; // 被合并
        }
        ele[2].init(biIdx, biHigh, biLow, combDir);
        eleValid[2] = true;

        // actualBreak 检查
        if (!actualBreak()) {
            return doReset();
        }

        // updateFx
        ele[1].updateFx(ele[0], ele[2], exclude_included, allow_top_equal);

        // 检查分型
        bool isFx = (isUp() && ele[1].fx == 1) || (isDown() && ele[1].fx == -1);
        if (isFx) {
            return true;
        } else {
            return doReset();
        }
    }

    // add: 逐笔添加，返回是否形成分型
    // 对应 Python EigenFX.add()
    bool add(int biIdx) {
        float biHigh, biLow;
        EigenElement::getBiHighLow(biIdx, *pBiPoints, *pCombiner, biHigh, biLow);
        biList.push_back(biIdx);

        if (!eleValid[0]) {
            return treatFirstEle(biIdx, biHigh, biLow);
        } else if (!eleValid[1]) {
            return treatSecondEle(biIdx, biHigh, biLow);
        } else if (!eleValid[2]) {
            return treatThirdEle(biIdx, biHigh, biLow);
        }
        return false;
    }

    // actualBreak: 对应 Python EigenFX.actual_break
    // 防止第二元素因合并导致后面没有实际突破
    bool actualBreak() {
        if (!exclude_included) return true;
        if (!eleValid[2] || !eleValid[1]) return false;

        // 获取 ele[1] 最后一根笔的 high/low
        int lastBi1 = ele[1].lastBiIdx();
        float lastBi1High, lastBi1Low;
        EigenElement::getBiHighLow(lastBi1, *pBiPoints, *pCombiner, lastBi1High, lastBi1Low);

        if ((isUp() && ele[2].low < lastBi1Low) ||
            (isDown() && ele[2].high > lastBi1High)) {
            return true;
        }

        // 检查 ele[2] 只有一根笔且其后续笔能突破
        if (ele[2].size() != 1) return false;
        int ele2BiIdx = ele[2].firstBiIdx();

        // 需要检查 ele2_bi.next.next (即 biIdx+2)
        int nextNextIdx = ele2BiIdx + 2;
        if (nextNextIdx < (int)pBiPoints->size()) {
            float nnHigh, nnLow;
            EigenElement::getBiHighLow(nextNextIdx, *pBiPoints, *pCombiner, nnHigh, nnLow);
            float e2High, e2Low;
            EigenElement::getBiHighLow(ele2BiIdx, *pBiPoints, *pCombiner, e2High, e2Low);

            // Python: ele2_bi.is_down() / ele2_bi.is_up()
            // biPoints[ele2BiIdx].dir: -1(底)→UP笔, +1(顶)→DOWN笔
            if ((*pBiPoints)[ele2BiIdx].dir == 1 && nnLow < e2Low) {  // 顶=DOWN笔
                lastEvidenceBiIdx = nextNextIdx;
                return true;
            } else if ((*pBiPoints)[ele2BiIdx].dir == -1 && nnHigh > e2High) {  // 底=UP笔
                lastEvidenceBiIdx = nextNextIdx;
                return true;
            }
        }
        return false;
    }

    // doReset: 对应 Python EigenFX.reset
    // 从第二根笔开始重新构建
    bool doReset() {
        if (exclude_included) {
            std::vector<int> tmpList(biList.begin() + 1, biList.end());
            clear();
            for (int bIdx : tmpList) {
                if (add(bIdx)) return true;
            }
        } else {
            // ele[0] <- ele[1], ele[1] <- ele[2], ele[2] = invalid
            int ele2BeginIdx = eleValid[1] ? ele[1].firstBiIdx() : -1;
            ele[0] = ele[1];
            ele[1] = ele[2];
            eleValid[0] = eleValid[1];
            eleValid[1] = eleValid[2];
            eleValid[2] = false;
            // 过滤 biList: 只保留 idx >= ele2BeginIdx 的
            if (ele2BeginIdx >= 0) {
                std::vector<int> newList;
                for (int bIdx : biList) {
                    if (bIdx >= ele2BeginIdx) newList.push_back(bIdx);
                }
                // 不包括第一个（从原 biList[1:] 开始的)
                biList.clear();
                bool skippedFirst = false;
                for (int bIdx : newList) {
                    if (!skippedFirst) { skippedFirst = true; continue; }
                    biList.push_back(bIdx);
                }
            }
        }
        return false;
    }

    // GetPeakBiIdx: 获取线段端点的笔索引
    int GetPeakBiIdx() const {
        if (!eleValid[1]) return -1;
        int biDir = (*pBiPoints)[ele[1].firstBiIdx()].dir;
        return ele[1].getPeakBiIdx(biDir, *pBiPoints, *pCombiner);
    }

    // allBiIsSure: DLL中所有笔都视为确定
    bool allBiIsSure() const { return true; }

    // canBeEnd: 对应 Python EigenFX.can_be_end
    // gap的情况需要额外验证反向分型
    // 返回: 1=true, 0=false, -1=None(找到尾部也没找到)
    int canBeEnd(const std::vector<BiPoint>& biPoints, const CKLineCombiner& combiner) {
        if (!eleValid[1]) return 0;
        if (ele[1].gap) {
            int endBiIdx = GetPeakBiIdx();
            if (endBiIdx < 0 || endBiIdx >= (int)biPoints.size()) return -1;

            // thred_value = biPoints[endBiIdx].value (笔的end_val)
            // 但在Python中: thred_value = bi_lst[end_bi_idx].get_end_val()
            // get_end_val: up笔=end_klc.high, down笔=end_klc.low 即 biPoints[endBiIdx].value
            float thred_value = biPoints[endBiIdx].value;

            // break_thred = ele[0].low if is_up else ele[0].high
            float break_thred = isUp() ? ele[0].low : ele[0].high;

            return findRevertFx(biPoints, combiner, endBiIdx + 2, thred_value, break_thred);
        }
        return 1; // true
    }

    // findRevertFx: 对应 Python EigenFX.find_revert_fx
    // 在gap的情况下，从endBiIdx+2开始寻找反向分型
    // 返回: 1=true, 0=false, -1=None
    int findRevertFx(const std::vector<BiPoint>& biPoints,
                     const CKLineCombiner& combiner,
                     int beginIdx, float thred_value, float break_thred) {
        if (beginIdx >= (int)biPoints.size()) return -1;

        // Python: first_bi_dir = bi_list[begin_idx].dir
        // biPoints[beginIdx].dir: -1(底)→UP笔, +1(顶)→DOWN笔
        // 需要先转换为笔方向，再 revert
        int firstBiPointDir = biPoints[beginIdx].dir;
        // firstBiPointDir == -1(底) → UP笔 → revert → DOWN(-1)
        // firstBiPointDir == +1(顶) → DOWN笔 → revert → UP(+1)
        int revertDir = (firstBiPointDir == 1) ? 1 : -1;  // 顶(DOWN)→revert→UP(1), 底(UP)→revert→DOWN(-1)

        CEigenFX revertFx;
        revertFx.init(revertDir, false, biPoints, combiner); // exclude_included=false

        // Python: for bi in bi_list[begin_idx::2]:
        for (int i = beginIdx; i < (int)biPoints.size(); i += 2) {
            if (revertFx.add(i)) {
                // 找到分型
                // Python: while True: _test = egien_fx.can_be_end(bi_list); ...
                while (true) {
                    int _test = revertFx.canBeEnd(biPoints, combiner);
                    if (_test == 1 || _test == -1) {
                        lastEvidenceBiIdx = i;
                        return _test;
                    }
                    // _test == 0: false
                    if (!revertFx.doReset()) {
                        break;
                    }
                }
            }
        }
        return -1; // None: 找到尾部也没找到
    }
};

// ==========================================================================
// 线段识别器（对应 Python SegListChan.py + SegListComm.py）
//
// 主要逻辑：
// 1. cal_seg_sure: 用特征序列分型确定线段（递归→迭代）
// 2. collect_left_seg: 收集剩余虚线段
// ==========================================================================
class CSegDetector {
public:
    std::vector<SegPoint> segPoints;

    void clear() {
        segPoints.clear();
    }

    // detect: 主入口
    void detect(const std::vector<BiPoint>& biPoints,
                const CKLineCombiner& combiner) {
        clear();
        if (biPoints.size() < 4) return; // 至少需要4个笔端点才能形成线段

        cal_seg_sure(biPoints, combiner, 0);
        collect_left_seg(biPoints, combiner);
    }

private:
    // ---- 笔辅助方法 ----

    // 获取笔方向(作为线段方向): biPoints[i].dir ==  1 → 上升笔
    //                         biPoints[i].dir == -1 → 下降笔
    // 笔的方向交替出现：顶(+1)→底(-1)→顶(+1)...

    // 获取笔的结束值（对应 Python bi.get_end_val）
    float getBiEndVal(int biIdx, const std::vector<BiPoint>& biPoints) const {
        if (biIdx < 0 || biIdx >= (int)biPoints.size()) return 0;
        return biPoints[biIdx].value;
    }

    // 获取笔的开始值（对应 Python bi.get_begin_val）
    float getBiBeginVal(int biIdx, const std::vector<BiPoint>& biPoints) const {
        if (biIdx <= 0) {
            // 第一笔的begin_val需要从前一个端点获取
            // 但biPoints[0]是第一个端点，它本身就是begin
            // 在DLL中，笔是两个连续BiPoint之间的段
            // biPoints[i] 到 biPoints[i+1] 构成一笔
            // 所以笔i的begin_val = biPoints[i].value, end_val = biPoints[i+1].value
            return biPoints[0].value;
        }
        return biPoints[biIdx].value;
    }

    // 笔的方向（在DLL中，相邻BiPoint方向交替）
    // "笔i" = biPoints[i] → biPoints[i+1]
    // 笔的方向由起始端点决定:
    //   biPoints[i].dir == -1 (底) → 笔 i 是上升笔 (底→顶) → dir = UP(+1)
    //   biPoints[i].dir == +1 (顶) → 笔 i 是下降笔 (顶→底) → dir = DOWN(-1)
    int getBiDir(int biIdx, const std::vector<BiPoint>& biPoints) const {
        if (biIdx < 0 || biIdx >= (int)biPoints.size()) return 0;
        // 笔i 从 biPoints[i] 到 biPoints[i+1]
        // 方向取决于起点: 底→顶=UP, 顶→底=DOWN
        return (biPoints[biIdx].dir == -1) ? 1 : -1;  // -1(底)→UP(+1), +1(顶)→DOWN(-1)
    }

    // ---- 核心线段识别 ----

    // cal_seg_sure: 对应 Python SegListChan.cal_seg_sure
    // Python中是递归调用自身，C++中改为goto迭代
    void cal_seg_sure(const std::vector<BiPoint>& biPoints,
                      const CKLineCombiner& combiner,
                      int begin_idx) {
    restart:
        if (begin_idx >= (int)biPoints.size() - 1) return;

        // 创建两个方向的特征序列分型检测器
        CEigenFX up_eigen, down_eigen;
        up_eigen.init(1, true, biPoints, combiner);     // 上升线段：检测下降笔
        down_eigen.init(-1, true, biPoints, combiner);   // 下降线段：检测上升笔

        int last_seg_dir = segPoints.empty() ? 0 : segPoints.back().dir;

        for (int biIdx = begin_idx; biIdx < (int)biPoints.size() - 1; biIdx++) {
            // Python: for bi in bi_lst[begin_idx:]
            // 笔i 从 biPoints[biIdx] → biPoints[biIdx+1]
            int biDir = getBiDir(biIdx, biPoints);  // +1=UP, -1=DOWN

            CEigenFX* fx_eigen = nullptr;

            // Python: if bi.is_down() and last_seg_dir != BI_DIR.UP:
            //             if up_eigen.add(bi): fx_eigen = up_eigen
            // 上升线段用下降笔构成特征序列
            if (biDir == -1 && last_seg_dir != 1) {
                if (up_eigen.add(biIdx)) {
                    fx_eigen = &up_eigen;
                }
            }
            // Python: elif bi.is_up() and last_seg_dir != BI_DIR.DOWN:
            //             if down_eigen.add(bi): fx_eigen = down_eigen
            else if (biDir == 1 && last_seg_dir != -1) {
                if (down_eigen.add(biIdx)) {
                    fx_eigen = &down_eigen;
                }
            }

            // 尝试确定第一段方向（对应 Python L48-58）
            if (segPoints.empty()) {
                if (up_eigen.eleValid[1] && biDir == -1) {
                    last_seg_dir = -1; // 下降线段方向确定
                    down_eigen.clear();
                } else if (down_eigen.eleValid[1] && biDir == 1) {
                    up_eigen.clear();
                    last_seg_dir = 1;  // 上升线段方向确定
                }
                if (!up_eigen.eleValid[1] && last_seg_dir == -1 && biDir == -1) {
                    last_seg_dir = 0;
                } else if (!down_eigen.eleValid[1] && last_seg_dir == 1 && biDir == 1) {
                    last_seg_dir = 0;
                }
            }

            // 分型成立 → 处理
            if (fx_eigen) {
                // treat_fx_eigen
                int _test = fx_eigen->canBeEnd(biPoints, combiner);
                int end_bi_idx = fx_eigen->GetPeakBiIdx();

                if (_test == 1 || _test == -1) {
                    // None(=-1)表示反向分型找到尾部也没找到
                    bool is_true = (_test == 1);
                    bool is_sure = is_true && fx_eigen->allBiIsSure();

                    if (!addNewSeg(biPoints, combiner, end_bi_idx, is_sure)) {
                        // 第一根线段方向与首尾值异常
                        begin_idx = end_bi_idx + 1;
                        goto restart;
                    }
                    if (is_true) {
                        begin_idx = end_bi_idx + 1;
                        goto restart;
                    }
                } else {
                    // _test == 0: false
                    // Python: self.cal_seg_sure(bi_lst, fx_eigen.lst[1].idx)
                    if (fx_eigen->biList.size() > 1) {
                        begin_idx = fx_eigen->biList[1];
                    } else {
                        begin_idx = biIdx + 1;
                    }
                    goto restart;
                }
                return; // 处理完毕（非true情况不递归）
            }
        }
    }

    // addNewSeg: 对应 Python SegListComm.add_new_seg + try_add_new_seg
    // 创建新线段
    // 返回 false 表示线段方向异常（SEG_END_VALUE_ERR）
    bool addNewSeg(const std::vector<BiPoint>& biPoints,
                   const CKLineCombiner& combiner,
                   int endBiIdx, bool is_sure,
                   int forcedDir = 0, bool split_first_seg = true) {
        if (endBiIdx < 0 || endBiIdx >= (int)biPoints.size()) return false;

        // 确定起始笔索引
        int startBiIdx = segPoints.empty() ? 0 : segPoints.back().biIdx + 1;
        if (startBiIdx >= (int)biPoints.size()) return false;
        if (endBiIdx < startBiIdx) return false;

        // try_add_new_seg: split_first_seg 逻辑
        if (segPoints.empty() && split_first_seg && endBiIdx >= 3) {
            // 对应 Python L129-135: 寻找峰值笔分割第一段
            int peakBiIdx = -1;
            bool isHighPeak = (getBiDir(endBiIdx, biPoints) == -1);
            // Python: FindPeakBi(bi_lst[end_bi_idx-3::-1], bi_lst[end_bi_idx].is_down())
            peakBiIdx = findPeakBi(biPoints, combiner, 0, endBiIdx - 2, isHighPeak, true);
            if (peakBiIdx >= 0 && peakBiIdx < endBiIdx) {
                // 检查条件
                int peakDir = getBiDir(peakBiIdx, biPoints);
                float peakEndVal = getBiEndVal(peakBiIdx + 1, biPoints);
                float firstBiBeginVal = biPoints[0].value;
                bool shouldSplit = false;
                if (peakDir == -1) {
                    // 下降笔: low < 第一笔开头low || peakBiIdx == 0
                    float peakLow, peakHigh;
                    EigenElement::getBiHighLow(peakBiIdx, biPoints, combiner, peakHigh, peakLow);
                    float firstLow, firstHigh;
                    EigenElement::getBiHighLow(0, biPoints, combiner, firstHigh, firstLow);
                    shouldSplit = (peakLow < firstLow || peakBiIdx == 0);
                } else {
                    // 上升笔: high > 第一笔开头high || peakBiIdx == 0
                    float peakLow, peakHigh;
                    EigenElement::getBiHighLow(peakBiIdx, biPoints, combiner, peakHigh, peakLow);
                    float firstLow, firstHigh;
                    EigenElement::getBiHighLow(0, biPoints, combiner, firstHigh, firstLow);
                    shouldSplit = (peakHigh > firstHigh || peakBiIdx == 0);
                }
                if (shouldSplit) {
                    int splitDir = getBiDir(peakBiIdx, biPoints);
                    addSegPoint(biPoints, startBiIdx, peakBiIdx, false, splitDir);
                    addSegPoint(biPoints, peakBiIdx + 1, endBiIdx, false, 0);
                    return true;
                }
            }
        }

        // 确定线段方向
        int dir = forcedDir;
        if (dir == 0) {
            // 从笔的end方向推断: 对应 Python CSeg.__init__: self.dir = end_bi.dir
            // end_bi 是 biPoints[endBiIdx]到biPoints[endBiIdx+1]这一笔
            dir = getBiDir(endBiIdx, biPoints);
        }

        // 方向验证（对应 Python Seg.py check()）
        if (is_sure && endBiIdx - startBiIdx >= 2) {
            float beginVal = biPoints[startBiIdx].value;
            float endVal = (endBiIdx + 1 < (int)biPoints.size()) ?
                           biPoints[endBiIdx + 1].value : biPoints[endBiIdx].value;
            if (dir == -1 && beginVal < endVal) {
                // 下降线段但起始低于结束 → 异常
                if (segPoints.empty()) return false;
            }
            if (dir == 1 && beginVal > endVal) {
                // 上升线段但起始高于结束 → 异常
                if (segPoints.empty()) return false;
            }
        }

        // 最小长度检查
        if (endBiIdx - startBiIdx < 2) {
            is_sure = false;
        }

        addSegPoint(biPoints, startBiIdx, endBiIdx, is_sure, dir);
        return true;
    }

    // 实际添加线段端点
    void addSegPoint(const std::vector<BiPoint>& biPoints,
                     int startBiIdx, int endBiIdx, bool is_sure, int dir) {
        SegPoint sp;
        sp.biIdx = endBiIdx;
        sp.startBiIdx = startBiIdx;
        // origIdx: 线段终止笔的终点K线
        if (endBiIdx + 1 < (int)biPoints.size()) {
            sp.origIdx = biPoints[endBiIdx + 1].origIdx;
        } else {
            sp.origIdx = biPoints[endBiIdx].origIdx;
        }
        sp.dir = dir;
        sp.is_sure = is_sure;
        segPoints.push_back(sp);
    }

    // ---- 虚线段收集 ----

    // collect_left_seg: 对应 Python SegListComm.collect_left_seg
    void collect_left_seg(const std::vector<BiPoint>& biPoints,
                          const CKLineCombiner& combiner) {
        if (segPoints.empty()) {
            collect_first_seg(biPoints, combiner);
        } else {
            collect_segs(biPoints, combiner);
        }
    }

    // collect_first_seg: 对应 Python SegListComm.collect_first_seg
    void collect_first_seg(const std::vector<BiPoint>& biPoints,
                           const CKLineCombiner& combiner) {
        if (biPoints.size() < 4) return; // 至少3笔
        int numBis = (int)biPoints.size() - 1; // 笔的数量
        if (numBis < 3) return;

#if LEFT_SEG_METHOD == 1
        // ALL method: 所有笔归入一个线段
        float beginVal = biPoints[0].value;
        float endVal = biPoints.back().value;
        int _dir = (endVal >= beginVal) ? 1 : -1;
        addNewSeg(biPoints, combiner, numBis - 1, false, _dir, false);
#else
        // PEAK method: 寻找峰值
        float _high = -1e30f, _low = 1e30f;
        for (int i = 0; i < (int)biPoints.size(); i++) {
            if (biPoints[i].value > _high) _high = biPoints[i].value;
            if (biPoints[i].value < _low)  _low  = biPoints[i].value;
        }
        float beginVal = biPoints[0].value;
        if (fabsf(_high - beginVal) >= fabsf(_low - beginVal)) {
            // 找最高点
            int peakBi = findPeakBi(biPoints, combiner, 0, numBis - 1, true, false);
            if (peakBi >= 0) {
                addNewSeg(biPoints, combiner, peakBi, false, 1, false);
                collect_left_as_seg(biPoints, combiner);
            }
        } else {
            // 找最低点
            int peakBi = findPeakBi(biPoints, combiner, 0, numBis - 1, false, false);
            if (peakBi >= 0) {
                addNewSeg(biPoints, combiner, peakBi, false, -1, false);
                collect_left_as_seg(biPoints, combiner);
            }
        }
#endif
    }

    // collect_segs: 对应 Python SegListComm.collect_segs
    void collect_segs(const std::vector<BiPoint>& biPoints,
                      const CKLineCombiner& combiner) {
        int numBis = (int)biPoints.size() - 1;
        int lastSegEndBi = segPoints.back().biIdx;
        int lastBi = numBis - 1; // 最后一笔的索引

        if (lastBi - lastSegEndBi < 3) return;

        float lastSegEndVal = getBiEndVal(lastSegEndBi + 1, biPoints);
        float lastBiEndVal  = getBiEndVal(lastBi + 1 < (int)biPoints.size() ? lastBi + 1 : lastBi, biPoints);
        int lastSegEndDir = getBiDir(lastSegEndBi, biPoints);

        if (lastSegEndDir == -1 && lastBiEndVal <= lastSegEndVal) {
            // 下降笔结束且尾部更低 → 找高点
            int peakBi = findPeakBi(biPoints, combiner, lastSegEndBi + 3, lastBi, true, false);
            if (peakBi >= 0 && peakBi - lastSegEndBi >= 3) {
                addNewSeg(biPoints, combiner, peakBi, false, 1);
                collect_left_seg(biPoints, combiner);
                return;
            }
        } else if (lastSegEndDir == 1 && lastBiEndVal >= lastSegEndVal) {
            // 上升笔结束且尾部更高 → 找低点
            int peakBi = findPeakBi(biPoints, combiner, lastSegEndBi + 3, lastBi, false, false);
            if (peakBi >= 0 && peakBi - lastSegEndBi >= 3) {
                addNewSeg(biPoints, combiner, peakBi, false, -1);
                collect_left_seg(biPoints, combiner);
                return;
            }
        }

        // 剩余部分
#if LEFT_SEG_METHOD == 1
        // ALL method
        collect_left_as_seg(biPoints, combiner);
#else
        // PEAK method
        collect_left_seg_peak_method(lastSegEndBi, biPoints, combiner);
#endif
    }

    // collect_left_seg_peak_method: 对应 Python SegListComm.collect_left_seg_peak_method
    void collect_left_seg_peak_method(int lastSegEndBi,
                                      const std::vector<BiPoint>& biPoints,
                                      const CKLineCombiner& combiner) {
        int numBis = (int)biPoints.size() - 1;
        int lastSegEndDir = getBiDir(lastSegEndBi, biPoints);
        bool findNewSeg = false;

        if (lastSegEndDir == -1) {
            // 找高点
            int peakBi = findPeakBi(biPoints, combiner, lastSegEndBi + 3, numBis - 1, true, false);
            if (peakBi >= 0 && peakBi - lastSegEndBi >= 3) {
                addNewSeg(biPoints, combiner, peakBi, false, 1);
                findNewSeg = true;
            }
        } else {
            // 找低点
            int peakBi = findPeakBi(biPoints, combiner, lastSegEndBi + 3, numBis - 1, false, false);
            if (peakBi >= 0 && peakBi - lastSegEndBi >= 3) {
                addNewSeg(biPoints, combiner, peakBi, false, -1);
                findNewSeg = true;
            }
        }

        if (!findNewSeg) {
            collect_left_as_seg(biPoints, combiner);
        } else {
            int newLastEndBi = segPoints.back().biIdx;
            collect_left_seg_peak_method(newLastEndBi, biPoints, combiner);
        }
    }

    // collect_left_as_seg: 对应 Python SegListComm.collect_left_as_seg
    void collect_left_as_seg(const std::vector<BiPoint>& biPoints,
                             const CKLineCombiner& combiner) {
        if (segPoints.empty()) return;
        int numBis = (int)biPoints.size() - 1;
        int lastSegEndBi = segPoints.back().biIdx;
        int lastBi = numBis - 1;

        if (lastSegEndBi + 1 >= numBis) return;

        int lastSegEndDir = getBiDir(lastSegEndBi, biPoints);
        int lastBiDir = getBiDir(lastBi, biPoints);

        if (lastSegEndDir == lastBiDir) {
            addNewSeg(biPoints, combiner, lastBi - 1, false);
        } else {
            addNewSeg(biPoints, combiner, lastBi, false);
        }
    }

    // ---- 峰值笔搜索 ----

    // findPeakBi: 对应 Python SegListComm.FindPeakBi
    // 在 [startBiIdx, endBiIdx] 范围内找到高/低峰值笔
    // reverse: true=从后往前搜索
    int findPeakBi(const std::vector<BiPoint>& biPoints,
                   const CKLineCombiner& combiner,
                   int startBiIdx, int endBiIdx,
                   bool is_high, bool reverse) const {
        float peakVal = is_high ? -1e30f : 1e30f;
        int peakBi = -1;

        if (reverse) {
            // 从后往前
            for (int i = endBiIdx; i >= startBiIdx; i--) {
                int biDir = getBiDir(i, biPoints);
                float endVal = (i + 1 < (int)biPoints.size()) ? biPoints[i + 1].value : biPoints[i].value;

                if (is_high && endVal >= peakVal && biDir == 1) {
                    // 检查 pre.pre 条件
                    if (i >= 2) {
                        float prePre = (i - 1 < (int)biPoints.size()) ? biPoints[i - 1].value : 0;
                        if (prePre > endVal) continue;
                    }
                    peakVal = endVal;
                    peakBi = i;
                } else if (!is_high && endVal <= peakVal && biDir == -1) {
                    if (i >= 2) {
                        float prePre = (i - 1 < (int)biPoints.size()) ? biPoints[i - 1].value : 1e30f;
                        if (prePre < endVal) continue;
                    }
                    peakVal = endVal;
                    peakBi = i;
                }
            }
        } else {
            // 从前往后（正向搜索）
            for (int i = startBiIdx; i <= endBiIdx && i < (int)biPoints.size() - 1; i++) {
                int biDir = getBiDir(i, biPoints);
                float endVal = (i + 1 < (int)biPoints.size()) ? biPoints[i + 1].value : biPoints[i].value;

                if (is_high && endVal >= peakVal && biDir == 1) {
                    // Python: if bi.pre and bi.pre.pre and is_high and bi.pre.pre.get_end_val() > bi.get_end_val():
                    //             continue
                    if (i >= 2) {
                        float prePreVal = (i - 1 < (int)biPoints.size()) ? biPoints[i - 1].value : 0;
                        if (prePreVal > endVal) continue;
                    }
                    peakVal = endVal;
                    peakBi = i;
                } else if (!is_high && endVal <= peakVal && biDir == -1) {
                    if (i >= 2) {
                        float prePreVal = (i - 1 < (int)biPoints.size()) ? biPoints[i - 1].value : 1e30f;
                        if (prePreVal < endVal) continue;
                    }
                    peakVal = endVal;
                    peakBi = i;
                }
            }
        }

        return peakBi;
    }
};

#endif // __SEG_DETECTOR_H__
