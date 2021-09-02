#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include "mpi.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

typedef struct pixel_struct {
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
} pixel;

/* Bilinear interpolation. */
void bilinear(pixel* Im, float row, float col, pixel* pix, int width, int height) {
	int cm, cn, fm, fn;
	double alpha, beta;

	cm = (int)ceil(row);
	fm = (int)floor(row);
	cn = (int)ceil(col);
	fn = (int)floor(col);
	alpha = ceil(row) - row;
	beta = ceil(col) - col;

	pix->r = (unsigned char)(alpha*beta*Im[fm*width+fn].r
			+ (1-alpha)*beta*Im[cm*width+fn].r
			+ alpha*(1-beta)*Im[fm*width+cn].r
			+ (1-alpha)*(1-beta)*Im[cm*width+cn].r );
	pix->g = (unsigned char)(alpha*beta*Im[fm*width+fn].g
			+ (1-alpha)*beta*Im[cm*width+fn].g
			+ alpha*(1-beta)*Im[fm*width+cn].g
			+ (1-alpha)*(1-beta)*Im[cm*width+cn].g );
	pix->b = (unsigned char)(alpha*beta*Im[fm*width+fn].b
			+ (1-alpha)*beta*Im[cm*width+fn].b
			+ alpha*(1-beta)*Im[fm*width+cn].b
			+ (1-alpha)*(1-beta)*Im[cm*width+cn].b );
	pix->a = 255;
}

void SEGVFunction( int sig_num) {
	printf ("\n Signal %d received\n",sig_num);
	exit(sig_num);
}

int main(int argc, char** argv) {
	signal(SIGSEGV, SEGVFunction);
	stbi_set_flip_vertically_on_load(true);
	stbi_flip_vertically_on_write(true);

//TODO 1 - init
    int comm_size;
    int rank;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
//TODO END


	pixel* pixels_in;

	int in_width;
	int in_height;
	int channels;


//TODO 2 - broadcast
    if (rank == 0) {
        /* Root process; load and broadcast image. */
        pixels_in = (pixel *) stbi_load(argv[1], &in_width, &in_height, &channels, STBI_rgb_alpha);
        if (pixels_in == NULL) {
            exit(1);
        }
    }

    /* Broadcast dimensions. */
    MPI_Bcast((void*) &in_width,  1 /* 1 int, not byte */, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast((void*) &in_height, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /* Now that we know the dims, make a place for the data in the non-root procs. */
    if (rank != 0) {
        pixels_in = calloc(in_width * in_height, sizeof (pixel));
    }

    /* Broadcast the pixel buffer. */
    MPI_Bcast(pixels_in, sizeof (pixel) * in_width * in_height /* N bytes */, MPI_BYTE, 0, MPI_COMM_WORLD);
    
    /* So after this, width/height and buffer is prepared in all processes. */
//TODO END

	double scale_x = argc > 2 ? atof(argv[2]): 2;
	double scale_y = argc > 3 ? atof(argv[3]): 8;

	int out_width = in_width * scale_x;
	int out_height = in_height * scale_y;

    /* Inform the user what we are about to do. */
    if (rank == 0) {
        printf("Recaling image %dx%di -> %dx%d.\n", in_width, in_height, out_width, out_height);
    }
	
//TODO 3 - partitioning

    // i'm taking a wild guess that pixels are stored row-majorly by stb
    // the most efficient way to chunk data is NOT in a grid; its in rows

    /* how many rows do i do??? */

    int chunk = out_height / comm_size;

    /* where do we need to work??? */
    int offset = out_height - rank * chunk;

    // if the out-size isnt an exact multiple of the # of procs, we get
    // problems. fix it the stupid way by just doing more work in root.
    // i know we could assume this, but why not just make a good program :-)
    if (rank == 0) {
        chunk  = out_height - (comm_size-1) * chunk;
        offset = 0;
    }

    // So after this, the precise location we need to do work is decided.
    // Reserve enough space for the result.
    pixel *local_out = calloc(chunk * out_width, sizeof (pixel));

    printf("Proc #%d gonna do %d/%d rows starting at %d.\n", rank, chunk, out_height, offset);
//TODO END


//TODO 4 - computation
	for(int i = offset; i < offset+chunk; i++) {
        /* out width is the same as for single threaded case */
		for(int j = 0; j < out_width; j++) {
			pixel new_pixel;

			float row = i * (in_height-1) / (float)out_height;
			float col = j * (in_width-1) / (float)out_width;

			bilinear(pixels_in, row, col, &new_pixel, in_width, in_height);

            // kinda ugly to add and subtract offset in a nested loop but whatver, compiler will save us
			local_out[(i-offset)*out_width+j] = new_pixel;
		}
	}
    printf("Hey at least proc #%d didn't segfault!\n", rank);
//TODO END


//TODO 5 - gather
    /* only root needs valid pointers */
	pixel *pixels_out = NULL;
    int *recvcounts = NULL;
    int *displs = NULL;


    /* we should probably let each process just communicate this
     * by broadcasting a second array instead of reverse-engineering
     * it like this */
    if (rank == 0) {
        pixels_out = calloc(out_width * out_height, sizeof (pixel));
        recvcounts = calloc(comm_size, sizeof (int));
        displs = calloc(comm_size, sizeof (int));

        /* every process except root did height/procs rows.
         * starting at rank*chunk away from the end */
        for (int k = 1; k < comm_size; k++) {
            int rows = (out_height / comm_size);
            recvcounts[k] = sizeof (pixel) * rows * out_width;

            int row_disp = (out_height - k * chunk);
            displs[k] = sizeof(pixel) * row_disp * out_width;
        }

        /* root is special but we already know it. */
        recvcounts[0] = chunk * sizeof (pixel) * out_width;
        displs[0] = 0;

        for (int i = 0; i < comm_size; i++) {
            printf("Gonna receive %d bytes starting at %d from proc #%d\n", recvcounts[i], displs[i], i);
        }
    }

    MPI_Gatherv((void*) local_out, out_width * chunk * sizeof (pixel), MPI_BYTE,
                (void*) pixels_out, recvcounts, displs, MPI_BYTE, 0, MPI_COMM_WORLD);

    /* so at this point the picture is completed in root. */

    free(local_out); /* After the data is gathered, the local buffer is redundant. */

    if (rank == 0) {
        /* If we are root, also free the buffers we used to gather the data. */
        free(recvcounts);
        free(displs);

        /* Write the image and free the output buffer. */
        stbi_write_png("output.png", out_width, out_height, STBI_rgb_alpha, pixels_out, sizeof(pixel) * out_width);
        free(pixels_out);
    }

//TODO END


//TODO 1 - init
//TODO END
    MPI_Finalize();
	return 0;
}
