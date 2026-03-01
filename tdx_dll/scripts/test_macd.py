"""
test_macd.py - 验证 DLL MACD 计算与 Python MACD 的一致性

使用方式：
  cd m:\chan.py\tdx_dll
  python test_macd.py

测试说明：
  1. 生成一组模拟收盘价数据
  2. 用 Python CMACD 类计算 DIF/DEA/MACD
  3. 用 ctypes 加载 chan2026.dll，调用 Func1+Func10 计算 MACD
  4. 逐 K 线对比，输出差异报告
"""

import ctypes
import os
import sys
import numpy as np

# 添加项目根目录到 path 以导入 chan.py 模块
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from Math.MACD import CMACD

def generate_test_data(n=200):
    """生成模拟 K 线数据"""
    np.random.seed(42)
    # 模拟一个有趋势和震荡的价格序列
    close = np.cumsum(np.random.randn(n) * 0.5) + 100
    close = close.astype(np.float32)
    # high/low 基于 close 扩展
    high = close + np.abs(np.random.randn(n) * 0.3).astype(np.float32)
    low  = close - np.abs(np.random.randn(n) * 0.3).astype(np.float32)
    return high, low, close

def test_python_macd(close_arr):
    """用 Python CMACD 计算 MACD"""
    macd = CMACD(fastperiod=12, slowperiod=26, signalperiod=9)
    results = []
    for c in close_arr:
        item = macd.add(float(c))
        results.append((item.DIF, item.DEA, item.macd))
    return results

def test_dll_macd(high, low, close, dll_path):
    """用 DLL Func1 + Func10 计算 MACD"""
    dll = ctypes.CDLL(dll_path)

    n = len(close)

    # 准备数组
    c_float_arr = ctypes.c_float * n
    pOut = c_float_arr(*[0.0] * n)
    pHigh = c_float_arr(*high.tolist())
    pLow = c_float_arr(*low.tolist())
    pClose = c_float_arr(*close.tolist())

    # 通达信 DLL 函数签名: void Func(int nCount, float* pOut, float* a, float* b, float* c)
    FUNC_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_int,
                                  ctypes.POINTER(ctypes.c_float),
                                  ctypes.POINTER(ctypes.c_float),
                                  ctypes.POINTER(ctypes.c_float),
                                  ctypes.POINTER(ctypes.c_float))

    # 定义 PluginTCalcFuncInfo 结构体 (pack=1 匹配 DLL 的 #pragma pack(push,1))
    class PluginTCalcFuncInfo(ctypes.Structure):
        _pack_ = 1
        _fields_ = [
            ("nFuncMark", ctypes.c_ushort),
            ("pCallFunc", ctypes.c_void_p),
        ]

    # RegisterTdxFunc
    RegisterTdxFunc = dll.RegisterTdxFunc
    RegisterTdxFunc.argtypes = [ctypes.POINTER(ctypes.POINTER(PluginTCalcFuncInfo))]
    RegisterTdxFunc.restype = ctypes.c_bool

    # 获取函数表
    pInfo = ctypes.POINTER(PluginTCalcFuncInfo)()
    RegisterTdxFunc(ctypes.byref(pInfo))

    # 解析函数表，找到 Func1 和 Func10
    func1 = None
    func10 = None
    i = 0
    while True:
        entry = pInfo[i]
        if entry.nFuncMark == 0:
            break
        func_ptr = FUNC_TYPE(entry.pCallFunc)
        if entry.nFuncMark == 1:
            func1 = func_ptr
        elif entry.nFuncMark == 10:
            func10 = func_ptr
        i += 1

    if func1 is None or func10 is None:
        raise RuntimeError("Failed to find Func1/Func10 in DLL")

    # 调用 Func1：计算笔 + MACD
    func1(n, pOut, pHigh, pLow, pClose)

    # 调用 Func10 获取 MACD/DIF/DEA
    results = []
    for mode in [0, 1, 2]:  # 0=MACD, 1=DIF, 2=DEA
        pResult = c_float_arr(*[0.0] * n)
        pMode = c_float_arr(*[float(mode)] * n)
        pDummy = c_float_arr(*[0.0] * n)
        func10(n, pResult, pOut, pDummy, pMode)
        results.append([pResult[j] for j in range(n)])

    # results[0]=MACD, results[1]=DIF, results[2]=DEA
    dll_data = []
    for j in range(n):
        dll_data.append((results[1][j], results[2][j], results[0][j]))  # DIF, DEA, MACD
    return dll_data

