"""
test_auto_zs.py - 测试 ZS_ALGO=auto 时 DLL 与 Python 的差异
先用 Python zs_algo="auto" 计算中枢和买卖点，然后与 DLL(当前 over_seg) 对比
"""
import ctypes
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from Chan import CChan
from ChanConfig import CChanConfig
from Common.CEnum import DATA_SRC, KL_TYPE, BSP_TYPE, BI_DIR


BSP_CODE_MAP = {
    1.0:  ('buy',  'T1'),
    1.5:  ('buy',  'T1P'),
    2.0:  ('buy',  'T2'),
    2.5:  ('buy',  'T2S'),
    3.0:  ('buy',  'T3A'),
    3.5:  ('buy',  'T3B'),
    11.0: ('sell', 'T1'),
    11.5: ('sell', 'T1P'),
    12.0: ('sell', 'T2'),
    12.5: ('sell', 'T2S'),
    13.0: ('sell', 'T3A'),
    13.5: ('sell', 'T3B'),
}

PY_BSP_TYPE_MAP = {
    BSP_TYPE.T1:  'T1',
    BSP_TYPE.T1P: 'T1P',
    BSP_TYPE.T2:  'T2',
    BSP_TYPE.T2S: 'T2S',
    BSP_TYPE.T3A: 'T3A',
    BSP_TYPE.T3B: 'T3B',
}


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


def calc_python(code, zs_algo):
    """用 Python CChan 计算，支持 zs_algo 参数"""
    config_dict = {
        "bi_algo": "fx",
        "bi_strict": True,
        "bi_fx_check": "loss",
        "bi_end_is_peak": True,
        "bi_allow_sub_peak": True,
        "gap_as_kl": False,
        "zs_combine": True,
        "zs_combine_mode": "peak",
        "one_bi_zs": False,
        "zs_algo": zs_algo,
        "left_seg_method": "peak",
        "divergence_rate": float("inf"),
        "min_zs_cnt": 0,
        "bsp2_follow_1": False,
        "bsp3_follow_1": False,
        "bs1_peak": False,
        "macd_algo": "peak",
        "bs_type": "1,2,3a,1p,2s,3b",
        "trigger_step": False,
        "print_warning": True,
    }
    config = CChanConfig(config_dict)
    chan = CChan(
        code=code,
        data_src=DATA_SRC.TDX_LOCAL,
        lv_list=[KL_TYPE.K_DAY],
        config=config,
    )
    kl = chan[0]

    # 提取中枢
    py_zs_list = []
    for zs in kl.zs_list:
        py_zs_list.append({
            'begin_bi_idx': zs.begin_bi.idx,
            'end_bi_idx': zs.end_bi.idx,
            'high': zs.high,
            'low': zs.low,
            'is_one_bi': zs.is_one_bi_zs(),
        })

    # 提取买卖点
    py_bsp_list = []
    for bsp in kl.bs_point_lst.getSortedBspList():
        klu_idx = bsp.klu.idx
        is_buy = bsp.is_buy
        types = [PY_BSP_TYPE_MAP[t] for t in bsp.type]
        py_bsp_list.append((klu_idx, is_buy, types))

    return py_zs_list, py_bsp_list


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
        _fields_ = [
            ("nFuncMark", ctypes.c_ushort),
            ("pCallFunc", ctypes.c_void_p),
        ]
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


def calc_dll_bsp(high, low, close, dll_path):
    funcs = load_dll(dll_path)
    n = len(high)
    c_arr = ctypes.c_float * n
    pHigh = c_arr(*high.tolist())
    pLow = c_arr(*low.tolist())
    pClose = c_arr(*close.tolist())
    pBi = c_arr(*[0.0] * n)
    funcs[1](n, pBi, pHigh, pLow, pClose)
    pBspAll = c_arr(*[0.0] * n)
    funcs[11](n, pBspAll, pBi, pHigh, pLow)
    dll_bsp_list = []
    bsp_count = int(pBspAll[0])
    for i in range(bsp_count):
        orig_idx = int(pBspAll[1 + 2*i])
        code = pBspAll[2 + 2*i]
        if code in BSP_CODE_MAP:
            side, typ = BSP_CODE_MAP[code]
            is_buy = (side == 'buy')
            dll_bsp_list.append((orig_idx, is_buy, typ))
        else:
            dll_bsp_list.append((orig_idx, None, f"unknown({code})"))
    return dll_bsp_list


def _expand_bsp(bsp_list):
    expanded = {}
    for item in bsp_list:
        idx, is_buy, types_or_t = item
        if isinstance(types_or_t, list):
            for t in types_or_t:
                expanded[(idx, is_buy, t)] = True
        else:
            expanded[(idx, is_buy, types_or_t)] = True
    return set(expanded.keys())


def compare_bsp(bsp_a, bsp_b):
    a_keys = _expand_bsp(bsp_a)
    b_keys = _expand_bsp(bsp_b)
    only_a = sorted(a_keys - b_keys)
    only_b = sorted(b_keys - a_keys)
    matched = sorted(a_keys & b_keys)
    return only_a, only_b, matched


