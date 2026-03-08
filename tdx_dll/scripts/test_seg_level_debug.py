"""
test_seg_level_debug.py - 线段级计算差异诊断
分析 DLL 和 Python 在线段的线段、线段级中枢、线段级买卖点上的差异根因
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


def load_dll(dll_path):
    dll = ctypes.CDLL(dll_path)
    FUNC_TYPE = ctypes.CFUNCTYPE(
        None, ctypes.c_int,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
    )
    class PluginTCalcFuncInfo(ctypes.Structure):
        _pack_ = 1
        _fields_ = [("nFuncMark", ctypes.c_ushort), ("pCallFunc", ctypes.c_void_p)]
    RegisterTdxFunc = dll.RegisterTdxFunc
    RegisterTdxFunc.argtypes = [ctypes.POINTER(ctypes.POINTER(PluginTCalcFuncInfo))]
    RegisterTdxFunc.restype = ctypes.c_bool
    pInfo = ctypes.POINTER(PluginTCalcFuncInfo)()
    RegisterTdxFunc(ctypes.byref(pInfo))
    funcs = {}
    i = 0
    while True:
        entry = pInfo[i]
        if entry.nFuncMark == 0:
            break
        funcs[entry.nFuncMark] = FUNC_TYPE(entry.pCallFunc)
        i += 1
    return funcs


def main():
    code = "sh.000300"
    dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "chan2026-full.dll")

    print("=" * 70)
    print("线段级计算差异诊断")
    print("=" * 70)

    # Load data
    print("\n[1] 加载数据...")
    high, low, close = load_tdx_data(code)
    n = len(high)
    print(f"  K线数: {n}")

    # Python
    print("\n[2] Python 计算...")
    chan = calc_python(code)
    kl = chan[0]

    # Python: 线段端点（原始K线索引）
    py_seg_list = []
    for seg in kl.seg_list:
        begin_idx = seg.get_begin_klu().idx
        end_idx = seg.get_end_klu().idx
        dir_val = 1 if seg.dir == BI_DIR.UP else -1
        py_seg_list.append((begin_idx, end_idx, dir_val))

    # Python: 线段的线段端点
    py_segseg_list = []
    if hasattr(kl, 'segseg_list'):
        for ss in kl.segseg_list:
            begin_idx = ss.get_begin_klu().idx
            end_idx = ss.get_end_klu().idx
            dir_val = 1 if ss.dir == BI_DIR.UP else -1
            py_segseg_list.append((begin_idx, end_idx, dir_val))

    # Python: 线段级中枢
    py_seg_zs_list = []
    if hasattr(kl, 'seg_zs_lst') and kl.seg_zs_lst is not None:
        for zs in kl.seg_zs_lst:
            py_seg_zs_list.append({
                'begin': zs.begin.idx if hasattr(zs.begin, 'idx') else str(zs.begin),
                'end': zs.end.idx if hasattr(zs.end, 'idx') else str(zs.end),
                'low': zs.low,
                'high': zs.high,
                'peak_low': zs.peak_low,
                'peak_high': zs.peak_high,
            })

    print(f"  线段数: {len(py_seg_list)}")
    print(f"  线段的线段数: {len(py_segseg_list)}")
    print(f"  线段级中枢数: {len(py_seg_zs_list)}")

    # Print python seg list  
    print(f"\n  Python 线段列表 (前30个):")
    for i, (b, e, d) in enumerate(py_seg_list[:30]):
        print(f"    Seg[{i}]: K[{b}]-K[{e}] dir={'UP' if d==1 else 'DOWN'}")

    # Print python segseg list
    print(f"\n  Python 线段的线段列表:")
    for i, (b, e, d) in enumerate(py_segseg_list):
        print(f"    SegSeg[{i}]: K[{b}]-K[{e}] dir={'UP' if d==1 else 'DOWN'}")

    # Print python seg zs
    print(f"\n  Python 线段级中枢列表:")
    for i, zs in enumerate(py_seg_zs_list):
        print(f"    SegZS[{i}]: begin={zs['begin']}, end={zs['end']}, "
              f"[{zs['low']:.2f}, {zs['high']:.2f}], "
              f"peak=[{zs['peak_low']:.2f}, {zs['peak_high']:.2f}]")

    # Python: 线段级买卖点
    PY_BSP_TYPE_MAP = {
        BSP_TYPE.T1: 'T1', BSP_TYPE.T1P: 'T1P',
        BSP_TYPE.T2: 'T2', BSP_TYPE.T2S: 'T2S',
        BSP_TYPE.T3A: 'T3A', BSP_TYPE.T3B: 'T3B',
    }
    py_seg_bsp = []
    for bsp in kl.seg_bs_point_lst.getSortedBspList():
        klu_idx = bsp.klu.idx
        is_buy = bsp.is_buy
        types = [PY_BSP_TYPE_MAP[t] for t in bsp.type]
        py_seg_bsp.append((klu_idx, is_buy, types))

    print(f"\n  Python 线段级买卖点:")
    for idx, is_buy, types in py_seg_bsp:
        side = "买" if is_buy else "卖"
        print(f"    K[{idx}] {side}点: {','.join(types)}")

    # DLL
    print("\n[3] DLL 计算...")
    funcs = load_dll(dll_path)
    c_arr = ctypes.c_float * n

    pHigh = c_arr(*high.tolist())
    pLow = c_arr(*low.tolist())
    pClose = c_arr(*close.tolist())

    pBi = c_arr(*[0.0] * n)
    funcs[1](n, pBi, pHigh, pLow, pClose)

    # Func13: 线段的线段
    pSegSeg = c_arr(*[0.0] * n)
    if 13 in funcs:
        funcs[13](n, pSegSeg, pBi, pHigh, pLow)

    dll_segseg = {}
    for i in range(n):
        v = pSegSeg[i]
        if v != 0:
            dll_segseg[i] = 1 if v > 0 else -1

    print(f"\n  DLL 线段的线段端点:")
    for idx in sorted(dll_segseg.keys()):
        d = dll_segseg[idx]
        print(f"    K[{idx}] dir={'UP' if d==1 else 'DOWN'}")

    # Func12: 线段级买卖点
    SEG_BSP_CODE_MAP = {
        21.0: ('buy', 'T1'), 21.5: ('buy', 'T1P'),
        22.0: ('buy', 'T2'), 22.5: ('buy', 'T2S'),
        23.0: ('buy', 'T3A'), 23.5: ('buy', 'T3B'),
        31.0: ('sell', 'T1'), 31.5: ('sell', 'T1P'),
        32.0: ('sell', 'T2'), 32.5: ('sell', 'T2S'),
        33.0: ('sell', 'T3A'), 33.5: ('sell', 'T3B'),
    }
    pSegBsp = c_arr(*[0.0] * n)
    if 12 in funcs:
        funcs[12](n, pSegBsp, pBi, pHigh, pLow)

    dll_seg_bsp = []
    for i in range(n):
        v = pSegBsp[i]
        if v != 0:
            if v in SEG_BSP_CODE_MAP:
                side, typ = SEG_BSP_CODE_MAP[v]
                is_buy = (side == 'buy')
                dll_seg_bsp.append((i, is_buy, typ))
            else:
                dll_seg_bsp.append((i, None, f"unknown({v})"))

    print(f"\n  DLL 线段级买卖点(Func12):")
    for idx, is_buy, t in dll_seg_bsp:
        side = "买" if is_buy else "卖"
        print(f"    K[{idx}] {side}点: {t}")

    # 对比线段的线段
    print("\n[4] 对比线段的线段...")
    py_segseg_end_dict = {}
    for b, e, d in py_segseg_list:
        py_segseg_end_dict[e] = d

    py_keys = set(py_segseg_end_dict.keys())
    dll_keys = set(dll_segseg.keys())
    common = sorted(py_keys & dll_keys)
    only_py = sorted(py_keys - dll_keys)
    only_dll = sorted(dll_keys - py_keys)

    print(f"  匹配={len(common)}, 仅Python={len(only_py)}, 仅DLL={len(only_dll)}")
    if only_py:
        print(f"  仅Python: {only_py}")
    if only_dll:
        print(f"  仅DLL: {only_dll}")
    for idx in common:
        if py_segseg_end_dict[idx] != dll_segseg[idx]:
            print(f"  方向不一致: K[{idx}] py={py_segseg_end_dict[idx]}, dll={dll_segseg[idx]}")


if __name__ == "__main__":
    main()
