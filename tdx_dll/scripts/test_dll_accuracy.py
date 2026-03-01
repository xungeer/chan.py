"""
test_dll_accuracy.py - DLL 端到端精度验证
对比 DLL (chan2026.dll) 与 Python (chan.py) 的笔/线段/买卖点输出

使用方式：
  cd m:\chan.py\tdx_dll
  python test_dll_accuracy.py

测试说明：
  1. 使用 TdxLocalAPI 读取本地通达信 000300 日线数据
  2. Python 端用 CChan 完整计算 笔→线段→中枢→买卖点
  3. DLL 端用 ctypes 调用 Func1/Func5/Func9
  4. 逐项对比：笔端点、线段端点、买卖点
"""

import ctypes
import os
import sys
import numpy as np

# 添加项目根目录
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from Chan import CChan
from ChanConfig import CChanConfig
from Common.CEnum import DATA_SRC, KL_TYPE, BSP_TYPE, BI_DIR

# ==========================================================================
# DLL 买卖点编码 ↔ Python BSP_TYPE 映射
# ==========================================================================
# DLL 编码: 买点 1/1.5/2/2.5/3/3.5, 卖点 11/11.5/12/12.5/13/13.5
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
    """使用 TdxLocalAPI 加载数据，返回 high/low/close 数组"""
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
    """用 Python CChan 计算笔/线段/买卖点"""
    # 配置与 ChanConfig.h 完全对齐
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
        "zs_algo": "auto",
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

    kl = chan[0]  # 最高级别的 KLine_List

    # --- 提取笔端点 ---
    # 每个bi有 begin_klu 和 end_klu，其 idx 是原始K线索引
    py_bi_points = []
    for bi in kl.bi_list:
        begin_klu = bi.get_begin_klu()
        end_klu = bi.get_end_klu()
        dir_val = 1 if bi.dir == BI_DIR.UP else -1
        # 起点
        py_bi_points.append((begin_klu.idx, -dir_val))  # 起点方向与笔方向相反
        # 终点
        py_bi_points.append((end_klu.idx, dir_val))

    # 去重（相邻笔共享端点）
    seen = {}
    for idx, d in py_bi_points:
        seen[idx] = d  # 后出现的覆盖（实际上应一致）
    py_bi_dict = seen

    # --- 提取线段端点 ---
    py_seg_points = {}
    for seg in kl.seg_list:
        begin_klu = seg.get_begin_klu()
        end_klu = seg.get_end_klu()
        dir_val = 1 if seg.dir == BI_DIR.UP else -1
        py_seg_points[end_klu.idx] = dir_val

    # --- 提取买卖点 ---
    py_bsp_list = []
    for bsp in kl.bs_point_lst.getSortedBspList():
        klu_idx = bsp.klu.idx
        is_buy = bsp.is_buy
        types = [PY_BSP_TYPE_MAP[t] for t in bsp.type]
        py_bsp_list.append((klu_idx, is_buy, types))

    return py_bi_dict, py_seg_points, py_bsp_list, len(list(kl.klu_iter()))


def load_dll(dll_path):
    """加载 DLL 并返回函数指针字典"""
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


def calc_dll(high, low, close, dll_path):
    """用 DLL 计算笔/线段/买卖点"""
    funcs = load_dll(dll_path)
    n = len(high)
    c_arr = ctypes.c_float * n

    pHigh = c_arr(*high.tolist())
    pLow = c_arr(*low.tolist())
    pClose = c_arr(*close.tolist())

    # Func1: 笔标记
    pBi = c_arr(*[0.0] * n)
    funcs[1](n, pBi, pHigh, pLow, pClose)

    # Func9: 线段标记
    pSeg = c_arr(*[0.0] * n)
    funcs[9](n, pSeg, pBi, pHigh, pLow)

    # Func11: 买卖点完整列表（含同笔多类型）
    pBspAll = c_arr(*[0.0] * n)
    funcs[11](n, pBspAll, pBi, pHigh, pLow)

    # 提取非零位置
    dll_bi_dict = {}
    for i in range(n):
        if pBi[i] != 0:
            dll_bi_dict[i] = int(pBi[i])

    dll_seg_points = {}
    for i in range(n):
        v = pSeg[i]
        if v != 0:
            dll_seg_points[i] = 1 if v > 0 else -1

    # 从 Func11 输出解析所有BSP条目
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

    return dll_bi_dict, dll_seg_points, dll_bsp_list


def compare_bi(py_bi, dll_bi):
    """对比笔端点"""
    py_set = set(py_bi.keys())
    dll_set = set(dll_bi.keys())

    only_py = sorted(py_set - dll_set)
    only_dll = sorted(dll_set - py_set)
    common = sorted(py_set & dll_set)

    dir_mismatch = []
    for idx in common:
        if py_bi[idx] != dll_bi[idx]:
            dir_mismatch.append((idx, py_bi[idx], dll_bi[idx]))

    return only_py, only_dll, dir_mismatch, len(common)


def compare_seg(py_seg, dll_seg):
    """对比线段端点"""
    py_set = set(py_seg.keys())
    dll_set = set(dll_seg.keys())

    only_py = sorted(py_set - dll_set)
    only_dll = sorted(dll_set - py_set)
    common = sorted(py_set & dll_set)

    dir_mismatch = []
    for idx in common:
        if py_seg[idx] != dll_seg[idx]:
            dir_mismatch.append((idx, py_seg[idx], dll_seg[idx]))

    return only_py, only_dll, dir_mismatch, len(common)


