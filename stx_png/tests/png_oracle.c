/* Test-only libpng boundary. All allocations live in a C-owned record so a
 * libpng longjmp cannot skip C++ destructors or lose modified automatic state. */
#include <png.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct oracle_state {
    const unsigned char* input;
    size_t size;
    size_t offset;
    png_bytep* rows;
};
static void oracle_read(png_structp png, png_bytep destination, png_size_t count) {
    struct oracle_state* state = png_get_io_ptr(png);
    if (count > (*state).size - (*state).offset) png_error(png, "short oracle input");
    memcpy(destination, (*state).input + (*state).offset, count);
    (*state).offset += count;
}
static void oracle_warning(png_structp png, png_const_charp message) {
    (void)png;
    (void)message;
}
int stx_png_oracle(const unsigned char* input, size_t size,
                         unsigned char* output, size_t capacity,
                         uint32_t width, uint32_t height) {
    struct oracle_state* state = calloc(1, sizeof(struct oracle_state));
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, oracle_warning);
    png_infop info = NULL;
    int success = 0;
    if (state == NULL || png == NULL) {
        free(state);
        if (png != NULL) png_destroy_read_struct(&png, NULL, NULL);
        return 0;
    }
    info = png_create_info_struct(png);
    if (info == NULL) {
        png_destroy_read_struct(&png, NULL, NULL);
        free(state);
        return 0;
    }
    (*state).input = input;
    (*state).size = size;
    if (setjmp(png_jmpbuf(png)) == 0) {
        png_uint_32 y = 0;
        png_set_read_fn(png, state, oracle_read);
        png_set_user_limits(png, width, height);
        png_read_info(png, info);
        if (png_get_image_width(png, info) != width || png_get_image_height(png, info) != height) {
            png_error(png, "oracle dimensions");
        }
        png_set_expand_16(png);
        png_set_gray_to_rgb(png);
        if ((png_get_color_type(png, info) & PNG_COLOR_MASK_ALPHA) == 0 &&
            png_get_valid(png, info, PNG_INFO_tRNS) == 0) {
            png_set_add_alpha(png, 65535, PNG_FILLER_AFTER);
        }
        png_set_interlace_handling(png);
        png_read_update_info(png, info);
        if (png_get_rowbytes(png, info) != (size_t)width * 8 ||
            (uint64_t)width * height * 8 > capacity) png_error(png, "oracle output shape");
        (*state).rows = calloc(height, sizeof(png_bytep));
        if ((*state).rows == NULL) png_error(png, "oracle allocation");
        for (y = 0; y < height; ++y) (*state).rows[y] = output + (size_t)y * width * 8;
        png_read_image(png, (*state).rows);
        png_read_end(png, info);
        success = 1;
    }
    free((*state).rows);
    png_destroy_read_struct(&png, &info, NULL);
    free(state);
    return success;
}
