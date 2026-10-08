#include "stx_vorbis/stb_compatible.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static void check(int condition, const char* message) {
    if (!condition) { fprintf(stderr, "%s\n", message); abort(); }
}
int main(int argc, char** argv) {
    check(argc == 2, "fixture required");
    int error = 0;
    stb_vorbis* decoder = stb_vorbis_open_filename(argv[1], &error, NULL);
    check(decoder != NULL && error == 0, "C open");
    stb_vorbis_info info = stb_vorbis_get_info(decoder);
    check(info.channels > 0 && info.channels <= 255, "C channels");
    stb_vorbis_comment comments = stb_vorbis_get_comment(decoder);
    check(comments.vendor != NULL, "C comments");
    unsigned int length = stb_vorbis_stream_length_in_samples(decoder);
    check(length > 0, "C length");
    float first[255] = {0};
    check(stb_vorbis_get_samples_float_interleaved(decoder, info.channels, first, info.channels) == 1, "C sample");
    check(stb_vorbis_get_sample_offset(decoder) == 1, "C position");
    check(stb_vorbis_seek_start(decoder) != 0, "C seek");
    float repeated[255] = {0};
    check(stb_vorbis_get_samples_float_interleaved(decoder, info.channels, repeated, info.channels) == 1, "C repeat");
    for (int channel = 0; channel < info.channels; ++channel) check(first[channel] == repeated[channel], "C seek samples");
    check(stb_vorbis_get_samples_float(decoder, info.channels, NULL, 0) == 0, "C zero float planar");
    check(stb_vorbis_get_samples_short(decoder, info.channels, NULL, 0) == 0, "C zero short planar");
    stb_vorbis_close(decoder);
    short* pcm = NULL;
    int channels = 0;
    int rate = 0;
    int frames = stb_vorbis_decode_filename(argv[1], &channels, &rate, &pcm);
    check(frames == (int)length && pcm != NULL && channels == info.channels, "C whole decode");
    free(pcm);
    FILE* file = fopen(argv[1], "rb");
    check(file != NULL, "C fixture");
    check(fseek(file, 0, SEEK_END) == 0, "C fixture length");
    long bytes = ftell(file);
    check(bytes > 0 && bytes < 10000000, "C fixture size");
    rewind(file);
    unsigned char* input = malloc((size_t)bytes);
    check(input != NULL && fread(input, 1, (size_t)bytes, file) == (size_t)bytes, "C fixture read");
    fclose(file);
    char* arena = malloc(8 * 1024 * 1024);
    check(arena != NULL, "C arena");
    stb_vorbis_alloc allocation = {arena, 8 * 1024 * 1024};
    decoder = stb_vorbis_open_memory(input, (int)bytes, &error, &allocation);
    check(decoder != NULL, "C supplied arena");
    for (int attempt = 0; attempt < 10; ++attempt) check(stb_vorbis_seek_start(decoder) != 0, "C arena repeated seek");
    stb_vorbis_close(decoder);
    free(arena);
    int consumed = 0;
    decoder = stb_vorbis_open_pushdata(input, (int)bytes, &consumed, &error, NULL);
    check(decoder != NULL && consumed > 0 && consumed <= bytes, "C push open");
    int total = 0;
    for (int iteration = 0; iteration < 100000; ++iteration) {
        int samples = 0;
        float** planes = NULL;
        int accepted = stb_vorbis_decode_frame_pushdata(decoder, input + consumed, (int)bytes - consumed, &channels, &planes, &samples);
        consumed += accepted;
        total += samples;
        if (accepted == 0 && samples == 0) break;
    }
    check(total == (int)length, "C push PCM");
    stb_vorbis_close(decoder);
    free(input);
    puts("C adapter tests passed");
    return 0;
}
