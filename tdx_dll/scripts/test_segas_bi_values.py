"""
test_segas_bi_values.py - 验证 segBiPoints 的 value 值是否与 Python 的 _high/_low 一致
"""

import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from Chan import CChan
from ChanConfig import CChanConfig
from Common.CEnum import DATA_SRC, KL_TYPE, BI_DIR

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

def main():
    from DataAPI.TdxLocalAPI import CTdxLocalAPI
    CTdxLocalAPI.do_init()
    chan = calc_python()
    CTdxLocalAPI.do_close()
    kl = chan[0]

    print("=" * 70)
    print("分析 seg 作为 'bi' 时的 _high/_low 值")
    print("=" * 70)

    # 在 Python 中，线段（seg）作为虚拟"笔"进入第二层 cal_seg()
    # seg._high() 和 seg._low() 就是这些虚拟"笔"的 high/low
    # DLL 中 getBiHighLow 使用 segBiPoints[i].value
    #
    # 问题：segBiPoints[i].value = biPoints[seg.startBiIdx].value 或 biPoints[seg.biIdx+1].value
    # 这应该等于 seg.get_begin_val() / seg.get_end_val()
    # 但 seg._high() / seg._low() 是不同的！

    print("\n  Seg  | dir  | begin_val | end_val   | _high     | _low      | diff?")
    print("  " + "-" * 65)

    for i, seg in enumerate(kl.seg_list[:30]):
        d = "UP" if seg.dir == BI_DIR.UP else "DN"
        begin_val = seg.get_begin_val()
        end_val = seg.get_end_val()
        h = seg._high()
        l = seg._low()

        # 在 DLL 中:
        # UP seg (底→顶): segBiPoints 底.value=begin_val 顶.value=end_val
        # getBiHighLow(UP笔): high = 顶.value(=end_val), low = 底.value(=begin_val)
        # Python: _high = end_val, _low = begin_val  → 应该一致？
        # 但 Python 的 _high 和 _low 用的是 get_end_klu().high 而不是 end_val
        if seg.is_up():
            dll_high = end_val    # biPoints[seg端顶].value
            dll_low = begin_val   # biPoints[seg端底].value
        else:
            dll_high = begin_val  # biPoints[seg端顶].value
            dll_low = end_val     # biPoints[seg端底].value

        diff = "YES" if (abs(h - dll_high) > 0.01 or abs(l - dll_low) > 0.01) else ""
        print(f"  {i:3d}  | {d:4s} | {begin_val:9.2f} | {end_val:9.2f} | {h:9.2f} | {l:9.2f} | {diff}")
        if diff:
            print(f"       |  DLL would use: high={dll_high:.2f} low={dll_low:.2f}")
            print(f"       |  Python actual:  high={h:.2f}       low={l:.2f}")

    # 关键区间: Seg[7]-Seg[13] (K[86]-K[209])
    print(f"\n\n  关键区间分析 (Seg[7]-Seg[13]):")
    for i in range(7, 14):
        seg = kl.seg_list[i]
        d = "UP" if seg.dir == BI_DIR.UP else "DN"
        begin_val = seg.get_begin_val()
        end_val = seg.get_end_val()
        h = seg._high()
        l = seg._low()
        begin_klu = seg.get_begin_klu()
        end_klu = seg.get_end_klu()

        # 详细: begin_klu 的 high/low vs begin_val
        print(f"  Seg[{i:2d}] {d}: K[{begin_klu.idx}]-K[{end_klu.idx}]")
        print(f"    begin_val={begin_val:.2f} begin_klu.high={begin_klu.high:.2f} begin_klu.low={begin_klu.low:.2f}")
        print(f"    end_val  ={end_val:.2f}   end_klu.high  ={end_klu.high:.2f}   end_klu.low  ={end_klu.low:.2f}")
        print(f"    _high    ={h:.2f}   _low          ={l:.2f}")

        # Python: UP seg → _high=end_klu.high, _low=begin_klu.low
        #         DN seg → _high=begin_klu.high, _low=end_klu.low
        if seg.is_up():
            actual_high = end_klu.high
            actual_low = begin_klu.low
        else:
            actual_high = begin_klu.high
            actual_low = end_klu.low

        # DLL approximation: UP → high=end_val, low=begin_val
        #                    DN → high=begin_val, low=end_val
        dll_high = end_val if seg.is_up() else begin_val
        dll_low = begin_val if seg.is_up() else end_val

        if abs(actual_high - dll_high) > 0.01 or abs(actual_low - dll_low) > 0.01:
            print(f"    ** DIFF: Python high={actual_high:.2f} vs DLL high={dll_high:.2f}")
            print(f"    ** DIFF: Python low ={actual_low:.2f}  vs DLL low ={dll_low:.2f}")

if __name__ == "__main__":
    main()
