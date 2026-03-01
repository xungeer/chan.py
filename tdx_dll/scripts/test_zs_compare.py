"""
test_zs_compare.py - 对比 DLL (Func2/3/4 CCentroid) 与 Python (chan.py zs_list) 的中枢输出

目标：评估 DLL 的流式中枢绘图 (CCentroid) 是否与 Python 的线段内中枢 (zs_list) 一致。

DLL 中枢来源：
  - CCentroid (流式算法): Func2=高边界, Func3=低边界, Func4=起止信号
  - 全局逐笔扫描，不区分线段

Python 中枢来源：
  - CZSList.cal_bi_zs: zs_algo="auto", 按线段分段计算
  - 每个中枢有 begin_bi/end_bi/high/low

使用方式：
  cd m:\\chan.py\\tdx_dll
  python scripts\\test_zs_compare.py
"""

import ctypes
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from Chan import CChan
from ChanConfig import CChanConfig
from Common.CEnum import DATA_SRC, KL_TYPE, BI_DIR


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


def calc_python_zs(code):
    """用 Python CChan 计算中枢列表"""
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
    kl = chan[0]

    py_zs_list = []
    for zs in kl.zs_list:
        begin_klu_idx = zs.begin.idx  # KLine_Unit 的原始K线索引
        end_klu_idx = zs.end.idx
        py_zs_list.append({
            'begin_klu_idx': begin_klu_idx,
            'end_klu_idx': end_klu_idx,
            'begin_bi_idx': zs.begin_bi.idx,
            'end_bi_idx': zs.end_bi.idx,
            'high': zs.high,
            'low': zs.low,
            'peak_high': zs.peak_high,
            'peak_low': zs.peak_low,
            'is_one_bi': zs.is_one_bi_zs(),
        })

    # 同时获取笔端点的K线索引映射
    bi_klu_map = {}
    for bi in kl.bi_list:
        begin_klu = bi.get_begin_klu()
        end_klu = bi.get_end_klu()
        bi_klu_map[bi.idx] = {
            'begin_klu_idx': begin_klu.idx,
            'end_klu_idx': end_klu.idx,
            'dir': 'UP' if bi.dir == BI_DIR.UP else 'DOWN',
        }

    return py_zs_list, bi_klu_map, len(list(kl.klu_iter()))


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


def calc_dll_zs(high, low, close, dll_path):
    """用 DLL 计算中枢 (Func2/3/4)"""
    funcs = load_dll(dll_path)
    n = len(high)
    c_arr = ctypes.c_float * n

    pHigh = c_arr(*high.tolist())
    pLow = c_arr(*low.tolist())
    pClose = c_arr(*close.tolist())

    # Func1: 笔标记
    pBi = c_arr(*[0.0] * n)
    funcs[1](n, pBi, pHigh, pLow, pClose)

    # Func2: 中枢高边界
    pZsH = c_arr(*[0.0] * n)
    funcs[2](n, pZsH, pBi, pHigh, pLow)

    # Func3: 中枢低边界
    pZsL = c_arr(*[0.0] * n)
    funcs[3](n, pZsL, pBi, pHigh, pLow)

    # Func4: 中枢起止信号
    pZsS = c_arr(*[0.0] * n)
    funcs[4](n, pZsS, pBi, pHigh, pLow)

    # 解析 Func4 输出：提取起止区间
    dll_zs_list = []
    current_start = None
    for i in range(n):
        if pZsS[i] == 1:  # 起点
            current_start = i
        elif pZsS[i] == 2:  # 终点
            if current_start is not None:
                # 从 pZsH/pZsL 中提取该区间的 high/low
                # 在区间内 pZsH[j] 和 pZsL[j] 是常数填充
                zs_high = pZsH[current_start]
                zs_low = pZsL[current_start]
                dll_zs_list.append({
                    'start_idx': current_start,
                    'end_idx': i,
                    'high': float(zs_high),
                    'low': float(zs_low),
                })
                current_start = None

    # 处理可能遗漏的尾部中枢（起点标记了但没有终点=2）
    if current_start is not None:
        # 查找尾部是否有填充
        zs_high = pZsH[current_start]
        zs_low = pZsL[current_start]
        if zs_high > 0:
            dll_zs_list.append({
                'start_idx': current_start,
                'end_idx': n - 1,
                'high': float(zs_high),
                'low': float(zs_low),
                'is_tail': True,
            })

    # 同时提取笔端点
    dll_bi_points = {}
    for i in range(n):
        if pBi[i] != 0:
            dll_bi_points[i] = int(pBi[i])

    return dll_zs_list, dll_bi_points


