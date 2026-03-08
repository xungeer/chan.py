"""
test_seg_level_debug3.py - 分析 SegSeg[2] (K[97]-K[672]) 区间内差异
DLL 在 K[209] 多出了一个线段的线段端点
"""

import ctypes
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from Chan import CChan
from ChanConfig import CChanConfig
from Common.CEnum import DATA_SRC, KL_TYPE, BSP_TYPE, BI_DIR


def load_tdx_data(code="sh.000300"):
    from DataAPI.TdxLocalAPI import CTdxLocalAPI
    CTdxLocalAPI.do_init()
    api = CTdxLocalAPI(code=code, k_type=KL_TYPE.K_DAY)
    highs, lows, closes = [], [], []
    for klu in api.get_kl_data():
        highs.append(klu.high)
        lows.append(klu.low)
        closes.append(klu.close)
    CTdxLocalAPI.do_close()
    return (
        np.array(highs, dtype=np.float32),
        np.array(lows, dtype=np.float32),
        np.array(closes, dtype=np.float32),
    )

def calc_python(code="sh.000300"):
    config_dict = {
        "bi_algo": "fx", "bi_strict": True, "bi_fx_check": "loss",
        "bi_end_is_peak": True, "bi_allow_sub_peak": True,
        "gap_as_kl": False, "zs_combine": True, "zs_combine_mode": "peak",
        "one_bi_zs": False, "zs_algo": "auto", "left_seg_method": "peak",
        "divergence_rate": float("inf"), "min_zs_cnt": 0,
        "bsp2_follow_1": False, "bsp3_follow_1": False,
        "bs1_peak": False, "macd_algo": "peak",
        "bs_type": "1,2,3a,1p,2s,3b", "trigger_step": False,
        "print_warning": True,
    }
    config = CChanConfig(config_dict)
    chan = CChan(
        code=code, data_src=DATA_SRC.TDX_LOCAL,
        lv_list=[KL_TYPE.K_DAY], config=config,
    )
    return chan

def main():
    code = "sh.000300"
    print("=" * 70)
    print("诊断: SegSeg 差异分析")
    print("=" * 70)

    high, low, close = load_tdx_data(code)
    n = len(high)

    chan = calc_python(code)
    kl = chan[0]

    print("\n=== Python 端 ===")
    
    # Python 线段级中枢
    print(f"\n  Python segzs_list:")
    for i, zs in enumerate(kl.segzs_list):
        begin_klu = zs.begin.get_begin_klu() if hasattr(zs.begin, 'get_begin_klu') else None
        end_klu = zs.end.get_end_klu() if hasattr(zs.end, 'get_end_klu') else None
        b_idx = begin_klu.idx if begin_klu else "?"
        e_idx = end_klu.idx if end_klu else "?"
        print(f"    SegZS[{i}]: K[{b_idx}]-K[{e_idx}], "
              f"[{zs.low:.2f}, {zs.high:.2f}], "
              f"peak=[{zs.peak_low:.2f}, {zs.peak_high:.2f}], "
              f"is_one_bi_zs={zs.is_one_bi_zs}")

    # Python 线段级 T3B
    PY_BSP_TYPE_MAP = {
        BSP_TYPE.T1: 'T1', BSP_TYPE.T1P: 'T1P',
        BSP_TYPE.T2: 'T2', BSP_TYPE.T2S: 'T2S',
        BSP_TYPE.T3A: 'T3A', BSP_TYPE.T3B: 'T3B',
    }
    
    print(f"\n  Python 线段级 T3B 详情:")
    for bsp in kl.seg_bs_point_lst.getSortedBspList():
        types = [PY_BSP_TYPE_MAP[t] for t in bsp.type]
        if 'T3B' in types:
            side = "买" if bsp.is_buy else "卖"
            klu_idx = bsp.klu.idx
            bi = bsp.bi
            bi_begin = bi.get_begin_klu().idx
            bi_end = bi.get_end_klu().idx
            bi_dir = "UP" if bi.dir == BI_DIR.UP else "DOWN"
            bsp1_info = ""
            if hasattr(bsp, 'relate_bsp1') and bsp.relate_bsp1:
                bsp1_idx = bsp.relate_bsp1.klu.idx
                bsp1_types = [PY_BSP_TYPE_MAP[t] for t in bsp.relate_bsp1.type]
                bsp1_info = f", relate_bsp1=K[{bsp1_idx}] {bsp1_types}"
            print(f"    K[{klu_idx}] {side}点 {types}, bi=K[{bi_begin}]-K[{bi_end}] {bi_dir}{bsp1_info}")

    # 对比前几个线段的线段
    print(f"\n  Python segseg_list:")
    for i, ss in enumerate(kl.segseg_list):
        b = ss.get_begin_klu().idx
        e = ss.get_end_klu().idx
        d = "UP" if ss.dir == BI_DIR.UP else "DOWN"
        begin_val = ss.get_begin_val()
        end_val = ss.get_end_val()
        print(f"    SegSeg[{i:2d}]: K[{b:4d}]-K[{e:4d}] dir={d:5s} begin={begin_val:.2f} end={end_val:.2f}")

    # 分析 seg_list 在 K[97]-K[209] 区间内的线段
    print(f"\n  Seg[8]-Seg[13] 区间 (K[97]-K[209]):")
    for i in range(8, 14):
        seg = kl.seg_list[i]
        b = seg.get_begin_klu().idx
        e = seg.get_end_klu().idx
        d = "UP" if seg.dir == BI_DIR.UP else "DOWN"
        print(f"    Seg[{i:2d}]: K[{b:4d}]-K[{e:4d}] dir={d:5s} begin={seg.get_begin_val():.2f} end={seg.get_end_val():.2f}")

    # Python 中对 seg 作为 "笔" 的 segAsBi 是怎么建的？
    # 在 Python 中 segseg_list 是怎么算的？
    # 查看 Python KLine_List 中相关属性
    print(f"\n  kl.seg_list 总数: {len(kl.seg_list)}")
    print(f"  kl.segseg_list 总数: {len(kl.segseg_list)}")
    print(f"  kl.segzs_list 总数: {len(list(kl.segzs_list))}")
    
    # 比较关键: 在 Python 中，SegSeg[2] = K[97]-K[672]
    # 这是从 Seg[7]终点 K[97] 到 Seg[24]起点 K[672] 附近的某个线段
    # 对应的线段（作为笔）从 Seg[7] 到 Seg[24]
    # 其中 K[209] 是 Seg[13] 的终点，向下
    # 在 Python 中，SegSeg[2] 没有在 K[209] 断开
    # 在 DLL 中，CSegDetector 把它在 K[209] 断开了
    
    # 这说明 CSegDetector 在处理 segBiPoints 时产生了不同结果
    # 关键可能在于 buildSegCombiner 的构建方式


if __name__ == "__main__":
    main()
