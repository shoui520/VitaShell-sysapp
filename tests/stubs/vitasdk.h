/* Host-only declarations used to exercise sysapp.c without Vita hardware. */
#ifndef SYSAPP_TEST_VITASDK_H
#define SYSAPP_TEST_VITASDK_H
#include <stddef.h>
#include <stdint.h>
typedef int SceUID;
typedef uint32_t SceSize;
typedef unsigned int SceUInt;
typedef int SceKernelMemBlockType;
typedef int (*SceKernelThreadEntry)(SceSize, void *);
typedef struct { int unused; } SceKernelThreadOptParam;
typedef struct { int unused; } SceKernelAllocMemBlockOpt;
typedef struct {
  int size;
  unsigned int total_user_rw_mem, total_phycont_mem, free_user_rw, free_phycont_mem;
} SceAppMgrBudgetInfo;
#define SCE_KERNEL_CPU_MASK_SYSTEM 0x80000
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW 1
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE 2
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW 3
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW 4
int sceKernelDelayThread(unsigned int);
int sceAppMgrGetBudgetInfo(SceAppMgrBudgetInfo *);
int sceKernelGetThreadCpuAffinityMask(SceUID);
_Noreturn void sceKernelExitProcess(int);
#endif
