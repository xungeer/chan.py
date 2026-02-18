# 文件位置：DataAPI/__init__.py

from Common.CEnum import DATA_SRC

# ... 其他引用 ...

def CCommonStockApi(code, k_type, begin_date, end_date, data_src, autype):
    # 注意：Chan.py 要求 data_src 为字符串，所以这里需与字符串比较
    if data_src == "baostock" or data_src == DATA_SRC.BAO_STOCK.name.lower(): # 兼容字符串判断
        from .BaoStockAPI import CBaoStock
        return CBaoStock(code, k_type, begin_date, end_date, autype)
    
    elif data_src == "ccxt" or data_src == DATA_SRC.CCXT.name.lower():
        from .ccxt import CCXT
        return CCXT(code, k_type, begin_date, end_date, autype)
        
    elif data_src == "csv" or data_src == DATA_SRC.CSV.name.lower():
        from .csvAPI import CSV_API
        return CSV_API(code, k_type, begin_date, end_date, autype)

    # === 修改处：判断字符串 "tdx_local" ===
    elif data_src == "tdx_local":
        from .TdxLocalAPI import CTdxLocalAPI
        # 请确保 tdx_dir 路径正确
        return CTdxLocalAPI(code, k_type, begin_date, end_date, autype, tdx_dir=r'C:\iVST\new_tdx')
    # ===================================

    else:
        raise Exception(f"Unknown data source: {data_src}")