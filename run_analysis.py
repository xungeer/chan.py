import os
import sys
import pandas as pd
from pytdx.reader import TdxMinBarReader

# --- 1. 引入 chan.py 库 ---
sys.path.append(os.path.abspath(".")) 

try:
    from Chan import CChan
    from ChanConfig import CChanConfig
    from Common.CEnum import AUTYPE, DATA_SRC, KL_TYPE
except ImportError as e:
    print("错误: 未找到 chan.py 核心文件。请确保本脚本在 chan.py 项目根目录下运行。")
    sys.exit(1)

# --- 2. 数据读取与处理 ---

def load_data_safe(tdx_path, csv_path=None):
    df = None
    
    # A. 尝试读取 TDX
    if os.path.exists(tdx_path):
        print(f"[INFO] 尝试读取 TDX 文件: {tdx_path}")
        try:
            reader = TdxMinBarReader()
            df_tdx = reader.get_df(tdx_path)
            
            if df_tdx is not None and not df_tdx.empty:
                df_tdx.columns = [c.lower() for c in df_tdx.columns]
                
                # 检查索引是否包含时间信息
                if 'date' not in df_tdx.columns:
                    if isinstance(df_tdx.index, pd.DatetimeIndex) or 'date' in df_tdx.index.names:
                        print("[DEBUG] 检测到时间在索引中，正在重置索引...")
                        df_tdx.reset_index(inplace=True)
                        # 重置后第一列通常是 index，改名为 date
                        if 'date' not in df_tdx.columns: 
                            df_tdx.rename(columns={df_tdx.columns[0]: 'date'}, inplace=True)
                
                if 'date' in df_tdx.columns:
                    print(f"[INFO] TDX 文件读取成功。列名: {list(df_tdx.columns)}")
                    df = df_tdx
                else:
                    print(f"[WARN] TDX 数据缺失 date 列，将尝试 CSV。")
        except Exception as e:
            print(f"[ERROR] 读取 TDX 出错: {e}")

    # B. 回退到 CSV
    if df is None and csv_path and os.path.exists(csv_path):
        print(f"[INFO] 切换读取备份 CSV 文件: {csv_path}")
        df = pd.read_csv(csv_path)
        df.columns = [c.lower() for c in df.columns]

    if df is None:
        raise ValueError("无法获取有效数据。")

    # 数据清洗
    df['datetime'] = pd.to_datetime(df['date'])
    df.set_index('datetime', inplace=True)
    
    numeric_cols = ['open', 'high', 'low', 'close', 'volume']
    for col in numeric_cols:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors='coerce')
            
    return df[numeric_cols].dropna()

def process_to_5min(df_1m):
    print("[INFO] 正在重采样: 1分钟 -> 5分钟...")
    df_5m = df_1m.resample('5min', closed='left', label='left').agg({
        'open': 'first',
        'high': 'max',
        'low': 'min',
        'close': 'last',
        'volume': 'sum'
    })
    df_5m.dropna(inplace=True)
    df_5m = df_5m[df_5m['volume'] > 0]
    return df_5m

# --- 3. 主程序 ---

if __name__ == "__main__":
    # 配置路径
    TDX_FILE = r"C:\iVST\new_tdx\vipdoc\sh\fzline\sh510300.lc5"
    CSV_FILE = "./data\csv\sh510300_lc1.csv" 
    STOCK_CODE = "sh.999999"
    
    # 准备数据目录
    DATA_DIR = os.path.join(os.path.dirname(__file__), "data", "csv")
    if not os.path.exists(DATA_DIR):
        os.makedirs(DATA_DIR)
    
    TARGET_CHAN_CSV = os.path.join(DATA_DIR, f"{STOCK_CODE}.csv")

    try:
        # 1. 准备数据
        df_raw = load_data_safe(TDX_FILE, CSV_FILE)
        df_5m = df_raw
        
        export_df = df_5m.copy()
        export_df['code'] = STOCK_CODE
        export_df.reset_index(inplace=True)
        export_df.rename(columns={'datetime': 'time'}, inplace=True)
        
        print(f"[INFO] 保存中间数据到: {TARGET_CHAN_CSV}")
        export_df.to_csv(TARGET_CHAN_CSV, index=False)

        # 2. 配置 Chan.py (修正部分)
        print(f"[INFO] 启动缠论计算与绘图...")
        
        # === 修正点：移除 kl_data_src，只保留策略参数 ===
        conf = CChanConfig({
            "bi_algo": "normal",  # 笔生成算法
            "zs_algo": "normal",  # 中枢生成算法
            "trigger_step": False, 
            # 注意：plot_* 参数如果报错，也可以尝试移除，默认为开启
            # 如果不需要特定的买卖点配置，甚至可以传空字典 CChanConfig()
        })

        # 3. 实例化并运行
        chan = CChan(
            code=STOCK_CODE,
            data_src=DATA_SRC.CSV,  # 数据源在这里指定
            lv_list=[KL_TYPE.K_5M], # 级别
            config=conf
        )

        # 4. 绘图
        chan.plot()
        print("[SUCCESS] 绘图完成！")

    except Exception as e:
        print("\n" + "="*40)
        print("运行失败:")
        import traceback
        traceback.print_exc()
        print("="*40)