#ifndef VITASHELL_SYSAPP_IMAGE_H
#define VITASHELL_SYSAPP_IMAGE_H
#include <stddef.h>
#include <vita2d.h>
vita2d_texture *sysappLoadImageBuffer(const void *data, size_t size);
vita2d_texture *sysappLoadImageFile(const char *path);
#endif
