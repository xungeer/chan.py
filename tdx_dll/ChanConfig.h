/*****************************************************************************
 * chan2026 - 缠论配置参数头文件
 *
 * 对应 Python 端 main.py 中的 config_dict{}
 * 用户自定义参数后，重新编译 DLL 即生效
 *
 * 使用方法：
 *   1. 根据需要修改下方参数值
 *   2. 重新编译: make clean && make
 *   3. 将生成的 chan2026.dll 放入通达信 T0002/dlls/ 目录
 *****************************************************************************/

#ifndef __CHAN_CONFIG_H__
#define __CHAN_CONFIG_H__

//=============================================================================
// 一、笔(Bi)相关配置
// 对应 Python: CBiConfig (Bi/BiConfig.py)
//=============================================================================

// bi_algo: 笔识别算法
//   0 = "normal" - 普通算法，要求笔至少跨越5根合并K线
//   1 = "fx"     - 分型算法，只要形成有效分型即可成笔（不检查跨度）
//
// 示例: main.py 中 "bi_algo": "fx" 对应 BI_ALGO = 1
#define BI_ALGO  1

// bi_strict: 笔是否严格模式
//   true  = 严格模式: 笔的跨度要求 >= 4 根合并K线
//   false = 宽松模式: 跨度 >= 3 且中间至少3根独立K线
//
// 注意: 仅当 BI_ALGO = 0 ("normal") 时此参数才生效
//       BI_ALGO = 1 ("fx") 时跳过跨度检查
#define BI_STRICT  true

// bi_fx_check: 分型有效性检查模式
//   0 = "strict"  - 严格检查: 考虑分型三元素(前/中/后)的所有K线
//   1 = "loss"    - 宽松检查: 只检查顶底分型K线本身
//   2 = "half"    - 半严格:   检查分型前2个K线元素
//   3 = "totally" - 完全严格: 要求分型K线之间完全不重叠
//
// 示例: main.py 中 "bi_fx_check": "loss" 对应 BI_FX_CHECK = 1
#define BI_FX_CHECK  1

// bi_end_is_peak: 笔端点是否必须是区间极值
//   true  = 笔的端点必须是从起点到终点之间的最高/最低点
//   false = 不检查此条件
#define BI_END_IS_PEAK  true

// bi_allow_sub_peak: 是否允许次高/次低点更新笔端点
//   true  = 允许（更灵活的笔更新逻辑）
//   false = 不允许，启用 update_peak 逻辑
#define BI_ALLOW_SUB_PEAK  true

// gap_as_kl: 跳空缺口是否视为独立K线（增加跨度计数）
//   true  = 跳空缺口计入跨度
//   false = 不计入
#define GAP_AS_KL  false

//=============================================================================
// 二、中枢(ZS)相关配置
// 对应 Python: CZSConfig (ZS/ZSConfig.py)
//=============================================================================

// zs_combine: 是否进行中枢合并
//   true  = 合并相邻且有重叠的中枢
//   false = 不合并
#define ZS_COMBINE  true

// zs_combine_mode: 中枢合并模式
//   0 = "zs"   - 标准合并: 使用中枢的高低区间判断重叠
//   1 = "peak" - 峰值合并: 使用中枢内笔的最高/最低点判断重叠
//
// 示例: main.py 中 "zs_combine_mode": "peak" 对应 ZS_COMBINE_MODE = 1
#define ZS_COMBINE_MODE  1

// one_bi_zs: 是否允许单笔中枢
//   true  = 允许（极端行情下一笔也能构成中枢）
//   false = 不允许（标准定义至少3笔）
#define ONE_BI_ZS  false

// zs_algo: 中枢构建算法
//   0 = "normal"   - 普通算法: 标准中枢定义（按线段分段计算）
//   1 = "over_seg" - 跨线段算法: 中枢可以跨越线段边界
//   2 = "auto"     - 自动算法: 已确认线段用normal, 未确认线段用over_seg
//
// 示例: main.py 中 "zs_algo": "auto" 对应 ZS_ALGO = 2
#define ZS_ALGO  2

//=============================================================================
// 2.5、线段(Seg)相关配置
// 对应 Python: CSegConfig (Seg/SegConfig.py)
//=============================================================================

// left_seg_method: 线段残留部分处理方法
//   0 = "peak" - 峰值法: 在剩余笔中寻找峰值作为虚线段端点（默认）
//   1 = "all"  - 全包法: 将剩余笔全部归入一个线段
//
// 示例: main.py 中 "left_method": "peak" 对应 LEFT_SEG_METHOD = 0
#define LEFT_SEG_METHOD  0

//=============================================================================
// 三、买卖点(BSP)相关配置
// 对应 Python: CBSPointConfig (BuySellPoint/BSPointConfig.py)
//=============================================================================

