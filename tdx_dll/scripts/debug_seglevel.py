"""Quick debug: check CSegLevel intermediate data"""
import ctypes, os, sys, numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from DataAPI.TdxLocalAPI import CTdxLocalAPI
from Common.CEnum import KL_TYPE

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

dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "chan2026-full.dll")
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

c_arr = ctypes.c_float * n
pHigh = c_arr(*high.tolist())
pLow = c_arr(*low.tolist())
pClose = c_arr(*close.tolist())
pBi = c_arr(*[0.0] * n)
funcs[1](n, pBi, pHigh, pLow, pClose)

bi_count = sum(1 for i in range(n) if pBi[i] != 0)
print(f"K线数: {n}")
print(f"笔端点: {bi_count}")

pSeg = c_arr(*[0.0] * n)
funcs[9](n, pSeg, pBi, pHigh, pLow)
seg_count = sum(1 for i in range(n) if pSeg[i] != 0)
print(f"线段端点: {seg_count}")

pSegSeg = c_arr(*[0.0] * n)
funcs[13](n, pSegSeg, pBi, pHigh, pLow)
segseg_count = sum(1 for i in range(n) if pSegSeg[i] != 0)
print(f"线段的线段端点(Func13): {segseg_count}")
for i in range(n):
    if pSegSeg[i] != 0:
        print(f"  K[{i}] = {pSegSeg[i]}")

pSegBsp = c_arr(*[0.0] * n)
funcs[12](n, pSegBsp, pBi, pHigh, pLow)
segbsp_count = sum(1 for i in range(n) if pSegBsp[i] != 0)
print(f"线段级买卖点(Func12): {segbsp_count}")
