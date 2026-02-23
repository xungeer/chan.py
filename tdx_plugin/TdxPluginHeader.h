/*****************************************************************************
 * chan2026.dll - 缠论通达信插件
 * 基于 chan.py 项目，参考 indicator 项目接口
 *
 * 通达信插件接口头文件
 *****************************************************************************/

#ifndef __TDX_PLUGIN_HEADER_H__
#define __TDX_PLUGIN_HEADER_H__

#include <windows.h>

#ifndef DECLSPEC_EXPORT
#define DECLSPEC_EXPORT __declspec(dllexport)
#endif

#pragma pack(push, 1)


// 插件函数指针类型 (参数: 数据数量, 输出, 参数a, 参数b, 参数c)
typedef void (*pPluginFUNC)(int nCount, float *pOut, float *a, float *b, float *c);

// 插件函数注册信息
typedef struct tagPluginTCalcFuncInfo
{
    unsigned short nFuncMark; // 函数编号
    pPluginFUNC    pCallFunc; // 函数地址
} PluginTCalcFuncInfo;

#ifdef __cplusplus
extern "C" {
#endif

DECLSPEC_EXPORT BOOL RegisterTdxFunc(PluginTCalcFuncInfo **pInfo);

#ifdef __cplusplus
};
#endif

#pragma pack(pop)
#endif