// divergence_rate: 背驰比率阈值
//   当笔的MACD面积之比超过此值时，不认定为背驰
//   设为很大的值(如1e10)表示不限制
//   设为1.0表示严格要求后段MACD面积小于前段
//
// 示例: main.py 中 "divergence_rate": float("inf") 对应 DIVERGENCE_RATE = 1e10f
#define DIVERGENCE_RATE  1e10f

// min_zs_cnt: 买卖点判断所需最小中枢数量
//   0 = 不限制中枢数量
//   1 = 至少1个中枢后才判断买卖点（默认）
//   n = 至少n个中枢
//
// 示例: main.py 中 "min_zs_cnt": 0 对应 MIN_ZS_CNT = 0
#define MIN_ZS_CNT  0

// bsp2_follow_1: 二类买卖点是否必须跟随一类买卖点
//   true  = 必须在一类买卖点之后才能出现二类买卖点
//   false = 独立判断，无需依赖一类买卖点
//
// 示例: main.py 中 "bsp2_follow_1": False 对应 BSP2_FOLLOW_1 = false
#define BSP2_FOLLOW_1  false

// bsp3_follow_1: 三类买卖点是否必须跟随一类买卖点
//   true  = 必须在一类买卖点之后才能出现三类买卖点
//   false = 独立判断
//
// 示例: main.py 中 "bsp3_follow_1": False 对应 BSP3_FOLLOW_1 = false
#define BSP3_FOLLOW_1  false

// bs1_peak: 一类买卖点是否取极值模式
//   true  = 一买取最低点，一卖取最高点
//   false = 按标准位置判断
//
// 示例: main.py 中 "bs1_peak": False 对应 BS1_PEAK = false
#define BS1_PEAK  false

// macd_algo: MACD算法模式（用于背驰判断）
//   0 = "peak"      - 峰值模式: 比较MACD柱状图的峰值
//   1 = "area"      - 面积模式: 比较MACD柱状图的面积
//   2 = "full_area" - 完整面积模式
//   3 = "diff"      - 差值模式
//   4 = "slope"     - 斜率模式
//   5 = "amp"       - 振幅模式
//
// 注意: 在 CMACD.h 的 cal_metric() 中通过编译期分支选择算法
//
// 示例: main.py 中 "macd_algo": "peak" 对应 MACD_ALGO = 0
#define CFG_MACD_ALGO  0

// bs_type: 启用的买卖点类型（各类型独立开关）
//   BS_TYPE_1  = 一类买卖点 (趋势背驰)
//   BS_TYPE_2  = 二类买卖点 (回踩不破前低/反弹不破前高)
//   BS_TYPE_3A = 三类买卖点a (中枢在一类后面)
//   BS_TYPE_1P = 一类买卖点p (盘整背驰)
//   BS_TYPE_2S = 类二买卖点 (T2S)
//   BS_TYPE_3B = 三类b买卖点 (中枢在一类前面)
//
// 示例: main.py 中 "bs_type": "1,2,3a,1p,2s,3b" 对应全部设为 true
#define BS_TYPE_1   true
#define BS_TYPE_2   true
#define BS_TYPE_3A  true
#define BS_TYPE_1P  true
#define BS_TYPE_2S  true
#define BS_TYPE_3B  true

// bsp1_only_multibi_zs: 一类买卖点是否要求多笔中枢
//   true  = 一买一卖需要至少包含多笔的中枢（标准定义）
//   false = 允许较少笔数的中枢
#define BSP1_ONLY_MULTIBI_ZS  true

// max_bs2_rate: 二类买卖点最大回撤比率
//   0.9999 = 回踩不能超过前低/前高的99.99%
//   取值范围: 0.0 ~ 1.0
#define MAX_BS2_RATE  0.9999f

// bsp2s_follow_2: 类二买卖点是否必须跟随二类买卖点
//   true  = 必须在二类买卖点成立后才能出现类二买卖点
//   false = 即使二类不成立也继续检查类二
//
// 示例: main.py 中 "bsp2s_follow_2": False 对应 BSP2S_FOLLOW_2 = false
#define BSP2S_FOLLOW_2  false

// max_bsp2s_lv: 类二买卖点最大层级
//   0 = 不限制层级
//   n = 最多检查 n 层类二买卖点
//
// 示例: main.py 中 "max_bsp2s_lv": None 对应 MAX_BSP2S_LV = 0
#define MAX_BSP2S_LV  0

// strict_bsp3: 是否严格三类买卖点判断
//   true  = 严格模式
//   false = 标准模式
#define STRICT_BSP3  false

// bsp3_peak: 三类买卖点是否取极值
#define BSP3_PEAK  false

// bsp3a_max_zs_cnt: 三类买卖点a的最大中枢数量
#define BSP3A_MAX_ZS_CNT  1

//=============================================================================
// 四、其他配置
//=============================================================================

// print_warning: 是否输出警告信息（DLL中通过 OutputDebugString）
//   true  = 输出调试信息
//   false = 静默模式
#define PRINT_WARNING  true

#endif // __CHAN_CONFIG_H__
