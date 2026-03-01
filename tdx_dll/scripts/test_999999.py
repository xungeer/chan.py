"""临时脚本：使用 999999 数据验证 DLL"""
import sys, os
sys.path.insert(0, os.path.abspath('..'))

from test_dll_accuracy import load_tdx_data, calc_dll, compare_bi, compare_seg, compare_bsp, BSP_CODE_MAP, PY_BSP_TYPE_MAP

from Chan import CChan
from ChanConfig import CChanConfig
from Common.CEnum import DATA_SRC, KL_TYPE, BSP_TYPE, BI_DIR
from DataAPI.TdxLocalAPI import CTdxLocalAPI
import numpy as np

code = 'sh.999999'
begin_time = '2000-01-01'  # 跳过早期异常数据
dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'chan2026-full.dll')

print('=' * 60)
print(f'端到端验证: code={code}, begin={begin_time}')
print('=' * 60)

# ---- 加载数据 ----
print(f'\n[0] 加载数据...')
CTdxLocalAPI.do_init()
api = CTdxLocalAPI(code=code, k_type=KL_TYPE.K_DAY, begin_date=begin_time)
highs, lows, closes = [], [], []
for klu in api.get_kl_data():
    highs.append(klu.high)
    lows.append(klu.low)
    closes.append(klu.close)
CTdxLocalAPI.do_close()
high = np.array(highs, dtype=np.float32)
low = np.array(lows, dtype=np.float32)
close = np.array(closes, dtype=np.float32)
print(f'  K线数: {len(high)}')

# ---- Python 计算 ----
print('\n[1] Python CChan 计算...')
config_dict = {
    "bi_algo": "fx", "bi_strict": True, "bi_fx_check": "loss",
    "bi_end_is_peak": True, "bi_allow_sub_peak": True,
    "gap_as_kl": False, "zs_combine": True, "zs_combine_mode": "peak",
    "one_bi_zs": False, "zs_algo": "auto", "left_seg_method": "peak",
    "divergence_rate": float("inf"), "min_zs_cnt": 0,
    "bsp2_follow_1": False, "bsp3_follow_1": False,
    "bs1_peak": False, "macd_algo": "peak",
    "bs_type": "1,2,3a,1p,2s,3b",
    "trigger_step": False, "print_warning": True,
}
config = CChanConfig(config_dict)
chan = CChan(code=code, begin_time=begin_time, data_src=DATA_SRC.TDX_LOCAL,
            lv_list=[KL_TYPE.K_DAY], config=config)
kl = chan[0]

py_bi = {}
for bi in kl.bi_list:
    bklu = bi.get_begin_klu()
    eklu = bi.get_end_klu()
    d = 1 if bi.dir == BI_DIR.UP else -1
    py_bi[bklu.idx] = -d
    py_bi[eklu.idx] = d

py_seg = {}
for seg in kl.seg_list:
    eklu = seg.get_end_klu()
    d = 1 if seg.dir == BI_DIR.UP else -1
    py_seg[eklu.idx] = d

py_bsp = []
for bsp in kl.bs_point_lst.getSortedBspList():
    types = [PY_BSP_TYPE_MAP[t] for t in bsp.type]
    py_bsp.append((bsp.klu.idx, bsp.is_buy, types))

print(f'  笔: {len(py_bi)}, 线段: {len(py_seg)}, 买卖点: {len(py_bsp)}')

# ---- DLL 计算 ----
print('\n[2] DLL 计算...')
dll_bi, dll_seg, dll_bsp = calc_dll(high, low, close, dll_path)
print(f'  笔: {len(dll_bi)}, 线段: {len(dll_seg)}, 买卖点: {len(dll_bsp)}')

# ---- 对比 ----
print('\n[3] 对比结果:')
all_pass = True

only_py, only_dll, dir_mm, matched = compare_bi(py_bi, dll_bi)
if not only_py and not only_dll and not dir_mm:
    print(f'  BI:  PASS ({matched})')
else:
    all_pass = False
    print(f'  BI:  DIFF matched={matched}, onlyPy={len(only_py)}, onlyDLL={len(only_dll)}, dirMM={len(dir_mm)}')
    if only_py: print(f'    onlyPy: {only_py[:10]}')
    if only_dll: print(f'    onlyDLL: {only_dll[:10]}')

only_py, only_dll, dir_mm, matched = compare_seg(py_seg, dll_seg)
if not only_py and not only_dll and not dir_mm:
    print(f'  SEG: PASS ({matched})')
else:
    all_pass = False
    print(f'  SEG: DIFF matched={matched}, onlyPy={len(only_py)}, onlyDLL={len(only_dll)}, dirMM={len(dir_mm)}')
    if only_py: print(f'    onlyPy: {only_py[:10]}')
    if only_dll: print(f'    onlyDLL: {only_dll[:10]}')

only_py, only_dll, matched_bsp = compare_bsp(py_bsp, dll_bsp)
if not only_py and not only_dll:
    print(f'  BSP: PASS ({len(matched_bsp)})')
else:
    all_pass = False
    print(f'  BSP: DIFF matched={len(matched_bsp)}, onlyPy={len(only_py)}, onlyDLL={len(only_dll)}')
    for idx, ib, t in only_py[:10]:
        print(f'    onlyPy: K[{idx}] {"buy" if ib else "sell"} {t}')
    for idx, ib, t in only_dll[:10]:
        print(f'    onlyDLL: K[{idx}] {"buy" if ib else "sell"} {t}')

print('\n' + '=' * 60)
print('RESULT:', 'ALL PASS' if all_pass else 'HAS DIFF')
print('=' * 60)
