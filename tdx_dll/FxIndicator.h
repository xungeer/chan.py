/*****************************************************************************
 * chan2026 - 缠论可视化分析系统 (通达信DLL接口)
 * 基于 chan.py 核心算法
 *****************************************************************************/

#ifndef __FXIndicator_h__
#define __FXIndicator_h__
#pragma pack(push,1)

#include <windows.h>

#ifndef DECLSPEC_EXPORT
#define DECLSPEC_EXPORT __declspec(dllexport)
#endif

// 函数原型(数据个数, 输出, 输入a, 输入b, 输入c)
typedef void(*pPluginFUNC)(int nCount, float *pOut, float *a, float *b, float *c);

typedef struct tagPluginTCalcFuncInfo
{
  unsigned short nFuncMark; // 函数编号
  pPluginFUNC    pCallFunc; // 函数地址
} PluginTCalcFuncInfo;

#ifdef __cplusplus
extern "C" {
#endif
DECLSPEC_EXPORT  BOOL RegisterTdxFunc(PluginTCalcFuncInfo **pInfo);
#ifdef __cplusplus
};
#endif

#pragma pack(pop)
#endif
