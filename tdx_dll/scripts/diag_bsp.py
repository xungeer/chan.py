"""对比修复后 DLL vs Python 的线段分界和买卖点"""
import sys, os, ctypes
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from Chan import CChan
from ChanConfig import CChanConfig
from Common.CEnum import DATA_SRC, KL_TYPE, BI_DIR
from DataAPI.TdxLocalAPI import CTdxLocalAPI

# Load data
CTdxLocalAPI.do_init()
api = CTdxLocalAPI(code="sh.000300", k_type=KL_TYPE.K_DAY)
highs, lows, closes = [], [], []
for klu in api.get_kl_data():
    highs.append(klu.high)
    lows.append(klu.low)
    closes.append(klu.close)
CTdxLocalAPI.do_close()

high = np.array(highs, dtype=np.float32)
low = np.array(lows, dtype=np.float32)
close = np.array(closes, dtype=np.float32)
n = len(high)

# DLL
dll_dir = os.path.dirname(os.path.abspath(__file__))
os.add_dll_directory(dll_dir)
dll = ctypes.CDLL(os.path.join(dll_dir, "chan2026-full.dll"))
FUNC_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.POINTER(ctypes.c_float),
    ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float))
class PTCFI(ctypes.Structure):
    _pack_ = 1
    _fields_ = [("nFuncMark", ctypes.c_ushort), ("pCallFunc", ctypes.c_void_p)]
pInfo = ctypes.POINTER(PTCFI)()
dll.RegisterTdxFunc(ctypes.byref(pInfo))
funcs = {}
i = 0
while pInfo[i].nFuncMark != 0:
    funcs[pInfo[i].nFuncMark] = FUNC_TYPE(pInfo[i].pCallFunc)
    i += 1

c_arr = ctypes.c_float * n
pBi = c_arr(*[0.0]*n)
funcs[1](n, pBi, c_arr(*high.tolist()), c_arr(*low.tolist()), c_arr(*close.tolist()))

# 重建 biPoints
bi_endpoints = []
for idx in range(n):
    if pBi[idx] != 0:
        bi_endpoints.append(idx)

# DLL Seg
pSeg = c_arr(*[0.0]*n)
funcs[9](n, pSeg, pBi, c_arr(*high.tolist()), c_arr(*low.tolist()))
dll_seg_klus = [idx for idx in range(n) if pSeg[idx] != 0]

# DLL BSP
pBsp = c_arr(*[0.0]*n)
funcs[5](n, pBsp, pBi, c_arr(*high.tolist()), c_arr(*low.tolist()))
dll_bsps = {}
for idx in range(n):
    if pBsp[idx] != 0:
        dll_bsps[idx] = pBsp[idx]

# Python
config_dict = {
    "bi_algo": "fx", "bi_strict": True, "bi_fx_check": "loss",
    "bi_end_is_peak": True, "bi_allow_sub_peak": True, "gap_as_kl": False,
    "zs_combine": True, "zs_combine_mode": "peak", "one_bi_zs": False,
    "zs_algo": "over_seg", "left_seg_method": "peak",
    "divergence_rate": float("inf"), "min_zs_cnt": 0,
    "bsp2_follow_1": False, "bsp3_follow_1": False,
    "bs1_peak": False, "macd_algo": "peak",
    "bs_type": "1,2,3a,1p,2s,3b", "trigger_step": False,
}
config = CChanConfig(config_dict)
chan = CChan("sh.000300", data_src=DATA_SRC.TDX_LOCAL,
            lv_list=[KL_TYPE.K_DAY], config=config)
kl = chan[0]
py_seg_klus = [seg.get_end_klu().idx for seg in kl.seg_list]

# 比较线段
print("=" * 60)
print("线段端点对比 (前20个)")
print("=" * 60)
max_len = max(len(dll_seg_klus), len(py_seg_klus))
match_count = 0
for i in range(min(20, max_len)):
    dll_klu = dll_seg_klus[i] if i < len(dll_seg_klus) else "---"
    py_klu = py_seg_klus[i] if i < len(py_seg_klus) else "---"
    m = "✅" if dll_klu == py_klu else "❌"
    if dll_klu == py_klu:
        match_count += 1
    print(f"  Seg[{i:2d}]: DLL klu={str(dll_klu):>5s}  Py klu={str(py_klu):>5s}  {m}")

# 全量对比
total_match = sum(1 for i in range(min(len(dll_seg_klus), len(py_seg_klus))) if dll_seg_klus[i] == py_seg_klus[i])
print(f"\n总线段: DLL={len(dll_seg_klus)}, Python={len(py_seg_klus)}")
print(f"匹配数: {total_match} / {min(len(dll_seg_klus), len(py_seg_klus))}")

# BSP 比较
BSP_MAP = {1:"T1买",1.5:"T1P买",2:"T2买",2.5:"T2S买",3:"T3A买",3.5:"T3B买",
           11:"T1卖",11.5:"T1P卖",12:"T2卖",12.5:"T2S卖",13:"T3A卖",13.5:"T3B卖"}
py_bsps = {}
for bsp in kl.bs_point_lst.getSortedBspList():
    klu_idx = bsp.klu.idx
    types = [t.value for t in bsp.type]
    is_buy = bsp.is_buy
    for t in types:
        if is_buy:
            code = {"1":1, "1p":1.5, "2":2, "2s":2.5, "3a":3, "3b":3.5}.get(t, 0)
        else:
            code = {"1":11, "1p":11.5, "2":12, "2s":12.5, "3a":13, "3b":13.5}.get(t, 0)
        if code:
            py_bsps[klu_idx] = code  # 取最后一种类型

all_klu = sorted(set(list(dll_bsps.keys()) + list(py_bsps.keys())))
matched = 0
only_py = 0
only_dll = 0
type_mismatch = 0
for klu_idx in all_klu:
    d = dll_bsps.get(klu_idx)
    p = py_bsps.get(klu_idx)
    if d and p:
        if d == p:
            matched += 1
        else:
            type_mismatch += 1
    elif d and not p:
        only_dll += 1
    elif p and not d:
        only_py += 1

print(f"\n{'='*60}")
print(f"买卖点对比")
print(f"{'='*60}")
print(f"  完全匹配: {matched}")
print(f"  类型不匹配: {type_mismatch}")
print(f"  仅DLL: {only_dll}")
print(f"  仅Python: {only_py}")
print(f"  DLL总数: {len(dll_bsps)}, Python总数: {len(py_bsps)}")
