/* Skip VitaShell's hardware includes while compiling the real decoder source. */
#define __MAIN_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vita2d.h>
#define BIG_BUFFER_SIZE (2 * 1024 * 1024)
int allocateReadFile(const char *, void **);
