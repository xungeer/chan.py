import datetime
from Chan import CChan
from ChanConfig import CChanConfig
from Common.CEnum import AUTYPE, DATA_SRC, KL_TYPE
from Plot.AnimatePlotDriver import CAnimateDriver
from Plot.PlotDriver import CPlotDriver

if __name__ == "__main__":
    code = ["sh.000688", "sh.000001", "sz.399006", "sh.880008"]

    # code = ["sh.688205"]
    begin_time = "2025-11-21"
    end_time = None
    data_src = DATA_SRC.TDX_LOCAL
    # lv_list = [KL_TYPE.K_30M, KL_TYPE.K_5M, KL_TYPE.K_1M]
    lv_list = [KL_TYPE.K_DAY, KL_TYPE.K_30M, KL_TYPE.K_5M]

    config_dict = {
        "zs_combine": True,
        "zs_combine_mode": 'peak',
        "bi_algo": "fx",
        "bi_strict": True,
        "bi_fx_check": 'loss',
        "trigger_step": False,
        "skip_step": 0,
        "divergence_rate": float("inf"),
        "bsp2_follow_1": False,
        "bsp3_follow_1": False,
        "min_zs_cnt": 0,
        "bs1_peak": False,
        "macd_algo": "peak",
        "bs_type": '1,2,3a,1p,2s,3b',
        "print_warning": True,
        "zs_algo": "auto",
    }
    config = CChanConfig(config_dict)

    plot_config = {
        "plot_kline": True,
        "plot_kline_combine": True,
        "plot_bi": True,
        "plot_seg": True,
        "plot_eigen": False,
        "plot_zs": True,
        "plot_macd": False,
        "plot_mean": False,
        "plot_channel": False,
        "plot_bsp": True,
        "plot_extrainfo": False,
        "plot_demark": False,
        "plot_marker": False,
        "plot_rsi": False,
        "plot_kdj": False,
    }

    plot_para = {
        "seg": {
            "plot_trendline": False,
        },
        "klc": {
            "plot_single_kl": False,
        },
        "bi": {
            # "show_num": True,
            # "disp_end": True,
        },
        "figure": {
            "w":6000,
            "h":1000,
            "x_range": 2400,
        },
        "marker": {
            # "markers": {  # text, position, color
            #     '2023/06/01': ('marker here', 'up', 'red'),
            #     '2023/06/08': ('marker here', 'down')
            # },
        }
    }

    for c in code:
        chan = CChan(
            code=c,
            begin_time=begin_time,
            end_time=end_time,
            data_src=data_src,
            lv_list=lv_list,
            config=config,
            autype=AUTYPE.QFQ,
        )

        if not config.trigger_step:
            plot_driver = CPlotDriver(
                chan,
                plot_config=plot_config,
                plot_para=plot_para,
            )
            plot_driver.figure.show()
            
            end_time_str = end_time if end_time else datetime.date.today().strftime('%Y%m%d')
            lv_list_str = "_".join([lv.name.split('_')[-1] for lv in lv_list])
            # 确保这里使用的是定义在最上方的那个字典变量
            para_str = f"{config_dict.get('bi_algo', 'default')}_{config_dict.get('zs_algo', 'default')}"
            file_name = f"./{c}_{begin_time}_{end_time_str}_{lv_list_str}_{para_str}.png"
            plot_driver.save2img(file_name)
        else:
            CAnimateDriver(
                chan,
                plot_config=plot_config,
                plot_para=plot_para,
            )
