"""
Focused debug: compare T1 vs T1P routing for specific K-line indices
"""
import os, sys, ctypes
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from Chan import CChan
from ChanConfig import CChanConfig
from Common.CEnum import DATA_SRC, KL_TYPE, BSP_TYPE, BI_DIR

# K-line indices where Python=T1P but DLL=T1 (from analysis)
CHECK_INDICES = [20, 97, 170, 365, 527, 604, 755, 948]

def calc_python(code="sh.000300"):
    from DataAPI.TdxLocalAPI import CTdxLocalAPI
    CTdxLocalAPI.do_init()
    config_dict = {
        "bi_algo": "fx", "bi_strict": True, "bi_fx_check": "loss",
        "bi_end_is_peak": True, "bi_allow_sub_peak": True, "gap_as_kl": False,
        "zs_combine": True, "zs_combine_mode": "peak", "one_bi_zs": False,
        "zs_algo": "over_seg", "left_seg_method": "peak",
        "divergence_rate": float("inf"), "min_zs_cnt": 0,
        "bsp2_follow_1": False, "bsp3_follow_1": False, "bs1_peak": False,
        "macd_algo": "peak", "bs_type": "1,2,3a,1p,2s,3b",
        "trigger_step": False, "print_warning": True,
    }
    config = CChanConfig(config_dict)
    chan = CChan(code=code, data_src=DATA_SRC.TDX_LOCAL, lv_list=[KL_TYPE.K_DAY], config=config)
    kl = chan[0]
    CTdxLocalAPI.do_close()

    # For each segment, dump the T1/T1P routing decision info
    for seg in kl.seg_list:
        end_klu_idx = seg.get_end_klu().idx
        if end_klu_idx not in CHECK_INDICES:
            continue

        print(f"\n=== Python Seg#{seg.idx} end_klu={end_klu_idx} end_bi={seg.end_bi.idx} dir={'DOWN' if seg.is_down() else 'UP'} ===")
        print(f"  zs_lst count: {len(seg.zs_lst)}")
        if len(seg.zs_lst) > 0:
            last_zs = seg.zs_lst[-1]
            print(f"  last_zs: begin_bi={last_zs.begin_bi.idx}, end_bi={last_zs.end_bi.idx}")
            print(f"  last_zs.is_one_bi_zs(): {last_zs.is_one_bi_zs()}")
            print(f"  last_zs.bi_out: {last_zs.bi_out.idx if last_zs.bi_out else None}")
            print(f"  last_zs.bi_lst[-1].idx: {last_zs.bi_lst[-1].idx}")
            print(f"  last_zs.get_bi_in().idx: {last_zs.get_bi_in().idx}")

            cond_1 = not last_zs.is_one_bi_zs()
            cond_out = ((last_zs.bi_out and last_zs.bi_out.idx >= seg.end_bi.idx) or
                       last_zs.bi_lst[-1].idx >= seg.end_bi.idx)
            cond_depth = seg.end_bi.idx - last_zs.get_bi_in().idx > 2

            go_bsp1 = cond_1 and cond_out and cond_depth
            print(f"  cond_not_one_bi: {cond_1}")
            print(f"  cond_out: {cond_out}")
            print(f"  cond_depth: {cond_depth} (end={seg.end_bi.idx}, bi_in={last_zs.get_bi_in().idx}, diff={seg.end_bi.idx - last_zs.get_bi_in().idx})")
            print(f"  → go_bsp1 = {go_bsp1} ({'T1' if go_bsp1 else 'T1P'})")

        # Also print all zs
        for zi, zs in enumerate(seg.zs_lst):
            print(f"  zs[{zi}]: begin_bi={zs.begin_bi.idx} end_bi={zs.end_bi.idx} one_bi={zs.is_one_bi_zs()}")

calc_python()
