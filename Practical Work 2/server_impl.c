#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "file_transfer.h"


file_result *get_file_1_svc(filename_t *argp, struct svc_req *rqstp)
{
    static file_result result;     
    FILE *fp;
    long filesize;
    char *buf;

    if (result.data.filedata_t_val != NULL) {
        free(result.data.filedata_t_val);
        result.data.filedata_t_val = NULL;
        result.data.filedata_t_len = 0;
    }

    result.status = 0;
    result.data.filedata_t_val = NULL;
    result.data.filedata_t_len = 0;

    printf("Client requested file: %s\n", *argp);

    fp = fopen(*argp, "rb");
    if (!fp) {
        perror("fopen");
        result.status = errno;
        return &result;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        result.status = errno;
        fclose(fp);
        return &result;
    }

    filesize = ftell(fp);
    if (filesize < 0 || filesize > MAXFILESIZE) {
        fprintf(stderr, "File too large or ftell error\n");
        result.status = EFBIG;
        fclose(fp);
        return &result;
    }

    rewind(fp);

    buf = malloc(filesize);
    if (!buf) {
        perror("malloc");
        result.status = ENOMEM;
        fclose(fp);
        return &result;
    }

    if (fread(buf, 1, filesize, fp) != (size_t)filesize) {
        perror("fread");
        result.status = EIO;
        free(buf);
        fclose(fp);
        return &result;
    }

    fclose(fp);

    result.status = 0;
    result.data.filedata_t_val = buf;
    result.data.filedata_t_len = (u_int)filesize;

    return &result;
}