def main():
    code = "sh.000300"
    dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "chan2026-full.dll")

    if not os.path.exists(dll_path):
        print(f"ERROR: DLL not found at {dll_path}")
        return 1

    print("=" * 65)
    print("ZS_ALGO 差异分析: auto vs over_seg vs DLL")
    print("=" * 65)

    # 加载数据
    print(f"\n[0] 加载 {code} 日线数据...")
    high, low, close = load_tdx_data(code)
    print(f"  K线数: {len(high)}")

    # Python: auto
    print("\n[1] Python zs_algo=auto 计算...")
    py_auto_zs, py_auto_bsp = calc_python(code, "auto")
    print(f"  中枢数: {len(py_auto_zs)}, 买卖点: {len(py_auto_bsp)}")

    # Python: over_seg
    print("\n[2] Python zs_algo=over_seg 计算...")
    py_overseg_zs, py_overseg_bsp = calc_python(code, "over_seg")
    print(f"  中枢数: {len(py_overseg_zs)}, 买卖点: {len(py_overseg_bsp)}")

    # DLL
    print("\n[3] DLL 计算 (当前 ZS_ALGO=over_seg)...")
    dll_bsp = calc_dll_bsp(high, low, close, dll_path)
    print(f"  买卖点: {len(dll_bsp)}")

    # 对比1: auto vs over_seg (Python)
    print("\n" + "─" * 65)
    print("对比1: Python auto vs Python over_seg")
    print("─" * 65)

    # 中枢对比
    print(f"\n  中枢数: auto={len(py_auto_zs)}, over_seg={len(py_overseg_zs)}")
    if len(py_auto_zs) != len(py_overseg_zs):
        # 找差异
        auto_ids = set((z['begin_bi_idx'], z['end_bi_idx']) for z in py_auto_zs)
        overseg_ids = set((z['begin_bi_idx'], z['end_bi_idx']) for z in py_overseg_zs)
        only_auto = sorted(auto_ids - overseg_ids)
        only_overseg = sorted(overseg_ids - auto_ids)
        if only_auto:
            print(f"  仅 auto 的中枢 (begin,end): {only_auto[:10]}{'...' if len(only_auto)>10 else ''}")
        if only_overseg:
            print(f"  仅 over_seg 的中枢 (begin,end): {only_overseg[:10]}{'...' if len(only_overseg)>10 else ''}")
    else:
        # 逐个对比
        diff_count = 0
        for a, b in zip(py_auto_zs, py_overseg_zs):
            if a['begin_bi_idx'] != b['begin_bi_idx'] or a['end_bi_idx'] != b['end_bi_idx']:
                diff_count += 1
        if diff_count == 0:
            print(f"  ✅ 中枢完全一致 ({len(py_auto_zs)} 个)")
        else:
            print(f"  ⚠️ 中枢数量相同但有 {diff_count} 个位置不同")

    # BSP对比
    only_a, only_o, matched_ao = compare_bsp(py_auto_bsp, py_overseg_bsp)
    if not only_a and not only_o:
        print(f"  ✅ 买卖点完全一致 ({len(matched_ao)} 个)")
    else:
        print(f"  ⚠️ 买卖点差异: 匹配={len(matched_ao)}, 仅auto={len(only_a)}, 仅over_seg={len(only_o)}")
        if only_a:
            print("  仅 auto:")
            for idx, is_buy, t in only_a[:10]:
                side = "买" if is_buy else "卖"
                print(f"    K线[{idx}] {side}点 {t}")
        if only_o:
            print("  仅 over_seg:")
            for idx, is_buy, t in only_o[:10]:
                side = "买" if is_buy else "卖"
                print(f"    K线[{idx}] {side}点 {t}")

    # 对比2: auto(Python) vs DLL
    print("\n" + "─" * 65)
    print("对比2: Python auto vs DLL (当前 over_seg)")
    print("─" * 65)

    only_py, only_dll, matched = compare_bsp(py_auto_bsp, dll_bsp)
    if not only_py and not only_dll:
        print(f"  ✅ 买卖点 100% 一致 ({len(matched)} 个)")
    else:
        print(f"  ⚠️ 买卖点差异: 匹配={len(matched)}, 仅Python(auto)={len(only_py)}, 仅DLL={len(only_dll)}")
        if only_py:
            print("  仅 Python(auto):")
            for idx, is_buy, t in only_py[:15]:
                side = "买" if is_buy else "卖"
                print(f"    K线[{idx}] {side}点 {t}")
        if only_dll:
            print("  仅 DLL:")
            for idx, is_buy, t in only_dll[:15]:
                side = "买" if is_buy else "卖"
                print(f"    K线[{idx}] {side}点 {t}")

    # 对比3: over_seg(Python) vs DLL (已知差异参考)
    print("\n" + "─" * 65)
    print("对比3: Python over_seg vs DLL")
    print("─" * 65)
    only_py2, only_dll2, matched2 = compare_bsp(py_overseg_bsp, dll_bsp)
    if not only_py2 and not only_dll2:
        print(f"  ✅ 买卖点 100% 一致 ({len(matched2)} 个)")
    else:
        print(f"  ⚠️ 买卖点差异: 匹配={len(matched2)}, 仅Python(over_seg)={len(only_py2)}, 仅DLL={len(only_dll2)}")

    print("\n" + "=" * 65)
    return 0


if __name__ == "__main__":
    sys.exit(main())
