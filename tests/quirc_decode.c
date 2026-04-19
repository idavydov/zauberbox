#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../firmware/src/qr_reader/quirc/quirc.h"

static int parse_dimension(const char *value, const char *name) {
    char *end = NULL;
    const long parsed = strtol(value, &end, 10);
    if (!end || *end != '\0' || parsed <= 0 || parsed > 32767) {
        fprintf(stderr, "invalid %s: %s\n", name, value);
        return -1;
    }
    return (int)parsed;
}

static int read_exact(uint8_t *buffer, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const size_t nread = fread(buffer + offset, 1, size - offset, stdin);
        if (nread == 0) {
            if (ferror(stdin)) {
                perror("fread");
            }
            return -1;
        }
        offset += nread;
    }
    return 0;
}

static void print_payload_hex(const uint8_t *payload, int length) {
    for (int i = 0; i < length; ++i) {
        printf("%02x", payload[i]);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <width> <height>\n", argv[0]);
        return 2;
    }

    const int width = parse_dimension(argv[1], "width");
    const int height = parse_dimension(argv[2], "height");
    if (width <= 0 || height <= 0) {
        return 2;
    }

    const size_t image_size = (size_t)width * (size_t)height;
    uint8_t *source = malloc(image_size);
    if (!source) {
        fprintf(stderr, "malloc(%zu) failed: %s\n", image_size, strerror(errno));
        return 1;
    }

    if (read_exact(source, image_size) != 0) {
        fprintf(stderr, "failed to read %zu image bytes from stdin\n", image_size);
        free(source);
        return 1;
    }

    struct quirc *decoder = quirc_new();
    if (!decoder) {
        fprintf(stderr, "quirc_new failed\n");
        free(source);
        return 1;
    }
    if (quirc_resize(decoder, width, height) < 0) {
        fprintf(stderr, "quirc_resize(%d, %d) failed\n", width, height);
        quirc_destroy(decoder);
        free(source);
        return 1;
    }

    uint8_t *image = quirc_begin(decoder, NULL, NULL);
    memcpy(image, source, image_size);
    quirc_end(decoder);

    const int count = quirc_count(decoder);
    printf("COUNT\t%d\n", count);

    for (int index = 0; index < count; ++index) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(decoder, index, &code);
        const quirc_decode_error_t err = quirc_decode(&code, &data);
        if (err != QUIRC_SUCCESS) {
            printf("INVALID\t%s\n", quirc_strerror(err));
            continue;
        }

        printf("VALID\t%d\t%d\t", data.data_type, data.payload_len);
        print_payload_hex(data.payload, data.payload_len);
        printf("\n");
    }

    quirc_destroy(decoder);
    free(source);
    return 0;
}
