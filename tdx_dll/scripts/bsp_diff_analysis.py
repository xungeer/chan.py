"""
分析 BSP 差异的详细分类脚本
"""
import ctypes
import os
import sys
import numpy as np
from collections import Counter

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

    py_bsp_list = []
    for bsp in kl.bs_point_lst.getSortedBspList():
        klu_idx = bsp.klu.idx
        is_buy = bsp.is_buy
        types = [PY_BSP_TYPE_MAP[t] for t in bsp.type]
        py_bsp_list.append((klu_idx, is_buy, types))
    CTdxLocalAPI.do_close()
    return py_bsp_list

def calc_dll(code="sh.000300"):
    from DataAPI.TdxLocalAPI import CTdxLocalAPI
    CTdxLocalAPI.do_init()
    api = CTdxLocalAPI(code=code, k_type=KL_TYPE.K_DAY)
    highs, lows, closes = [], [], []
    for klu in api.get_kl_data():
        highs.append(klu.high)
        lows.append(klu.low)
        closes.append(klu.close)
    CTdxLocalAPI.do_close()
    high = np.array(highs, dtype=np.float32)
    low = np.array(lows, dtype=np.float32)
    close = np.array(closes, dtype=np.float32)

    dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "chan2026-full.dll")
    dll = ctypes.CDLL(dll_path)
    FUNC_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.POINTER(ctypes.c_float),
                                  ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
                                  ctypes.POINTER(ctypes.c_float))
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
        if entry.nFuncMark == 0: break
        funcs[entry.nFuncMark] = FUNC_TYPE(entry.pCallFunc)
        i += 1

    n = len(high)
    c_arr = ctypes.c_float * n
    pHigh = c_arr(*high.tolist())
    pLow = c_arr(*low.tolist())
    pClose = c_arr(*close.tolist())
    pBi = c_arr(*[0.0]*n)
    funcs[1](n, pBi, pHigh, pLow, pClose)
    pBspAll = c_arr(*[0.0]*n)
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
    return dll_bsp_list

# Run
py_bsp = calc_python()
dll_bsp = calc_dll()

# Expand Python multi-type
py_expanded = {}
for idx, is_buy, types in py_bsp:
    for t in types:
        key = (idx, is_buy, t)
        py_expanded[key] = True

dll_expanded = {}
for idx, is_buy, t in dll_bsp:
    key = (idx, is_buy, t)
    dll_expanded[key] = True

py_keys = set(py_expanded.keys())
dll_keys = set(dll_expanded.keys())

only_py = sorted(py_keys - dll_keys)
only_dll = sorted(dll_keys - py_keys)
matched = sorted(py_keys & dll_keys)

# Categorize differences
print(f"=== 匹配: {len(matched)}, 仅Python: {len(only_py)}, 仅DLL: {len(only_dll)} ===\n")

# 1. Same position, different type (type conflicts)
same_pos_diff = []
for idx, is_buy, t in only_py:
    for idx2, is_buy2, t2 in only_dll:
        if idx == idx2 and is_buy == is_buy2:
            same_pos_diff.append((idx, is_buy, t, t2))

print(f"=== 相同位置但类型不同 ({len(same_pos_diff)} 个): ===")
type_swap_counter = Counter()
for idx, is_buy, py_t, dll_t in same_pos_diff:
    side = "买" if is_buy else "卖"
    type_swap_counter[(py_t, dll_t)] += 1
for (pt, dt), cnt in type_swap_counter.most_common():
    print(f"  Python:{pt} -> DLL:{dt}: {cnt} 次")

# Show details for first 30
print(f"\n  详情 (前30个):")
for idx, is_buy, py_t, dll_t in same_pos_diff[:30]:
    side = "买" if is_buy else "卖"
    print(f"    K[{idx}] {side}: Python={py_t}, DLL={dll_t}")

# 2. Only-Python by type
print(f"\n=== 仅Python 按类型分 ({len(only_py)} 个): ===")
type_counter_py = Counter()
for idx, is_buy, t in only_py:
    type_counter_py[t] += 1
for t, cnt in type_counter_py.most_common():
    print(f"  {t}: {cnt}")

# 3. Only-DLL by type
print(f"\n=== 仅DLL 按类型分 ({len(only_dll)} 个): ===")
type_counter_dll = Counter()
for idx, is_buy, t in only_dll:
    type_counter_dll[t] += 1
for t, cnt in type_counter_dll.most_common():
    print(f"  {t}: {cnt}")

# 4. Position-only analysis
py_positions = {}
for idx, is_buy, types in py_bsp:
    py_positions[(idx, is_buy)] = set(types)
dll_positions = {}
for idx, is_buy, t in dll_bsp:
    if (idx, is_buy) not in dll_positions:
        dll_positions[(idx, is_buy)] = set()
    dll_positions[(idx, is_buy)].add(t)

all_pos = set(py_positions.keys()) | set(dll_positions.keys())
exact_match = 0
partial_match = 0
only_py_pos = 0
only_dll_pos = 0
for pos in all_pos:
    py_t = py_positions.get(pos, set())
    dll_t = dll_positions.get(pos, set())
    if py_t == dll_t:
        exact_match += 1
    elif len(py_t) > 0 and len(dll_t) > 0:
        partial_match += 1
    elif len(py_t) > 0:
        only_py_pos += 1
    else:
        only_dll_pos += 1

print(f"\n=== 位置级别统计: ===")
print(f"  总位置数: {len(all_pos)}")
print(f"  完全一致: {exact_match}")
print(f"  部分匹配(位置相同类型不同): {partial_match}")
print(f"  仅Python: {only_py_pos}")
print(f"  仅DLL: {only_dll_pos}")

# 5. Remaining pure only-py (no corresponding DLL at same position)
pure_only_py = []
for idx, is_buy, t in only_py:
    if (idx, is_buy) not in dll_positions:
        pure_only_py.append((idx, is_buy, t))
pure_only_dll = []
for idx, is_buy, t in only_dll:
    if (idx, is_buy) not in py_positions:
        pure_only_dll.append((idx, is_buy, t))

print(f"\n=== 纯仅Python (DLL该位置完全没有BSP, {len(pure_only_py)} 个): ===")
type_counter = Counter()
for idx, is_buy, t in pure_only_py:
    type_counter[t] += 1
for t, cnt in type_counter.most_common():
    print(f"  {t}: {cnt}")
print(f"  前20个:")
for idx, is_buy, t in pure_only_py[:20]:
    side = "买" if is_buy else "卖"
    print(f"    K[{idx}] {side}: {t}")

print(f"\n=== 纯仅DLL (Python该位置完全没有BSP, {len(pure_only_dll)} 个): ===")
type_counter = Counter()
for idx, is_buy, t in pure_only_dll:
    type_counter[t] += 1
for t, cnt in type_counter.most_common():
    print(f"  {t}: {cnt}")
print(f"  前20个:")
for idx, is_buy, t in pure_only_dll[:20]:
    side = "买" if is_buy else "卖"
    print(f"    K[{idx}] {side}: {t}")

# 6. Python multi-type BSPs (DLL output format limits to 1)
print(f"\n=== Python 多类型 BSP (DLL只输出一个): ===")
multi_type_bsp = [(idx, is_buy, types) for idx, is_buy, types in py_bsp if len(types) > 1]
print(f"  总共 {len(multi_type_bsp)} 个多类型BSP")
for idx, is_buy, types in multi_type_bsp[:20]:
    side = "买" if is_buy else "卖"
    dll_t = dll_positions.get((idx, is_buy), set())
    print(f"  K[{idx}] {side}: Python={types}, DLL={dll_t}")
