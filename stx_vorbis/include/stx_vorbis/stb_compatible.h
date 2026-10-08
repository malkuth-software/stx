#ifndef STX_VORBIS_STB_COMPATIBLE_H
#define STX_VORBIS_STB_COMPATIBLE_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Source-compatible stb_vorbis API, implemented by stx_vorbis. Link either this
   adapter or stb_vorbis, never both. No decoder implementation is included here.
   Supplied allocation buffers are bounded arenas; no heap fallback is used.
   Use the C++ API for explicit chain events and resource/recovery configuration. */
typedef struct stb_vorbis stb_vorbis;
typedef struct { char* alloc_buffer; int alloc_buffer_length_in_bytes; } stb_vorbis_alloc;
typedef struct {
    unsigned int sample_rate;
    int channels;
    unsigned int setup_memory_required;
    unsigned int setup_temp_memory_required;
    unsigned int temp_memory_required;
    int max_frame_size;
} stb_vorbis_info;
typedef struct { char* vendor; int comment_list_length; char** comment_list; } stb_vorbis_comment;
enum STBVorbisError {
    VORBIS__no_error = 0, VORBIS_need_more_data = 1, VORBIS_invalid_api_mixing = 2,
    VORBIS_outofmem = 3, VORBIS_feature_not_supported = 4, VORBIS_too_many_channels = 5,
    VORBIS_file_open_failure = 6, VORBIS_seek_without_length = 7,
    VORBIS_unexpected_eof = 10, VORBIS_seek_invalid = 11,
    VORBIS_invalid_setup = 20, VORBIS_invalid_stream = 21,
    VORBIS_missing_capture_pattern = 30, VORBIS_invalid_stream_structure_version = 31,
    VORBIS_continued_packet_flag_invalid = 32, VORBIS_incorrect_stream_serial_number = 33,
    VORBIS_invalid_first_page = 34, VORBIS_bad_packet_type = 35,
    VORBIS_cant_find_last_page = 36, VORBIS_seek_failed = 37, VORBIS_ogg_skeleton_not_supported = 38
};
stb_vorbis_info stb_vorbis_get_info(stb_vorbis* decoder);
stb_vorbis_comment stb_vorbis_get_comment(stb_vorbis* decoder);
int stb_vorbis_get_error(stb_vorbis* decoder);
void stb_vorbis_close(stb_vorbis* decoder);
int stb_vorbis_get_sample_offset(stb_vorbis* decoder);
unsigned int stb_vorbis_get_file_offset(stb_vorbis* decoder);
stb_vorbis* stb_vorbis_open_pushdata(const unsigned char* bytes, int size, int* consumed, int* error, const stb_vorbis_alloc* allocation);
int stb_vorbis_decode_frame_pushdata(stb_vorbis* decoder, const unsigned char* bytes, int size, int* channels, float*** output, int* samples);
void stb_vorbis_flush_pushdata(stb_vorbis* decoder);
stb_vorbis* stb_vorbis_open_memory(const unsigned char* bytes, int size, int* error, const stb_vorbis_alloc* allocation);
stb_vorbis* stb_vorbis_open_filename(const char* path, int* error, const stb_vorbis_alloc* allocation);
stb_vorbis* stb_vorbis_open_file(FILE* file, int close_on_close, int* error, const stb_vorbis_alloc* allocation);
stb_vorbis* stb_vorbis_open_file_section(FILE* file, int close_on_close, int* error, const stb_vorbis_alloc* allocation, unsigned int length);
int stb_vorbis_decode_filename(const char* path, int* channels, int* sample_rate, short** output);
int stb_vorbis_decode_memory(const unsigned char* bytes, int size, int* channels, int* sample_rate, short** output);
int stb_vorbis_seek_frame(stb_vorbis* decoder, unsigned int sample);
int stb_vorbis_seek(stb_vorbis* decoder, unsigned int sample);
int stb_vorbis_seek_start(stb_vorbis* decoder);
unsigned int stb_vorbis_stream_length_in_samples(stb_vorbis* decoder);
float stb_vorbis_stream_length_in_seconds(stb_vorbis* decoder);
int stb_vorbis_get_frame_float(stb_vorbis* decoder, int* channels, float*** output);
int stb_vorbis_get_frame_short_interleaved(stb_vorbis* decoder, int channels, short* buffer, int shorts);
int stb_vorbis_get_frame_short(stb_vorbis* decoder, int channels, short** buffer, int frames);
int stb_vorbis_get_samples_float_interleaved(stb_vorbis* decoder, int channels, float* buffer, int floats);
int stb_vorbis_get_samples_float(stb_vorbis* decoder, int channels, float** buffer, int frames);
int stb_vorbis_get_samples_short_interleaved(stb_vorbis* decoder, int channels, short* buffer, int shorts);
int stb_vorbis_get_samples_short(stb_vorbis* decoder, int channels, short** buffer, int frames);
#ifdef __cplusplus
}
#endif
#endif
