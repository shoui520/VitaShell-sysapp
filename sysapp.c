/* VitaShell Sys: process-wide resource policy, also applied to static libraries. */
#include <vitasdk.h>
#include <stdint.h>
#include <string.h>
#include "sysapp.h"
#include "vitashell_error.h"

static struct { SceUID uid; SceSize size; int phycont; } blocks[512];
static unsigned int block_bytes, phycont_bytes;
static volatile int block_lock;
static int startup = 1;

static void lockBlocks(void) {
  while (__sync_lock_test_and_set(&block_lock, 1))
    sceKernelDelayThread(100);
}

static void unlockBlocks(void) {
  __sync_lock_release(&block_lock);
}

void sysappCheckRuntime(void) {
  SceAppMgrBudgetInfo budget = { .size = sizeof(budget) };
  if (sceAppMgrGetBudgetInfo(&budget) < 0 ||
      budget.total_user_rw_mem > SYSAPP_USER_LIMIT ||
      budget.total_phycont_mem > SYSAPP_PHYCONT_LIMIT ||
      sceKernelGetThreadCpuAffinityMask(0) != SCE_KERNEL_CPU_MASK_SYSTEM)
    sceKernelExitProcess(VITASHELL_ERROR_INVALID_ARGUMENT);
}

void sysappFinishStartup(void) { startup = 0; }

SceUID __real_sceKernelCreateThread(const char *, SceKernelThreadEntry, int,
                                   SceSize, SceUInt, int, const SceKernelThreadOptParam *);
SceUID __wrap_sceKernelCreateThread(const char *name, SceKernelThreadEntry entry,
    int priority, SceSize stack, SceUInt attr, int affinity,
    const SceKernelThreadOptParam *option) {
  /* Ordinary application's 0x40/0xBF priorities and CPU0-2 are not available. */
  if (priority < 0x10000000) priority = 0x10000100;
  if (stack > 256 * 1024) stack = 256 * 1024;
  return __real_sceKernelCreateThread(name, entry, priority, stack, attr,
                                     SCE_KERNEL_CPU_MASK_SYSTEM, option);
}

int __real_sceKernelChangeThreadCpuAffinityMask(SceUID, int);
int __wrap_sceKernelChangeThreadCpuAffinityMask(SceUID thread, int mask) {
  return __real_sceKernelChangeThreadCpuAffinityMask(thread, SCE_KERNEL_CPU_MASK_SYSTEM);
}

SceUID __real_sceKernelAllocMemBlock(const char *, SceKernelMemBlockType, SceSize,
                                    SceKernelAllocMemBlockOpt *);
SceUID __wrap_sceKernelAllocMemBlock(const char *name, SceKernelMemBlockType type,
                                    SceSize size, SceKernelAllocMemBlockOpt *opt) {
  SceUID uid = VITASHELL_ERROR_NO_MEMORY;
  /* Avoid wasting a full 1 MiB PHYCONT page on each small icon. GXM's
     larger gpu_mem blocks benefit from the separately reserved pool. */
  if (name && !strcmp(name, "gpu_mem") && size >= 256 * 1024 && !opt) {
    if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_RW)
      type = SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW;
    else if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE)
      type = SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW;
  }
  int phycont = type == SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW ||
                type == SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW;
  /* CDRAM belongs to the game; never fall back into that pool. */
  if (!phycont && type != SCE_KERNEL_MEMBLOCK_TYPE_USER_RW &&
      type != SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE) goto failed;
  unsigned int limit = phycont ? SYSAPP_PHYCONT_BLOCK_LIMIT : SYSAPP_MEMBLOCK_LIMIT;
  unsigned int alignment = phycont ? 1024 * 1024 : 4096;
  if (!size || size > limit) goto failed;
  SceSize charged = (size + alignment - 1) & ~(alignment - 1);
  if (phycont) size = charged;
  lockBlocks();
  SceAppMgrBudgetInfo budget = { .size = sizeof(budget) };
  unsigned int used = phycont ? phycont_bytes : block_bytes;
  int budget_result = sceAppMgrGetBudgetInfo(&budget);
  unsigned int available = phycont ? budget.free_phycont_mem : budget.free_user_rw;
  unsigned int reserve = phycont ? 2 * 1024 * 1024 : SYSAPP_MEMORY_RESERVE;
  if (charged <= limit - used && budget_result >= 0 &&
      available >= reserve && charged <= available - reserve) {
    for (unsigned int i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i) {
      if (!blocks[i].size) {
        uid = __real_sceKernelAllocMemBlock(name, type, size, opt);
        if (uid >= 0) {
          blocks[i].uid = uid;
          blocks[i].size = charged;
          blocks[i].phycont = phycont;
          if (phycont) phycont_bytes += charged; else block_bytes += charged;
        }
        break;
      }
    }
  }
  unlockBlocks();
failed:
  /* Graphics startup in libvita2d assumes allocations succeed. Stop before
     it can dereference a failed allocation; runtime callers handle NULL. */
  if (uid < 0 && startup) sceKernelExitProcess(uid);
  return uid;
}

int __real_sceKernelFreeMemBlock(SceUID);
int __wrap_sceKernelFreeMemBlock(SceUID uid) {
  lockBlocks();
  int result = __real_sceKernelFreeMemBlock(uid);
  if (result >= 0) {
    for (unsigned int i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i) {
      if (blocks[i].size && blocks[i].uid == uid) {
        if (blocks[i].phycont) phycont_bytes -= blocks[i].size;
        else block_bytes -= blocks[i].size;
        blocks[i].size = 0;
        break;
      }
    }
  }
  unlockBlocks();
  return result;
}
