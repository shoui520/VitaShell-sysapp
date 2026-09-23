#ifndef VITASHELL_SYSAPP_H
#define VITASHELL_SYSAPP_H

/* SELF ceilings include application code, modules and system allocations. */
#define SYSAPP_MEMORY_LIMIT (74U * 1024 * 1024)
#define SYSAPP_USER_LIMIT (48U * 1024 * 1024)
#define SYSAPP_PHYCONT_LIMIT (26U * 1024 * 1024)
#define SYSAPP_MEMBLOCK_LIMIT (32U * 1024 * 1024)
#define SYSAPP_PHYCONT_BLOCK_LIMIT (24U * 1024 * 1024)
#define SYSAPP_MEMORY_RESERVE (4U * 1024 * 1024)
#define SYSAPP_MAX_LIST_ENTRIES 4096

void sysappCheckRuntime(void);
void sysappFinishStartup(void);

#endif