def compare_bsp(py_bsp, dll_bsp):
    """对比买卖点"""
    # Python: [(idx, is_buy, [types...]), ...]
    # DLL:   [(idx, is_buy, type_str), ...]

    # 展开 Python 的多类型
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

    return only_py, only_dll, matched


def main():
    print("=" * 60)
    print("端到端验证: Python chan.py vs DLL chan2026.dll")
    print("=" * 60)

    code = "sh.000300"
    dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "chan2026-full.dll")

    if not os.path.exists(dll_path):
        print(f"  ERROR: DLL not found at {dll_path}")
        return 1

    # ---- 加载数据 ----
    print(f"\n[0/4] 加载 {code} 日线数据...")
    high, low, close = load_tdx_data(code)
    n = len(high)
    print(f"  K线数: {n}")

    # ---- Python 端计算 ----
    print("\n[1/4] Python CChan 计算...")
    py_bi, py_seg, py_bsp, py_klu_cnt = calc_python(code)
    print(f"  Python K线数: {py_klu_cnt}, 笔端点: {len(py_bi)}, 线段端点: {len(py_seg)}, 买卖点: {len(py_bsp)}")

    # ---- DLL 端计算 ----
    print("\n[2/4] DLL Func1/5/9 计算...")
    dll_bi, dll_seg, dll_bsp = calc_dll(high, low, close, dll_path)
    print(f"  DLL 笔端点: {len(dll_bi)}, 线段端点: {len(dll_seg)}, 买卖点: {len(dll_bsp)}")

    all_pass = True

    # ---- 笔端点对比 ----
    print("\n[3/4] 逐项对比...")
    print("\n  ── 笔端点 ──")
    only_py, only_dll, dir_mm, matched = compare_bi(py_bi, dll_bi)
    if not only_py and not only_dll and not dir_mm:
        print(f"  ✅ 笔端点 100% 一致 ({matched} 个端点)")
    else:
        all_pass = False
        print(f"  ⚠️  笔端点存在差异: 匹配={matched}, 仅Python={len(only_py)}, 仅DLL={len(only_dll)}, 方向不一致={len(dir_mm)}")
        if only_py:
            print(f"    仅 Python: {only_py[:10]}{'...' if len(only_py)>10 else ''}")
        if only_dll:
            print(f"    仅 DLL:    {only_dll[:10]}{'...' if len(only_dll)>10 else ''}")
        if dir_mm:
            for idx, pd, dd in dir_mm[:5]:
                print(f"    K线[{idx}]: Python dir={pd}, DLL dir={dd}")

    # ---- 线段端点对比 ----
    print("\n  ── 线段端点 ──")
    only_py, only_dll, dir_mm, matched = compare_seg(py_seg, dll_seg)
    if not only_py and not only_dll and not dir_mm:
        print(f"  ✅ 线段端点 100% 一致 ({matched} 个端点)")
    else:
        all_pass = False
        print(f"  ⚠️  线段端点存在差异: 匹配={matched}, 仅Python={len(only_py)}, 仅DLL={len(only_dll)}, 方向不一致={len(dir_mm)}")
        if only_py:
            print(f"    仅 Python: {only_py[:10]}{'...' if len(only_py)>10 else ''}")
        if only_dll:
            print(f"    仅 DLL:    {only_dll[:10]}{'...' if len(only_dll)>10 else ''}")

    # ---- 买卖点对比 ----
    print("\n  ── 买卖点 ──")
    only_py, only_dll, matched = compare_bsp(py_bsp, dll_bsp)

    print(f"  Python 买卖点详情:")
    for idx, is_buy, types in sorted(py_bsp):
        side = "买" if is_buy else "卖"
        print(f"    K线[{idx}] {side}点: {','.join(types)}")

    print(f"  DLL 买卖点详情:")
    for idx, is_buy, t in sorted(dll_bsp):
        side = "买" if is_buy else "卖"
        print(f"    K线[{idx}] {side}点: {t}")

    if not only_py and not only_dll:
        print(f"\n  ✅ 买卖点 100% 一致 ({len(matched)} 个买卖点)")
    else:
        all_pass = False
        print(f"\n  ⚠️  买卖点存在差异: 匹配={len(matched)}, 仅Python={len(only_py)}, 仅DLL={len(only_dll)}")
        if only_py:
            print(f"    仅 Python:")
            for idx, is_buy, t in only_py[:10]:
                side = "买" if is_buy else "卖"
                print(f"      K线[{idx}] {side}点 {t}")
        if only_dll:
            print(f"    仅 DLL:")
            for idx, is_buy, t in only_dll[:10]:
                side = "买" if is_buy else "卖"
                print(f"      K线[{idx}] {side}点 {t}")

    # ---- 总结 ----
    print("\n" + "=" * 60)
    if all_pass:
        print("✅ 全部通过: 笔/线段/买卖点与 Python 100% 一致")
    else:
        print("⚠️  存在差异，请检查上方详情")
    print("=" * 60)

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
