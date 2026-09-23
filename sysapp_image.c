/* Bounded decoders for untrusted photos, covers and theme images. */
#include "main.h"
#include "sysapp_image.h"
#include <png.h>
#include <jpeglib.h>
#include <setjmp.h>

#define MAX_IMAGE_DIMENSION 1024U
#define MAX_IMAGE_PIXELS (512U * 1024)

static int dimensionsAllowed(unsigned int w, unsigned int h) {
  return w && h && w <= MAX_IMAGE_DIMENSION && h <= MAX_IMAGE_DIMENSION &&
         w <= MAX_IMAGE_PIXELS / h;
}

typedef struct {
  const unsigned char *data;
  size_t size, offset;
  vita2d_texture *texture;
  png_bytep *rows;
} PngInput;

static void readPng(png_structp png, png_bytep out, png_size_t length) {
  PngInput *in = png_get_io_ptr(png);
  if (length > in->size - in->offset) png_error(png, "Truncated image");
  memcpy(out, in->data + in->offset, length);
  in->offset += length;
}

static vita2d_texture *loadPng(const void *data, size_t size) {
  PngInput *in = calloc(1, sizeof(*in));
  if (!in) return NULL;
  in->data = data;
  in->size = size;
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) { free(in); return NULL; }
  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_read_struct(&png, NULL, NULL); free(in); return NULL; }
  if (setjmp(png_jmpbuf(png))) {
    if (in->texture) vita2d_free_texture(in->texture);
    free(in->rows);
    free(in);
    png_destroy_read_struct(&png, &info, NULL);
    return NULL;
  }
  png_set_read_fn(png, in, readPng);
  png_set_user_limits(png, MAX_IMAGE_DIMENSION, MAX_IMAGE_DIMENSION);
  png_set_chunk_malloc_max(png, 128 * 1024);
  png_set_chunk_cache_max(png, 32);
  png_read_info(png, info);
  unsigned int w = png_get_image_width(png, info), h = png_get_image_height(png, info);
  if (!dimensionsAllowed(w, h)) png_error(png, "Image exceeds sysapp limit");
  int color = png_get_color_type(png, info), bits = png_get_bit_depth(png, info);
  if (bits == 16) png_set_strip_16(png);
  if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
  if (color == PNG_COLOR_TYPE_GRAY && bits < 8) png_set_expand_gray_1_2_4_to_8(png);
  int transparency = png_get_valid(png, info, PNG_INFO_tRNS);
  if (transparency) png_set_tRNS_to_alpha(png);
  if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
  if (!(color & PNG_COLOR_MASK_ALPHA) && !transparency) png_set_filler(png, 255, PNG_FILLER_AFTER);
  png_set_interlace_handling(png);
  png_read_update_info(png, info);
  if (png_get_rowbytes(png, info) != w * 4) png_error(png, "Unsupported image");
  in->texture = vita2d_create_empty_texture(w, h);
  in->rows = malloc(h * sizeof(*in->rows));
  if (!in->texture || !in->rows) png_error(png, "Out of memory");
  unsigned char *pixels = vita2d_texture_get_datap(in->texture);
  unsigned int stride = vita2d_texture_get_stride(in->texture);
  for (unsigned int y = 0; y < h; ++y) in->rows[y] = pixels + y * stride;
  png_read_image(png, in->rows);
  png_read_end(png, info);
  vita2d_texture *texture = in->texture;
  free(in->rows);
  free(in);
  png_destroy_read_struct(&png, &info, NULL);
  return texture;
}

typedef struct {
  struct jpeg_decompress_struct decoder;
  struct jpeg_error_mgr error;
  jmp_buf jump;
  vita2d_texture *texture;
} JpegInput;

static void jpegError(j_common_ptr decoder) {
  JpegInput *in = decoder->client_data;
  longjmp(in->jump, 1);
}

