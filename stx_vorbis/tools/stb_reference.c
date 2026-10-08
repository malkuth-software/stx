/* Test-only executable linked with the pinned, external stb implementation. */
#include "stx_vorbis/stb_compatible.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    int error = 0;
    stb_vorbis* decoder = stb_vorbis_open_filename(argv[1], &error, NULL);
    if (decoder == NULL) { fprintf(stderr, "stb error %d\n", error); return 1; }
    FILE* output = fopen(argv[2], "wb");
    if (output == NULL) { stb_vorbis_close(decoder); return 1; }
    int channels = 0;
    float** samples = NULL;
    while (1) {
        int frames = stb_vorbis_get_frame_float(decoder, &channels, &samples);
        if (frames == 0) break;
        for (int frame = 0; frame < frames; ++frame) {
            for (int channel = 0; channel < channels; ++channel) {
                float value = samples[channel][frame];
                if (fwrite(&value, sizeof(value), 1, output) != 1) return 1;
            }
        }
    }
    error = stb_vorbis_get_error(decoder);
    stb_vorbis_close(decoder);
    fclose(output);
    if (error != 0) { fprintf(stderr, "stb decode error %d\n", error); return 1; }
    return 0;
}
