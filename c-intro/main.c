#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

//#include <windows.h>
//#include <magick_wand.h>

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} pixel;

void avg(pixel *p, pixel *q, pixel *ret) {
    ret->r = .5 * (p->r + q->r);
    ret->g = .5 * (p->g + q->g);
    ret->b = .5 * (p->b + q->b);
    ret->a = 255;
}

int main(int argc, char** argv) {
    stbi_set_flip_vertically_on_load(true);
    stbi_flip_vertically_on_write(true);

    int width;
    int height;
    int channels;
    
    /* Assume that the pictures are the same dimensions. */
    unsigned char* char_pixels_1 = stbi_load(argv[1], &width, &height, &channels, STBI_rgb_alpha);
    unsigned char* char_pixels_2 = stbi_load(argv[2], &width, &height, &channels, STBI_rgb_alpha);

    /* Interpret buffers as pixels arrays. */
    pixel* pixels_1 = (pixel *) char_pixels_1;
    pixel* pixels_2 = (pixel *) char_pixels_2;

    printf("height:%d, width: %d\n", height, width);
    if (pixels_1 == NULL || pixels_2 == NULL)
    {
        exit(1);
    }

    /* Assigning void* promotes it safely, this is fine in C. */
    pixel* pixels_out = malloc(width * height * sizeof (pixel));

    /* Fill the output buffer with the mean of the input buffers. */
    for (int i = 0; i < width * height; i++) {
        avg(pixels_1 + i, pixels_2 + i, pixels_out + i);
    }

    stbi_write_png("output.png", width, height, STBI_rgb_alpha, pixels_out, sizeof(pixel) * width);

    /* Clean up. */
    free(pixels_1); /* alias of char_pixels_1 */
    free(pixels_2); /* alias of char_pixels_2 */
    free(pixels_out);

    return 0;
}