static vita2d_texture *loadJpeg(const void *data, size_t size) {
  JpegInput *in = calloc(1, sizeof(*in));
  if (!in) return NULL;
  in->decoder.err = jpeg_std_error(&in->error);
  in->error.error_exit = jpegError;
  in->decoder.client_data = in;
  if (setjmp(in->jump)) {
    if (in->texture) vita2d_free_texture(in->texture);
    jpeg_destroy_decompress(&in->decoder);
    free(in);
    return NULL;
  }
  jpeg_create_decompress(&in->decoder);
  jpeg_mem_src(&in->decoder, data, size);
  jpeg_read_header(&in->decoder, TRUE);
  if (!dimensionsAllowed(in->decoder.image_width, in->decoder.image_height)) jpegError((j_common_ptr)&in->decoder);
  in->decoder.out_color_space = JCS_RGB;
  in->decoder.mem->max_memory_to_use = 2 * 1024 * 1024;
  jpeg_start_decompress(&in->decoder);
  in->texture = vita2d_create_empty_texture_format(in->decoder.output_width,
      in->decoder.output_height, SCE_GXM_TEXTURE_FORMAT_U8U8U8_BGR);
  if (!in->texture) jpegError((j_common_ptr)&in->decoder);
  unsigned char *pixels = vita2d_texture_get_datap(in->texture);
  unsigned int stride = vita2d_texture_get_stride(in->texture);
  while (in->decoder.output_scanline < in->decoder.output_height) {
    JSAMPROW row = pixels + in->decoder.output_scanline * stride;
    jpeg_read_scanlines(&in->decoder, &row, 1);
  }
  jpeg_finish_decompress(&in->decoder);
  jpeg_destroy_decompress(&in->decoder);
  vita2d_texture *texture = in->texture;
  free(in);
  return texture;
}

static uint32_t le32(const unsigned char *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static vita2d_texture *loadBmp(const unsigned char *data, size_t size) {
  if (size < 54 || le32(data + 14) < 40) return NULL;
  uint32_t w = le32(data + 18), raw_h = le32(data + 22), offset = le32(data + 10);
  int top_down = (int32_t)raw_h < 0;
  uint32_t h = top_down ? 0U - raw_h : raw_h;
  unsigned int bits = data[28] | (data[29] << 8);
  if (!dimensionsAllowed(w, h) || (bits != 24 && bits != 32) ||
      data[26] != 1 || data[27] || le32(data + 30) || offset < 54 || offset > size)
    return NULL;
  unsigned int stride = (w * (bits / 8) + 3) & ~3U;
  if (h > (size - offset) / stride) return NULL;
  vita2d_texture *texture = vita2d_create_empty_texture(w, h);
  if (!texture) return NULL;
  unsigned char *out = vita2d_texture_get_datap(texture);
  unsigned int out_stride = vita2d_texture_get_stride(texture);
  for (unsigned int y = 0; y < h; ++y) {
    const unsigned char *row = data + offset + (top_down ? y : h - y - 1) * stride;
    for (unsigned int x = 0; x < w; ++x) {
      unsigned char *pixel = out + y * out_stride + x * 4;
      pixel[0] = row[x * (bits / 8) + 2];
      pixel[1] = row[x * (bits / 8) + 1];
      pixel[2] = row[x * (bits / 8)];
      pixel[3] = 255;
    }
  }
  return texture;
}

vita2d_texture *sysappLoadImageBuffer(const void *data, size_t size) {
  if (!data || size < 8 || size > BIG_BUFFER_SIZE) return NULL;
  const unsigned char *p = data;
  if (!png_sig_cmp(p, 0, 8)) return loadPng(data, size);
  if (p[0] == 0xff && p[1] == 0xd8) return loadJpeg(data, size);
  if (p[0] == 'B' && p[1] == 'M') return loadBmp(data, size);
  return NULL;
}

vita2d_texture *sysappLoadImageFile(const char *path) {
  void *buffer = NULL;
  int size = allocateReadFile(path, &buffer);
  vita2d_texture *texture = size > 0 ? sysappLoadImageBuffer(buffer, size) : NULL;
  free(buffer);
  return texture;
}