def match_zs(py_zs_list, dll_zs_list, tolerance=3):
    """
    尝试匹配 Python 和 DLL 的中枢
    tolerance: 起止K线索引允许的偏差
    """
    matched = []
    only_py = list(range(len(py_zs_list)))
    only_dll = list(range(len(dll_zs_list)))
    
    for pi, pz in enumerate(py_zs_list):
        best_di = None
        best_score = float('inf')
        for di in only_dll:
            dz = dll_zs_list[di]
            start_diff = abs(pz['begin_klu_idx'] - dz['start_idx'])
            end_diff = abs(pz['end_klu_idx'] - dz['end_idx'])
            h_diff = abs(pz['high'] - dz['high'])
            l_diff = abs(pz['low'] - dz['low'])
            
            if start_diff <= tolerance and end_diff <= tolerance:
                score = start_diff + end_diff + h_diff + l_diff
                if score < best_score:
                    best_score = score
                    best_di = di

        if best_di is not None:
            matched.append((pi, best_di, best_score))
            only_py.remove(pi)
            only_dll.remove(best_di)

    return matched, only_py, only_dll


def main():
    code = "sh.000300"
    dll_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "chan2026-full.dll")

    if not os.path.exists(dll_path):
        dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "chan2026-full.dll")
    if not os.path.exists(dll_path):
        print(f"ERROR: DLL not found")
        return 1

    print("=" * 70)
    print("中枢一致性分析: DLL CCentroid (Func2/3/4) vs Python zs_list")
    print("=" * 70)

    # 加载数据
    print(f"\n[0] 加载 {code} 日线数据...")
    high, low, close = load_tdx_data(code)
    print(f"  K线数: {len(high)}")

    # Python 计算
    print("\n[1] Python CChan 计算 (zs_algo=auto)...")
    py_zs_list, bi_klu_map, py_klu_cnt = calc_python_zs(code)
    print(f"  中枢数: {len(py_zs_list)}")

    # DLL 计算
    print("\n[2] DLL Func2/3/4 计算...")
    dll_zs_list, dll_bi_points = calc_dll_zs(high, low, close, dll_path)
    print(f"  中枢数: {len(dll_zs_list)}")

    # 详细输出
    print("\n" + "─" * 70)
    print("Python zs_list 详情:")
    print("─" * 70)
    for i, zs in enumerate(py_zs_list):
        one_bi = " [一笔中枢]" if zs['is_one_bi'] else ""
        print(f"  ZS[{i:3d}] K[{zs['begin_klu_idx']:4d}~{zs['end_klu_idx']:4d}]"
              f"  bi[{zs['begin_bi_idx']:3d}~{zs['end_bi_idx']:3d}]"
              f"  H={zs['high']:.2f}  L={zs['low']:.2f}"
              f"  PH={zs['peak_high']:.2f}  PL={zs['peak_low']:.2f}{one_bi}")

    print("\n" + "─" * 70)
    print("DLL CCentroid 详情:")
    print("─" * 70)
    for i, zs in enumerate(dll_zs_list):
        tail = " [尾部未完成]" if zs.get('is_tail') else ""
        print(f"  ZS[{i:3d}] K[{zs['start_idx']:4d}~{zs['end_idx']:4d}]"
              f"  H={zs['high']:.2f}  L={zs['low']:.2f}{tail}")

    # 匹配分析
    print("\n" + "─" * 70)
    print("匹配分析 (容差=5根K线):")
    print("─" * 70)
    matched, only_py, only_dll = match_zs(py_zs_list, dll_zs_list, tolerance=5)

    print(f"\n  匹配数: {len(matched)}")
    print(f"  仅Python: {len(only_py)}")
    print(f"  仅DLL: {len(only_dll)}")

    if matched:
        print(f"\n  匹配详情:")
        h_diffs = []
        l_diffs = []
        for pi, di, score in matched:
            pz = py_zs_list[pi]
            dz = dll_zs_list[di]
            start_diff = pz['begin_klu_idx'] - dz['start_idx']
            end_diff = pz['end_klu_idx'] - dz['end_idx']
            h_diff = abs(pz['high'] - dz['high'])
            l_diff = abs(pz['low'] - dz['low'])
            h_diffs.append(h_diff)
            l_diffs.append(l_diff)
            
            h_match = "✅" if h_diff < 0.01 else f"⚠️ Δ={h_diff:.2f}"
            l_match = "✅" if l_diff < 0.01 else f"⚠️ Δ={l_diff:.2f}"
            
            print(f"    PY[{pi}] K[{pz['begin_klu_idx']}~{pz['end_klu_idx']}] "
                  f"↔ DLL[{di}] K[{dz['start_idx']}~{dz['end_idx']}]"
                  f"  起点Δ={start_diff:+d} 终点Δ={end_diff:+d}"
                  f"  H:{h_match}  L:{l_match}")
        
        if h_diffs:
            print(f"\n  High差异统计: 平均={np.mean(h_diffs):.4f}, 最大={max(h_diffs):.4f}, "
                  f"完全一致={sum(1 for d in h_diffs if d < 0.01)}/{len(h_diffs)}")
            print(f"  Low差异统计:  平均={np.mean(l_diffs):.4f}, 最大={max(l_diffs):.4f}, "
                  f"完全一致={sum(1 for d in l_diffs if d < 0.01)}/{len(l_diffs)}")

    if only_py:
        print(f"\n  仅 Python 的中枢:")
        for pi in only_py:
            pz = py_zs_list[pi]
            one_bi = " [一笔中枢]" if pz['is_one_bi'] else ""
            print(f"    ZS[{pi}] K[{pz['begin_klu_idx']}~{pz['end_klu_idx']}]"
                  f"  H={pz['high']:.2f} L={pz['low']:.2f}{one_bi}")

    if only_dll:
        print(f"\n  仅 DLL 的中枢:")
        for di in only_dll:
            dz = dll_zs_list[di]
            tail = " [尾部]" if dz.get('is_tail') else ""
            print(f"    ZS[{di}] K[{dz['start_idx']}~{dz['end_idx']}]"
                  f"  H={dz['high']:.2f} L={dz['low']:.2f}{tail}")

    # 总结
    print("\n" + "=" * 70)
    total_py = len(py_zs_list)
    total_dll = len(dll_zs_list)
    match_rate = len(matched) / max(total_py, total_dll) * 100 if max(total_py, total_dll) > 0 else 0

    if len(matched) == total_py == total_dll and all(
        abs(py_zs_list[pi]['high'] - dll_zs_list[di]['high']) < 0.01 and
        abs(py_zs_list[pi]['low'] - dll_zs_list[di]['low']) < 0.01
        for pi, di, _ in matched
    ):
        print("✅ 中枢完全一致: 数量、位置、高低边界全部匹配")
    else:
        print(f"⚠️  中枢存在差异:")
        print(f"  Python: {total_py} 个, DLL: {total_dll} 个, 匹配: {len(matched)} 个 ({match_rate:.1f}%)")
        print(f"  仅Python: {len(only_py)} 个, 仅DLL: {len(only_dll)} 个")
        
        # 算法差异分析
        print("\n── 差异原因分析 ──")
        print("  DLL Func2/3/4 使用 CCentroid 流式算法：")
        print("    - 全局逐笔扫描，不区分线段边界")
        print("    - 中枢 = 连续笔的重叠区间（min_high > max_low）")
        print("    - 中枢合并使用 peak 模式")
        print("  Python zs_list 使用 CZSList.cal_bi_zs (zs_algo=auto)：")
        print("    - 按线段分段计算（sure seg用normal，unsure seg用over_seg）")
        print("    - 中枢限制在线段内部，不跨线段")
        print("    - 线段边界处会重新开始中枢构建")

    print("=" * 70)
    return 0


if __name__ == "__main__":
    sys.exit(main())
