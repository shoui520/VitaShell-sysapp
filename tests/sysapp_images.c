#include <assert.h>
#include <png.h>
#include <jpeglib.h>
#include "sysapp_image.h"

static int textures;
static int reject_texture;
int allocateReadFile(const char *path, void **buffer) { *buffer = NULL; return -1; }
vita2d_texture *vita2d_create_empty_texture_format(unsigned int w, unsigned int h, int format) {
  if (reject_texture) return NULL;
  assert(w <= 1024 && h <= 1024 && w * h <= 512 * 1024);
  vita2d_texture *t = calloc(1, sizeof(*t));
  assert(t);
  t->width = w; t->height = h;
  t->stride = ((w + 7) & ~7) * (format == 3 ? 3 : 4);
  t->pixels = calloc(h, t->stride);
  assert(t->pixels);
  textures++;
  return t;
}
vita2d_texture *vita2d_create_empty_texture(unsigned int w, unsigned int h) {
  return vita2d_create_empty_texture_format(w, h, 4);
}
void vita2d_free_texture(vita2d_texture *t) { if (t) { free(t->pixels); free(t); textures--; } }
void *vita2d_texture_get_datap(const vita2d_texture *t) { return t->pixels; }
unsigned int vita2d_texture_get_stride(const vita2d_texture *t) { return t->stride; }

int main(void) {
  assert(!sysappLoadImageBuffer(NULL, 0));
  assert(!sysappLoadImageFile("missing"));
  unsigned char pixels[8 * 8 * 4];
  memset(pixels, 127, sizeof(pixels));
  png_image png = { .version = PNG_IMAGE_VERSION, .width = 8, .height = 8, .format = PNG_FORMAT_RGBA };
  unsigned char png_data[4096];
  png_alloc_size_t length = sizeof(png_data);
  assert(png_image_write_to_memory(&png, png_data, &length, 0, pixels, 0, NULL));
  vita2d_texture *t = sysappLoadImageBuffer(png_data, length);
  assert(t && t->width == 8 && t->height == 8);
  vita2d_free_texture(t);
  for (size_t size = 8; size < length; size += 7) {
    t = sysappLoadImageBuffer(png_data, size);
    vita2d_free_texture(t);
    assert(textures == 0); /* libpng longjmp must free an allocated texture. */
  }
  reject_texture = 1;
  assert(!sysappLoadImageBuffer(png_data, length));
  reject_texture = 0;
  /* Well-formed but too large, including an IHDR with a correct CRC. */
  png.width = 1024; png.height = 1024;
  void *large = calloc(1024 * 1024, 4);
  length = 0;
  assert(png_image_write_to_memory(&png, NULL, &length, 0, large, 0, NULL));
  void *encoded = malloc(length);
  assert(png_image_write_to_memory(&png, encoded, &length, 0, large, 0, NULL));
  assert(!sysappLoadImageBuffer(encoded, length));
  free(encoded); free(large);
  struct jpeg_compress_struct jpeg;
  struct jpeg_error_mgr error;
  jpeg.err = jpeg_std_error(&error);
  jpeg_create_compress(&jpeg);
  unsigned char *jpeg_data = NULL;
  unsigned long jpeg_size = 0;
  jpeg_mem_dest(&jpeg, &jpeg_data, &jpeg_size);
  jpeg.image_width = 8; jpeg.image_height = 8; jpeg.input_components = 3; jpeg.in_color_space = JCS_RGB;
  jpeg_set_defaults(&jpeg);
  jpeg_start_compress(&jpeg, TRUE);
  while (jpeg.next_scanline < 8) {
    JSAMPROW row = pixels;
    jpeg_write_scanlines(&jpeg, &row, 1);
  }
  jpeg_finish_compress(&jpeg);
  jpeg_destroy_compress(&jpeg);
  t = sysappLoadImageBuffer(jpeg_data, jpeg_size);
  assert(t && t->width == 8);
  vita2d_free_texture(t);
  reject_texture = 1;
  assert(!sysappLoadImageBuffer(jpeg_data, jpeg_size));
  reject_texture = 0;
  assert(!sysappLoadImageBuffer(jpeg_data, 8));
  free(jpeg_data);
  unsigned char bmp[58] = { 'B', 'M' };
  bmp[10] = 54; bmp[14] = 40; bmp[18] = 1; bmp[22] = 1; bmp[26] = 1; bmp[28] = 24;
  t = sysappLoadImageBuffer(bmp, sizeof(bmp));
  assert(t); vita2d_free_texture(t);
  assert(!sysappLoadImageBuffer(bmp, 57));
  bmp[18] = 0xff; bmp[19] = 0xff; bmp[20] = 0xff; bmp[21] = 0xff;
  assert(!sysappLoadImageBuffer(bmp, sizeof(bmp)));
  assert(textures == 0);
  puts("bounded image decoding: passed");
}
