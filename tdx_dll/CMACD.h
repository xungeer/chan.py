/*****************************************************************************
 * chan2026 - MACD 指标计算
 * 移植自 chan.py: Math/MACD.py + Bi/Bi.py cal_macd_metric
 *
 * EMA 递推式完全对应 Python CMACD.add():
 *   fast_ema = (2*close + (fast-1)*prev_fast_ema) / (fast+1)
 *   slow_ema = (2*close + (slow-1)*prev_slow_ema) / (slow+1)
 *   DIF = fast_ema - slow_ema
 *   DEA = (2*DIF + (signal-1)*prev_DEA) / (signal+1)
 *   MACD = 2*(DIF - DEA)
 *
 * 配置参数来自 ChanConfig.h:
 *   - CFG_MACD_ALGO: 背驰判断使用的算法模式
 *****************************************************************************/

#ifndef __CMACD_H__
#define __CMACD_H__

#include <vector>
#include <cmath>

// MACD 单项数据（对应 Python CMACD_item）
struct CMACDItem {
    float fast_ema;
    float slow_ema;
    float DIF;
    float DEA;
    float macd;
};

// MACD 计算器（对应 Python CMACD 类）
class CMACD {
public:
    int fastperiod;
    int slowperiod;
    int signalperiod;
    std::vector<CMACDItem> items;

    CMACD(int fast = 12, int slow = 26, int signal = 9)
        : fastperiod(fast), slowperiod(slow), signalperiod(signal) {}

    void clear() {
        items.clear();
    }

    // 逐K线递推计算（对应 Python CMACD.add）
    // 与 Python 完全一致：首根K线 fast_ema=slow_ema=close, DIF=DEA=0
    void add(float close) {
        CMACDItem item;
        if (items.empty()) {
            item.fast_ema = close;
            item.slow_ema = close;
            item.DIF = 0.0f;
            item.DEA = 0.0f;
            item.macd = 0.0f;
        } else {
            const CMACDItem& prev = items.back();
            item.fast_ema = (2.0f * close + (fastperiod - 1) * prev.fast_ema) / (fastperiod + 1);
            item.slow_ema = (2.0f * close + (slowperiod - 1) * prev.slow_ema) / (slowperiod + 1);
            item.DIF = item.fast_ema - item.slow_ema;
            item.DEA = (2.0f * item.DIF + (signalperiod - 1) * prev.DEA) / (signalperiod + 1);
            item.macd = 2.0f * (item.DIF - item.DEA);
        }
        items.push_back(item);
    }

    int size() const { return (int)items.size(); }

    const CMACDItem& operator[](int idx) const { return items[idx]; }

    // =========================================================================
    // 区间 MACD 指标：用于笔的背驰判断
    // 对应 Python Bi.py 的 Cal_MACD_area / Cal_MACD_peak / Cal_MACD_half
    // =========================================================================

    // 区间 MACD 面积（对应 Cal_MACD_area）
    // 累加笔方向同侧的 |macd| 值
    // isDown: true=下降笔(累加负柱), false=上升笔(累加正柱)
    float cal_area(int startIdx, int endIdx, bool isDown) const {
        float s = 1e-7f;
        for (int i = startIdx; i <= endIdx && i < (int)items.size(); i++) {
            float m = items[i].macd;
            if ((isDown && m < 0) || (!isDown && m > 0)) {
                s += fabsf(m);
            }
        }
        return s;
    }

    // 区间 MACD 峰值（对应 Cal_MACD_peak）
    // 取笔方向同侧的 |macd| 最大值
    float cal_peak(int startIdx, int endIdx, bool isDown) const {
        float peak = 1e-7f;
        for (int i = startIdx; i <= endIdx && i < (int)items.size(); i++) {
            float m = items[i].macd;
            float am = fabsf(m);
            if (am > peak) {
                if ((isDown && m < 0) || (!isDown && m > 0)) {
                    peak = am;
                }
            }
        }
        return peak;
    }

    // 半面积-正向（对应 Cal_MACD_half_obverse）
    // 从笔起点开始，累加与起点同号的 macd 值，遇到变号停止
    float cal_half_obverse(int startIdx, int endIdx) const {
        float s = 1e-7f;
        if (startIdx < 0 || startIdx >= (int)items.size()) return s;
        float peakMacd = items[startIdx].macd;
        for (int i = startIdx; i <= endIdx && i < (int)items.size(); i++) {
            if (items[i].macd * peakMacd > 0) {
                s += fabsf(items[i].macd);
            } else {
                break;
            }
        }
        return s;
    }

    // 半面积-反向（对应 Cal_MACD_half_reverse）
    // 从笔终点开始，向起点方向累加与终点同号的 macd 值
    float cal_half_reverse(int startIdx, int endIdx) const {
        float s = 1e-7f;
        if (endIdx < 0 || endIdx >= (int)items.size()) return s;
        float peakMacd = items[endIdx].macd;
        for (int i = endIdx; i >= startIdx; i--) {
            if (items[i].macd * peakMacd > 0) {
                s += fabsf(items[i].macd);
            } else {
                break;
            }
        }
        return s;
    }

    // MACD diff（对应 Cal_MACD_diff）
    // 区间内最大最小 MACD 差值
    float cal_diff(int startIdx, int endIdx) const {
        float maxVal = -1e30f, minVal = 1e30f;
        for (int i = startIdx; i <= endIdx && i < (int)items.size(); i++) {
            float m = items[i].macd;
            if (m > maxVal) maxVal = m;
            if (m < minVal) minVal = m;
        }
        return maxVal - minVal;
    }

    // 综合背驰指标计算（对应 Bi.cal_macd_metric）
    // 根据 CFG_MACD_ALGO 配置选择算法
    // isDown: 笔方向, isReverse: 半面积方向
    float cal_metric(int startIdx, int endIdx, bool isDown, bool isReverse) const {
#if CFG_MACD_ALGO == 0
        // peak 模式
        return cal_peak(startIdx, endIdx, isDown);
#elif CFG_MACD_ALGO == 1
        // area (半面积) 模式
        if (isReverse) {
            return cal_half_reverse(startIdx, endIdx);
        } else {
            return cal_half_obverse(startIdx, endIdx);
        }
#elif CFG_MACD_ALGO == 2
        // full_area 模式
        return cal_area(startIdx, endIdx, isDown);
#elif CFG_MACD_ALGO == 3
        // diff 模式
        return cal_diff(startIdx, endIdx);
#else
        // 默认 peak
        return cal_peak(startIdx, endIdx, isDown);
#endif
    }
};

#endif // __CMACD_H__
