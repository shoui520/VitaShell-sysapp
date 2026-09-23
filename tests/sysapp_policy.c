#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vitasdk.h>
#include "sysapp.h"

static unsigned int free_user = SYSAPP_USER_LIMIT, free_phy = SYSAPP_PHYCONT_LIMIT;
static int next_uid = 1, fail_alloc, fail_free, budget_error;
static int requested_type, requested_priority, requested_affinity;
static unsigned int requested_size, requested_stack;

int sceKernelDelayThread(unsigned int usec) { return 0; }
int sceAppMgrGetBudgetInfo(SceAppMgrBudgetInfo *info) {
  info->total_user_rw_mem = SYSAPP_USER_LIMIT;
  info->total_phycont_mem = SYSAPP_PHYCONT_LIMIT;
  info->free_user_rw = free_user;
  info->free_phycont_mem = free_phy;
  return budget_error ? -1 : 0;
}
int sceKernelGetThreadCpuAffinityMask(SceUID id) { return SCE_KERNEL_CPU_MASK_SYSTEM; }
_Noreturn void sceKernelExitProcess(int error) { abort(); }
SceUID __real_sceKernelAllocMemBlock(const char *name, SceKernelMemBlockType type,
                                    SceSize size, SceKernelAllocMemBlockOpt *opt) {
  requested_type = type;
  requested_size = size;
  return fail_alloc ? -1 : next_uid++;
}
int __real_sceKernelFreeMemBlock(SceUID uid) { return fail_free ? -1 : 0; }
SceUID __real_sceKernelCreateThread(const char *name, SceKernelThreadEntry entry,
    int priority, SceSize stack, SceUInt attr, int affinity, const SceKernelThreadOptParam *opt) {
  requested_priority = priority;
  requested_stack = stack;
  requested_affinity = affinity;
  return 1;
}
int __real_sceKernelChangeThreadCpuAffinityMask(SceUID uid, int mask) {
  requested_affinity = mask;
  return 0;
}
SceUID __wrap_sceKernelAllocMemBlock(const char *, SceKernelMemBlockType, SceSize, SceKernelAllocMemBlockOpt *);
int __wrap_sceKernelFreeMemBlock(SceUID);
SceUID __wrap_sceKernelCreateThread(const char *, SceKernelThreadEntry, int, SceSize, SceUInt, int, const SceKernelThreadOptParam *);
int __wrap_sceKernelChangeThreadCpuAffinityMask(SceUID, int);
#define alloc(type, size) __wrap_sceKernelAllocMemBlock("test", type, size, NULL)
#define RW SCE_KERNEL_MEMBLOCK_TYPE_USER_RW
int main(void) {
  sysappCheckRuntime();
  sysappFinishStartup();
  assert(alloc(RW, 0) < 0);
  assert(alloc(RW, UINT32_MAX) < 0);
  assert(alloc(99, 4096) < 0);
  SceUID full = alloc(RW, SYSAPP_MEMBLOCK_LIMIT);
  assert(full >= 0 && alloc(RW, 1) < 0);
  fail_free = 1;
  assert(__wrap_sceKernelFreeMemBlock(full) < 0 && alloc(RW, 4096) < 0);
  fail_free = 0;
  assert(__wrap_sceKernelFreeMemBlock(full) == 0);
  fail_alloc = 1;
  assert(alloc(RW, SYSAPP_MEMBLOCK_LIMIT) < 0);
  fail_alloc = 0;
  full = alloc(RW, SYSAPP_MEMBLOCK_LIMIT);
  assert(full >= 0);
  __wrap_sceKernelFreeMemBlock(full);
  free_user = SYSAPP_MEMORY_RESERVE + 4095;
  assert(alloc(RW, 1) < 0);
  free_user++;
  full = alloc(RW, 1);
  assert(full >= 0);
  __wrap_sceKernelFreeMemBlock(full);
  free_user = SYSAPP_USER_LIMIT;
  budget_error = 1;
  assert(alloc(RW, 4096) < 0);
  budget_error = 0;
  SceUID gpu = __wrap_sceKernelAllocMemBlock("gpu_mem", RW, 256 * 1024, NULL);
  assert(gpu >= 0 && requested_type == SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW);
  assert(requested_size == 1024 * 1024);
  __wrap_sceKernelFreeMemBlock(gpu);
  gpu = __wrap_sceKernelAllocMemBlock("gpu_mem", RW, 4096, NULL);
  assert(gpu >= 0 && requested_type == RW);
  __wrap_sceKernelFreeMemBlock(gpu);
  gpu = alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, SYSAPP_PHYCONT_BLOCK_LIMIT);
  assert(gpu >= 0);
  assert(alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, 1) < 0);
  full = alloc(RW, SYSAPP_MEMBLOCK_LIMIT); /* separate accounting */
  assert(full >= 0);
  __wrap_sceKernelFreeMemBlock(full);
  __wrap_sceKernelFreeMemBlock(gpu);
  free_phy = 2 * 1024 * 1024;
  assert(alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, 4096) < 0);
  free_phy = SYSAPP_PHYCONT_LIMIT;
  SceUID slots[512];
  for (unsigned int i = 0; i < 512; i++) { slots[i] = alloc(RW, 4096); assert(slots[i] >= 0); }
  assert(alloc(RW, 4096) < 0);
  for (unsigned int i = 0; i < 512; i++) __wrap_sceKernelFreeMemBlock(slots[i]);
  __wrap_sceKernelCreateThread("test", NULL, 0x40, 1024 * 1024, 0, 0x70000, NULL);
  assert(requested_affinity == SCE_KERNEL_CPU_MASK_SYSTEM);
  assert(requested_priority == 0x10000100 && requested_stack == 256 * 1024);
  __wrap_sceKernelChangeThreadCpuAffinityMask(1, 0x70000);
  assert(requested_affinity == SCE_KERNEL_CPU_MASK_SYSTEM);
  puts("sysapp resource policy: passed");
}
