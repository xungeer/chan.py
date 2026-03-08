"""
test_seg_level_debug2.py - 分析前30个线段的 segBiPoints 和线段的线段的差异
重点分析 K[209] 差异
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
    print("诊断: 早期线段列表和线段的线段 (K0~K700)")
    print("=" * 70)

    # Load data  
    high, low, close = load_tdx_data(code)
    n = len(high)

    # Python
    chan = calc_python(code)
    kl = chan[0]

    print("\n=== Python 端 ===")
    
    # Python 线段列表 -> "笔"（segAsBi）
    # 对应 Python cal_seg_and_zs() 中第二层的 seg_list（作为笔来处理）
    print("\n  Python seg_list (前30个):")
    for i, seg in enumerate(kl.seg_list[:30]):
        b = seg.get_begin_klu().idx
        e = seg.get_end_klu().idx
        d = "UP" if seg.dir == BI_DIR.UP else "DOWN"
        # 打印线段的 begin_val 和 end_val
        begin_val = seg.get_begin_val()
        end_val = seg.get_end_val()
        print(f"    Seg[{i:2d}]: K[{b:4d}]-K[{e:4d}] dir={d:5s} begin={begin_val:.2f} end={end_val:.2f}")

    # Python 线段的线段
    print(f"\n  Python segseg_list (前10个):")
    for i, ss in enumerate(kl.segseg_list[:10]):
        b = ss.get_begin_klu().idx
        e = ss.get_end_klu().idx
        d = "UP" if ss.dir == BI_DIR.UP else "DOWN"
        begin_val = ss.get_begin_val()
        end_val = ss.get_end_val()
        print(f"    SegSeg[{i:2d}]: K[{b:4d}]-K[{e:4d}] dir={d:5s} begin={begin_val:.2f} end={end_val:.2f}")
    
    # 查看 Python 的 seg_zs_lst (线段级中枢)
    print(f"\n  Python seg_zs_lst (前10个):")
    for i, zs in enumerate(list(kl.seg_zs_lst)[:10]):
        begin_bi = zs.begin
        end_bi = zs.end
        print(f"    SegZS[{i}]: begin={begin_bi}, end={end_bi}, "
              f"[{zs.low:.2f}, {zs.high:.2f}], "
              f"peak=[{zs.peak_low:.2f}, {zs.peak_high:.2f}], "
              f"bi_in={zs.bi_in}, bi_out={zs.bi_out}, "
              f"is_one_bi_zs={zs.is_one_bi_zs}")
    
    # 查看 Python segseg_list 的检测逻辑
    # 线段的线段 其实就是对 seg_list 进行 "笔→线段" 的再次检测
    # 关键：Python 中 seg 对应有没有 segseg 的区分

    # 分析T3B差异
    # Python 有5个T3B，DLL都没有
    # T3B 是 "中枢在一类买卖点之前" 的三类买卖点
    # 首先看 Python 端的 T3B 详情
    print(f"\n  Python 线段级 T3B 买卖点:")
    PY_BSP_TYPE_MAP = {
        BSP_TYPE.T1: 'T1', BSP_TYPE.T1P: 'T1P',
        BSP_TYPE.T2: 'T2', BSP_TYPE.T2S: 'T2S',
        BSP_TYPE.T3A: 'T3A', BSP_TYPE.T3B: 'T3B',
    }
    for bsp in kl.seg_bs_point_lst.getSortedBspList():
        types = [PY_BSP_TYPE_MAP[t] for t in bsp.type]
        if 'T3B' in types:
            side = "买" if bsp.is_buy else "卖"
            klu_idx = bsp.klu.idx
            # 获取关联的 BSP1
            if hasattr(bsp, 'relate_bsp1') and bsp.relate_bsp1:
                bsp1_idx = bsp.relate_bsp1.klu.idx
                bsp1_types = [PY_BSP_TYPE_MAP[t] for t in bsp.relate_bsp1.type]
                print(f"    K[{klu_idx}] {side}点 types={types} relate_bsp1=K[{bsp1_idx}] {bsp1_types}")
            else:
                print(f"    K[{klu_idx}] {side}点 types={types} (no relate_bsp1)")


if __name__ == "__main__":
    main()
