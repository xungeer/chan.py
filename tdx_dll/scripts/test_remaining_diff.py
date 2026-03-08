"""
test_remaining_diff.py - 分析剩余线段级差异
1. K[97] T1 vs T1P (线段级go_bsp1判断差异)
2. K[209] 多出的线段的线段 (CSegDetector差异)
3. K[1235]/K[2767] DLL多出T1
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

PY_BSP_TYPE_MAP = {
    BSP_TYPE.T1: 'T1', BSP_TYPE.T1P: 'T1P',
    BSP_TYPE.T2: 'T2', BSP_TYPE.T2S: 'T2S',
    BSP_TYPE.T3A: 'T3A', BSP_TYPE.T3B: 'T3B',
}

def main():
    code = "sh.000300"
    print("=" * 70)
    print("剩余差异根因分析")
    print("=" * 70)

    high, low, close = load_tdx_data(code)
    chan = calc_python(code)
    kl = chan[0]

    # ============================================================
    # 1. K[97] T1 vs T1P 分析
    # ============================================================
    print("\n" + "=" * 50)
    print("1. K[97] 买点类型差异 (Python=T1P, DLL=T1)")
    print("=" * 50)

    # K[97] 是 Python SegSeg[1] 的终点 = SegSeg[2] 的起点
    # 在线段级别, K[97] 对应 seg_list 中的 Seg[7] 终点
    # 线段级买卖点使用 segseg_list 作为 segPoints

    # Python 的 "线段级别" 计算中：
    #   "笔" = seg_list (每个线段作为一个虚拟笔)
    #   "线段" = segseg_list (线段的线段)
    #   "中枢" = segzs_list (线段级中枢)

    # 找到 segseg_list 中以 K[97] 结尾的线段的线段
    for i, ss in enumerate(kl.segseg_list):
        end_idx = ss.get_end_klu().idx
        if end_idx == 97:
            print(f"  Python SegSeg[{i}]: K[{ss.get_begin_klu().idx}]-K[{end_idx}]"
                  f" dir={'UP' if ss.dir == BI_DIR.UP else 'DOWN'}")
            # 在这个 segseg 中有哪些 seg
            print(f"    包含的线段:")
            for j, seg in enumerate(kl.seg_list):
                sb = seg.get_begin_klu().idx
                se = seg.get_end_klu().idx
                if sb >= ss.get_begin_klu().idx and se <= end_idx:
                    print(f"      Seg[{j}]: K[{sb}]-K[{se}] dir={'UP' if seg.dir == BI_DIR.UP else 'DOWN'}")

    # 找 K[97] 的 seg_bs_point_lst 买卖点
    for bsp in kl.seg_bs_point_lst.getSortedBspList():
        if bsp.klu.idx == 97:
            types = [PY_BSP_TYPE_MAP[t] for t in bsp.type]
            side = "买" if bsp.is_buy else "卖"
            print(f"\n  Python K[97] 线段级买卖点: {side}点 {types}")
            print(f"    is_buy={bsp.is_buy}")
            # 查看由什么逻辑生成的
            bi = bsp.bi
            print(f"    bi: dir={'UP' if bi.dir == BI_DIR.UP else 'DOWN'}")
            print(f"    bi range: K[{bi.get_begin_klu().idx}]-K[{bi.get_end_klu().idx}]")

    # 分析线段级中枢（segzs_list）
    # K[97] 在 segseg[1] 终点，对应 seg_list 中 Seg[7]
    # 那么在 seg_list 坐标中，Seg[7] 的最后一个中枢是什么？
    print(f"\n  线段级中枢 (segzs_list) 的前5个:")
    for i, zs in enumerate(kl.segzs_list):
        if i >= 5:
            break
        # 中枢的 begin(开始线段) 和 end(结束线段) 是 seg_list 中的 seg 对象
        begin_seg = zs.begin
        end_seg = zs.end

        begin_begin_idx = begin_seg.get_begin_klu().idx if hasattr(begin_seg, 'get_begin_klu') else "?"
        end_end_idx = end_seg.get_end_klu().idx if hasattr(end_seg, 'get_end_klu') else "?"

        bi_in_info = ""
        if zs.bi_in is not None:
            bi_in_info = f"bi_in=K[{zs.bi_in.get_begin_klu().idx}]-K[{zs.bi_in.get_end_klu().idx}]"
        bi_out_info = ""
        if zs.bi_out is not None:
            bi_out_info = f"bi_out=K[{zs.bi_out.get_begin_klu().idx}]-K[{zs.bi_out.get_end_klu().idx}]"

        bi_lst_info = ""
        if zs.bi_lst:
            bi_lst_info = f"bi_lst={len(zs.bi_lst)}"

        print(f"    SegZS[{i}]: begin=K[{begin_begin_idx}], end=K[{end_end_idx}], "
              f"[{zs.low:.2f}, {zs.high:.2f}], "
              f"is_one_bi_zs={zs.is_one_bi_zs()}, "
              f"{bi_in_info} {bi_out_info} {bi_lst_info}")

    # ============================================================
    # 2. K[1235] / K[2767] 多出 T1 分析
    # ============================================================
    print("\n" + "=" * 50)
    print("2. K[1235] / K[2767] DLL 多出 T1 分析")
    print("=" * 50)

    # 在 Python segseg_list 中，
    # SegSeg[5]: K[1113]-K[1235] DOWN
    # SegSeg[19]: K[2664]-K[2767] DOWN
    # 这些位置应该有BSP1信号（记录在bsp1BiIndices），但Python的is_target_bsp=false

    # 检查 Python 是否在 K[1235] 和 K[2767] 有 BSP1（但不输出）
    for target in [1235, 2767]:
        found = False
        for bsp in kl.seg_bs_point_lst.getSortedBspList():
            if bsp.klu.idx == target:
                types = [PY_BSP_TYPE_MAP[t] for t in bsp.type]
                side = "买" if bsp.is_buy else "卖"
                print(f"  Python K[{target}]: {side}点 {types}")
                found = True
        if not found:
            print(f"  Python K[{target}]: 无买卖点 (is_target_bsp=false 未输出)")

    # 找对应 segseg
    for target in [1235, 2767]:
        for i, ss in enumerate(kl.segseg_list):
            end_idx = ss.get_end_klu().idx
            if end_idx == target:
                d = "UP" if ss.dir == BI_DIR.UP else "DOWN"
                print(f"  SegSeg[{i}]: K[{ss.get_begin_klu().idx}]-K[{end_idx}] dir={d}")

                # 分析这个 segseg 中的线段
                segs_in = []
                for j, seg in enumerate(kl.seg_list):
                    sb = seg.get_begin_klu().idx
                    se = seg.get_end_klu().idx
                    if sb >= ss.get_begin_klu().idx and se <= end_idx:
                        segs_in.append((j, sb, se, "UP" if seg.dir == BI_DIR.UP else "DOWN"))
                print(f"    包含 {len(segs_in)} 个线段:")
                for j, sb, se, d in segs_in:
                    print(f"      Seg[{j}]: K[{sb}]-K[{se}] dir={d}")


if __name__ == "__main__":
    main()