def compare_results(py_results, dll_results, tolerance=1e-4):
    """逐 K 线对比 Python 和 DLL 的 MACD 结果"""
    n = len(py_results)
    max_diff = {"DIF": 0.0, "DEA": 0.0, "MACD": 0.0}
    errors = []

    for i in range(n):
        py_dif, py_dea, py_macd = py_results[i]
        dll_dif, dll_dea, dll_macd = dll_results[i]

        diff_dif = abs(py_dif - dll_dif)
        diff_dea = abs(py_dea - dll_dea)
        diff_macd = abs(py_macd - dll_macd)

        max_diff["DIF"] = max(max_diff["DIF"], diff_dif)
        max_diff["DEA"] = max(max_diff["DEA"], diff_dea)
        max_diff["MACD"] = max(max_diff["MACD"], diff_macd)

        if diff_dif > tolerance or diff_dea > tolerance or diff_macd > tolerance:
            errors.append({
                "idx": i,
                "py": (py_dif, py_dea, py_macd),
                "dll": (dll_dif, dll_dea, dll_macd),
                "diff": (diff_dif, diff_dea, diff_macd)
            })

    return max_diff, errors

def main():
    print("=" * 60)
    print("MACD 计算验证: Python vs DLL")
    print("=" * 60)

    # 生成测试数据
    high, low, close = generate_test_data(200)
    print(f"\n测试数据: {len(close)} 根 K 线")
    print(f"  收盘价范围: {close.min():.2f} ~ {close.max():.2f}")

    # Python MACD
    print("\n[1/3] 计算 Python MACD...")
    py_results = test_python_macd(close)
    print(f"  Python 最后一根: DIF={py_results[-1][0]:.6f}, DEA={py_results[-1][1]:.6f}, MACD={py_results[-1][2]:.6f}")

    # DLL MACD
    print("\n[2/3] 计算 DLL MACD...")
    dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "chan2026.dll")
    if not os.path.exists(dll_path):
        print(f"  ERROR: DLL not found at {dll_path}")
        return 1

    try:
        dll_results = test_dll_macd(high, low, close, dll_path)
        print(f"  DLL   最后一根: DIF={dll_results[-1][0]:.6f}, DEA={dll_results[-1][1]:.6f}, MACD={dll_results[-1][2]:.6f}")
    except Exception as e:
        print(f"  ERROR: DLL call failed: {e}")
        return 1

    # 对比
    print("\n[3/3] 对比结果...")
    max_diff, errors = compare_results(py_results, dll_results)

    print(f"\n  最大误差:")
    print(f"    DIF:  {max_diff['DIF']:.8f}")
    print(f"    DEA:  {max_diff['DEA']:.8f}")
    print(f"    MACD: {max_diff['MACD']:.8f}")

    if errors:
        print(f"\n  ⚠️  超过容差(1e-4)的 K 线数: {len(errors)}")
        for e in errors[:5]:  # 只显示前5个
            print(f"    K线[{e['idx']}]: py={e['py']}, dll={e['dll']}, diff={e['diff']}")
    else:
        print(f"\n  ✅ 所有 {len(py_results)} 根 K 线的 MACD 计算完全一致 (容差 < 1e-4)")

    print("\n" + "=" * 60)
    return 0 if not errors else 1

if __name__ == "__main__":
    sys.exit(main())
