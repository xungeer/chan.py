import pandas as pd
from mootdx.reader import Reader
from Common.CEnum import DATA_FIELD, KL_TYPE, AUTYPE
from Common.ChanException import CChanException, ErrCode
from Common.CTime import CTime
from KLine.KLine_Unit import CKLine_Unit
from .CommonStockAPI import CCommonStockApi

class CTdxLocalAPI(CCommonStockApi):
    # 【配置】通达信数据根目录
    TDX_DIR = r'C:\iVST\new_tdx'
    
    _reader = None

    def __init__(self, code, k_type=KL_TYPE.K_DAY, begin_date=None, end_date=None, autype=AUTYPE.QFQ, tdx_dir=None):
        if tdx_dir:
            self.TDX_DIR = tdx_dir
        super(CTdxLocalAPI, self).__init__(code, k_type, begin_date, end_date, autype)

    @classmethod
    def do_init(cls):
        if cls._reader is None:
            import os
            if not os.path.exists(cls.TDX_DIR):
                raise CChanException(f"通达信目录不存在: {cls.TDX_DIR}", ErrCode.ENV_ERR)
            try:
                cls._reader = Reader.factory(market='std', tdxdir=cls.TDX_DIR)
            except Exception as e:
                raise CChanException(f"初始化通达信读取器失败: {e}", ErrCode.ENV_ERR)

    @classmethod
    def do_close(cls):
        cls._reader = None

    def get_kl_data(self):
        if self._reader is None:
            self.do_init()

        symbol = self.code.replace('.', '')
        
        # 1. 确定基础数据来源 (1m, 5m, day)
        source_k_type = self._get_source_k_type(self.k_type)
        
        # 2. 读取基础数据
        data = None
        try:
            if source_k_type == KL_TYPE.K_DAY:
                data = self._reader.daily(symbol=symbol)
            elif source_k_type == KL_TYPE.K_1M:
                data = self._reader.minute(symbol=symbol)
            elif source_k_type == KL_TYPE.K_5M:
                data = self._reader.fzline(symbol=symbol)
            else:
                raise CChanException(f"不支持的数据周期: {self.k_type}", ErrCode.PARA_ERROR)
        except Exception as e:
            raise CChanException(f"读取通达信数据失败 code={symbol}: {e}", ErrCode.SRC_DATA_NOT_FOUND)

        if data is None or data.empty:
            return

        # 3. 数据清洗与索引处理
        # 重置索引，确保 'date'/'datetime' 是一列而不是 Index
        data = data.reset_index() 
        
        # 寻找并标准化时间列名为 'date'
        time_col = self._find_time_column(data)
        if time_col != 'date':
            data.rename(columns={time_col: 'date'}, inplace=True)
        
        # 确保 date 列是 datetime 类型
        data['date'] = pd.to_datetime(data['date'])

        # 4. 如果目标周期不等于源周期，进行重采样合成
        if self.k_type != source_k_type:
            data = self._resample_data(data, self.k_type)

        # 5. 预处理过滤时间 (转为 CTime 比较)
        filter_begin = self._parse_date_filter(self.begin_date)
        filter_end = self._parse_date_filter(self.end_date)

        # 6. 生成结果
        for index, row in data.iterrows():
            # 时间转换
            t = row['date']
            if self.k_type in [KL_TYPE.K_DAY, KL_TYPE.K_WEEK, KL_TYPE.K_MON, KL_TYPE.K_QUARTER, KL_TYPE.K_YEAR]:
                k_time = CTime(t.year, t.month, t.day, 0, 0)
            else:
                k_time = CTime(t.year, t.month, t.day, t.hour, t.minute)

            # 过滤
            if filter_begin is not None and k_time < filter_begin:
                continue
            if filter_end is not None and k_time > filter_end:
                continue

            # 构造单元
            item_dict = {
                DATA_FIELD.FIELD_TIME: k_time,
                DATA_FIELD.FIELD_OPEN: float(row['open']),
                DATA_FIELD.FIELD_HIGH: float(row['high']),
                DATA_FIELD.FIELD_LOW: float(row['low']),
                DATA_FIELD.FIELD_CLOSE: float(row['close']),
                DATA_FIELD.FIELD_VOLUME: float(row['volume']),
                DATA_FIELD.FIELD_TURNOVER: float(row['amount']) if 'amount' in row else 0.0
            }
            yield CKLine_Unit(item_dict)

    def _get_source_k_type(self, target_type):
        """根据目标周期返回需要读取的通达信基础文件类型"""
        if target_type in [KL_TYPE.K_1M, KL_TYPE.K_3M]:
            return KL_TYPE.K_1M
        elif target_type in [KL_TYPE.K_5M, KL_TYPE.K_15M, KL_TYPE.K_30M, KL_TYPE.K_60M]:
            return KL_TYPE.K_5M
        else:
            # 日、周、月、季、年 都从日线合成
            return KL_TYPE.K_DAY

    def _find_time_column(self, df):
        """寻找时间列"""
        possible_cols = ['date', 'datetime', 'time', 'timestamp', 'index']
        for col in df.columns:
            if str(col).lower() in possible_cols:
                return col
        raise CChanException(f"无法找到时间列: {df.columns}", ErrCode.SRC_DATA_FORMAT_ERROR)

    def _parse_date_filter(self, date_str):
        """将字符串日期转换为 CTime 用于比较"""
        if not date_str:
            return None
        try:
            t = pd.to_datetime(date_str)
            return CTime(t.year, t.month, t.day, t.hour, t.minute)
        except:
            return None

    def _resample_data(self, df, target_type):
        """利用 Pandas resample 合成K线"""
        # 设置时间索引
        df.set_index('date', inplace=True)
        
        # 定义周期规则 (Pandas Offset Aliases)
        rule_map = {
            KL_TYPE.K_3M: '3min',
            KL_TYPE.K_15M: '15min',
            KL_TYPE.K_30M: '30min',
            KL_TYPE.K_60M: '60min',
            KL_TYPE.K_WEEK: 'W-FRI', # 周线，周五结束
            KL_TYPE.K_MON: 'ME',      # 月末 (Pandas新版用ME，旧版用M)
            KL_TYPE.K_QUARTER: 'QE',  # 季末
            KL_TYPE.K_YEAR: 'YE',     # 年末
        }
        
        rule = rule_map.get(target_type)
        if not rule:
            # 尝试兼容旧版 Pandas 'M', 'Q', 'Y'
            if target_type == KL_TYPE.K_MON: rule = 'M'
            elif target_type == KL_TYPE.K_QUARTER: rule = 'Q'
            elif target_type == KL_TYPE.K_YEAR: rule = 'A' # Annual
            else:
                 raise CChanException(f"无法重采样到周期: {target_type}", ErrCode.PARA_ERROR)

        # 定义聚合逻辑
        agg_dict = {
            'open': 'first',
            'high': 'max',
            'low': 'min',
            'close': 'last',
            'volume': 'sum',
            'amount': 'sum'
        }
        
        # 分钟线通常是“右闭”区间（例如 09:35 的K线代表 09:30-09:35）
        # 日线合成通常直接按日历
        is_intraday = target_type in [KL_TYPE.K_3M, KL_TYPE.K_15M, KL_TYPE.K_30M, KL_TYPE.K_60M]
        
        try:
            if is_intraday:
                # 分钟线合成：label='right', closed='right' 
                # 比如 5分钟数据：09:35, 09:40, 09:45 -> 合成15分钟 -> 应该标记为 09:45
                resampled = df.resample(rule, closed='right', label='right').agg(agg_dict)
            else:
                # 日线合成
                resampled = df.resample(rule).agg(agg_dict)
        except Exception as e:
             # 如果列名大小写不一致（如 Volume vs volume），尝试修正 agg_dict
             # 这里简单处理，假设清洗阶段已经统一了列名大小写，或者直接跳过
             raise CChanException(f"重采样失败: {e}", ErrCode.SRC_DATA_FORMAT_ERROR)

        # 去除空值（例如停牌期间或非交易时间段）
        resampled.dropna(inplace=True)
        
        # 重置索引，让 date 变回一列
        return resampled.reset_index()

    def SetBasciInfo(self):
        self.name = self.code
        self.is_stock = True