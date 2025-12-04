#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXFILESIZE 1048576   /* 1 MB */
#define TAG_SIZE    0
#define TAG_DATA    1

int main(int argc, char *argv[])
{
    int rank, size;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size != 2) {
        if (rank == 0) {
            fprintf(stderr,
                    "Please run with exactly 2 processes.\n"
                    "Example: mpirun -np 2 ./mpi_file_transfer in.txt out.txt\n");
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (argc != 3) {
        if (rank == 0) {
            fprintf(stderr,
                    "Usage: %s <input_file> <output_file>\n", argv[0]);
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    const char *input_file  = argv[1];
    const char *output_file = argv[2];

    if (rank == 0) {
        FILE *fp = fopen(input_file, "rb");
        if (!fp) {
            perror("fopen input_file");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        if (fseek(fp, 0, SEEK_END) != 0) {
            perror("fseek");
            fclose(fp);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        long filesize = ftell(fp);
        if (filesize < 0 || filesize > MAXFILESIZE) {
            fprintf(stderr,
                    "File too large or ftell error (size=%ld)\n", filesize);
            fclose(fp);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        rewind(fp);

        int len = (int)filesize;
        unsigned char *buffer = malloc(len);
        if (!buffer) {
            perror("malloc");
            fclose(fp);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        if (fread(buffer, 1, len, fp) != (size_t)len) {
            perror("fread");
            free(buffer);
            fclose(fp);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fclose(fp);

        printf("[Rank 0] Read %d bytes from %s, sending to rank 1...\n",
               len, input_file);

        MPI_Send(&len, 1, MPI_INT, 1, TAG_SIZE, MPI_COMM_WORLD);
        MPI_Send(buffer, len, MPI_BYTE, 1, TAG_DATA, MPI_COMM_WORLD);

        printf("[Rank 0] Done sending.\n");
        free(buffer);

    } else if (rank == 1) {
        int len = 0;
        MPI_Status status;

        MPI_Recv(&len, 1, MPI_INT, 0, TAG_SIZE, MPI_COMM_WORLD, &status);
        if (len <= 0 || len > MAXFILESIZE) {
            fprintf(stderr,
                    "[Rank 1] Invalid size received: %d\n", len);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        unsigned char *buffer = malloc(len);
        if (!buffer) {
            perror("malloc");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        MPI_Recv(buffer, len, MPI_BYTE, 0, TAG_DATA, MPI_COMM_WORLD, &status);

        FILE *out = fopen(output_file, "wb");
        if (!out) {
            perror("fopen output_file");
            free(buffer);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        if (fwrite(buffer, 1, len, out) != (size_t)len) {
            perror("fwrite");
            free(buffer);
            fclose(out);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        fclose(out);
        printf("[Rank 1] Received %d bytes from rank 0 and wrote to %s\n",
               len, output_file);
        free(buffer);
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}
