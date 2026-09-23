#ifndef SYSAPP_TEST_VITA2D_H
#define SYSAPP_TEST_VITA2D_H
#include <stddef.h>
typedef struct { unsigned int width, height, stride; void *pixels; } vita2d_texture;
#define SCE_GXM_TEXTURE_FORMAT_U8U8U8_BGR 3
vita2d_texture *vita2d_create_empty_texture(unsigned int, unsigned int);
vita2d_texture *vita2d_create_empty_texture_format(unsigned int, unsigned int, int);
void vita2d_free_texture(vita2d_texture *);
void *vita2d_texture_get_datap(const vita2d_texture *);
unsigned int vita2d_texture_get_stride(const vita2d_texture *);
#endif
