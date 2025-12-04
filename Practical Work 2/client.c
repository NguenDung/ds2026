#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_transfer.h"

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <server_host> <remote_filename> <local_filename>\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }

    char *server_host   = argv[1];
    char *remote_file   = argv[2];
    char *local_file    = argv[3];

    CLIENT *clnt;
    file_result *res;
    FILE *out;

    clnt = clnt_create(server_host, FILE_TRANSFER_PROG,
                       FILE_TRANSFER_VERS, "tcp");
    if (clnt == NULL) {
        clnt_pcreateerror(server_host);
        exit(EXIT_FAILURE);
    }

    res = get_file_1(&remote_file, clnt);
    if (res == NULL) {
        clnt_perror(clnt, "RPC call failed");
        clnt_destroy(clnt);
        exit(EXIT_FAILURE);
    }

    if (res->status != 0) {
        fprintf(stderr, "Server error, status = %d\n", res->status);
        clnt_destroy(clnt);
        exit(EXIT_FAILURE);
    }

    out = fopen(local_file, "wb");
    if (!out) {
        perror("fopen");
        clnt_destroy(clnt);
        exit(EXIT_FAILURE);
    }

    if (fwrite(res->data.filedata_t_val, 1,
               res->data.filedata_t_len, out) != res->data.filedata_t_len) {
        perror("fwrite");
        fclose(out);
        clnt_destroy(clnt);
        exit(EXIT_FAILURE);
    }

    fclose(out);
    printf("Downloaded %u bytes to %s\n",
           res->data.filedata_t_len, local_file);

    clnt_destroy(clnt);
    return 0;
}
