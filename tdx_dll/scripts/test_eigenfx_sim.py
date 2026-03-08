"""
test_eigenfx_sim.py - 模拟 Python 和 DLL 在线段级别的特征序列检测
比较 K[97]-K[672] 区间的特征序列形成过程
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
    print("模拟特征序列检测 - K[97]-K[672] 区间")
    print("=" * 70)

    # Python segseg[1] ends at K[97] (DOWN), segseg[2] starts at K[97] (UP to K[672])
    # After segseg[1], the detector starts looking for UP segments from K[97]
    # For UP segments, it looks at DOWN direction "bi" (segments) in the eigenFX
    
    # The seg_list from Seg[7] (ends K[97]) to Seg[24+] (to K[672])
    # segAsBi indices: Seg[7] end = segBiPoint[8] (index 8 in segBiPoints)
    # Seg[8] = UP K[97]-K[107] = "笔8" in segBiPoints
    # For UP eigenFX, we add DOWN segments (odd indices from the start of this region)

    # Let's print the "segAsBi" sequence from Seg[7] onwards
    print("\n  线段列表(作为虚拟笔) Seg[7] 开始:")
    for i in range(7, 30):
        seg = kl.seg_list[i]
        d = "UP" if seg.dir == BI_DIR.UP else "DN"
        begin_val = seg.get_begin_val()
        end_val = seg.get_end_val()
        h = seg._high()
        l = seg._low()
        # In the virtual "bi" index space, this seg corresponds to biIdx = i
        # (because segBiPoints[0] = first seg start, segBiPoints[1] = seg[0] end, ...)
        # So biIdx = i+1 (seg[i] end is segBiPoints[i+1])
        # 
        # But more importantly, the "bi" from segBiPoints[i] to segBiPoints[i+1] 
        # corresponds to seg_list[i]
        print(f"  Seg[{i:2d}] {d}: K[{seg.get_begin_klu().idx:4d}]-K[{seg.get_end_klu().idx:4d}] "
              f"h={h:9.2f} l={l:9.2f} "
              f"begin={begin_val:9.2f} end={end_val:9.2f}")

    # Now simulate the eigenFX for UP direction starting after segseg[1]
    # UP eigenFX adds DOWN segments (odd segments from K[97])
    # Starting from Seg[8] (UP, K[97]-K[107])
    
    # EigenFX for UP segments: adds is_down() bi's
    # Seg[8] is UP → skip for UP eigenFX  
    # Seg[9] is DN → add to UP eigenFX
    # Seg[10] is UP → skip
    # Seg[11] is DN → add to UP eigenFX
    # ...

    print("\n\n  模拟 UP eigenFX 特征序列 (从 Seg[8] 开始):")
    print("  " + "-" * 60)

    # In Python, the second-level cal_seg_sure starts from begin_idx after segseg[1]
    # last_seg_dir = segseg[1].dir = DOWN
    # So for bi in seg_list[begin_idx:]:
    #   if bi.is_down() and last_seg_dir != UP:  → add to up_eigen
    #   elif bi.is_up() and last_seg_dir != DOWN: → NO (last_seg_dir is DOWN)
    
    # After segseg[1] (DOWN), begin_idx points to the seg after segseg[1] endpoint
    # Let's figure out what begin_idx is in the Python code
    
    # Python cal_seg_sure is called with begin_idx = end_bi_idx + 1
    # In segseg[1], end_bi_idx = segseg[1].end_bi.idx
    # segseg[1].end_bi is a seg (not a bi), so its idx is the seg index
    
    # In Python, it's:  self.last_sure_segseg_start_bi_idx = cal_seg(self.seg_list, self.segseg_list, ...)
    # cal_seg calls SegListChan.cal_seg_sure(seg_list, begin_idx)
    # When segseg[1] is detected, it restarts from: begin_idx = end_bi_idx + 1
    # end_bi_idx is the index of the ending bi (seg) in seg_list
    
    # segseg[1] = K[31]-K[97] = covers Seg[2]-Seg[7]
    # end_bi for segseg[1] = Seg[7] (the segment ending at K[97])
    # Seg[7].idx in seg_list = 7
    # So begin_idx = 7 + 1 = 8 (start from Seg[8])
    
    # last_seg_dir = segseg[1].dir = DOWN
    
    # Let's simulate:
    begin_idx = 8  # Start from Seg[8]
    last_seg_dir = -1  # DOWN (segseg[1].dir)
    
    # Eigen elements for UP detection
    ele = [None, None, None]  # (high, low, biIdxList)
    
    for i in range(begin_idx, min(begin_idx + 14, len(kl.seg_list))):
        seg = kl.seg_list[i]
        bi_dir = 1 if seg.dir == BI_DIR.UP else -1  # UP=+1, DOWN=-1
        
        h = seg._high()
        l = seg._low()
        
        d = "UP" if seg.dir == BI_DIR.UP else "DN"
        
        # UP eigenFX: adds is_down() bi's (bi_dir == -1)
        if bi_dir == -1 and last_seg_dir != 1:
            action = "→ ADD to UP eigenFX"
            # Simulate element logic
            if ele[0] is None:
                ele[0] = {'high': h, 'low': l, 'idx': i}
                action += f" [ele0: h={h:.2f} l={l:.2f}]"
            elif ele[1] is None:
                # test_combine with ele[0]
                e0 = ele[0]
                if e0['high'] >= h and e0['low'] <= l:
                    action += " → COMBINE with ele0 (cur contains new)"
                    # Actually combine (UP dir → take higher high, higher low)
                elif h >= e0['high'] and l >= e0['low']:
                    action += f" → UP → ele1: h={h:.2f} l={l:.2f}"
                    ele[1] = {'high': h, 'low': l, 'idx': i}
                    # Check: if UP eigenFX and ele[1].high < ele[0].high → reset
                    if ele[1]['high'] < ele[0]['high']:
                        action += " → RESET (ele1.high < ele0.high)"
                        ele = [None, None, None]
                elif h <= e0['high'] and l <= e0['low']:
                    action += f" → DOWN → ele1: h={h:.2f} l={l:.2f}"
                    ele[1] = {'high': h, 'low': l, 'idx': i}
                    if ele[1]['high'] < ele[0]['high']:
                        action += " → RESET (ele1.high < ele0.high)"
                        ele = [None, None, None]
                else:
                    action += f" → ele1: h={h:.2f} l={l:.2f}"
                    ele[1] = {'high': h, 'low': l, 'idx': i}
            elif ele[2] is None:
                e1 = ele[1]
                # test_combine with ele[1]
                if e1['high'] >= h and e1['low'] <= l:
                    action += " → COMBINE with ele1"
                elif h > e1['high']:
                    action += f" → UP out of ele1 → ele2: h={h:.2f} l={l:.2f}"
                    ele[2] = {'high': h, 'low': l, 'idx': i}
                elif l < e1['low']:
                    action += f" → DOWN out of ele1 → ele2: h={h:.2f} l={l:.2f}"
                    ele[2] = {'high': h, 'low': l, 'idx': i}
                else:
                    action += " → COMBINE/other"
                    
                if ele[2] is not None:
                    # Check updateFx: TOP if pre.high < mid.high and nxt.high <= mid.high and nxt.low < mid.low
                    e0, e1, e2 = ele[0], ele[1], ele[2]
                    if e0['high'] < e1['high'] and e2['high'] <= e1['high'] and e2['low'] < e1['low']:
                        action += " → **TOP FX DETECTED** → seg end!"
                    else:
                        action += " → no TOP fx → would reset"
        else:
            action = f"  (skip for UP eigenFX: bi_dir={bi_dir}, last_seg_dir={last_seg_dir})"
            
        print(f"  Seg[{i:2d}] {d}: K[{seg.get_begin_klu().idx:4d}]-K[{seg.get_end_klu().idx:4d}] "
              f"h={h:7.2f} l={l:7.2f}  {action}")


if __name__ == "__main__":
    main()
