#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CMD    256
#define MAX_OUTPUT 4096

#define TAG_CMD    0
#define TAG_RESULT 1

/* Chạy command trên server và ghi output vào buffer */
void run_command(const char *cmd, char *outbuf, size_t outbuf_size) {
    outbuf[0] = '\0';

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(outbuf, outbuf_size, "Failed to run command: %s\n", cmd);
        return;
    }

    size_t len = 0;
    int c;
    while ((c = fgetc(fp)) != EOF && len + 1 < outbuf_size) {
        outbuf[len++] = (char)c;
    }
    outbuf[len] = '\0';
    pclose(fp);

    if (len == 0) {
        snprintf(outbuf, outbuf_size, "(no output)\n");
    }
}

void server_loop(int world_size) {
    int active_clients = world_size - 1;
    char cmd_buf[MAX_CMD];
    char result_buf[MAX_OUTPUT];

    printf("[Server] Starting remote shell for %d clients.\n", active_clients);
    fflush(stdout);

    while (active_clients > 0) {
        MPI_Status status;

        /* Nhận lệnh từ bất kỳ client nào */
        MPI_Recv(cmd_buf, MAX_CMD, MPI_CHAR,
                 MPI_ANY_SOURCE, TAG_CMD,
                 MPI_COMM_WORLD, &status);

        int src = status.MPI_SOURCE;
        cmd_buf[MAX_CMD - 1] = '\0';  // bảo hiểm null-terminate

        printf("[Server] Received command from client %d: '%s'\n",
               src, cmd_buf);
        fflush(stdout);

        /* Nếu client muốn thoát */
        if (strcmp(cmd_buf, "exit") == 0 || strcmp(cmd_buf, "quit") == 0) {
            snprintf(result_buf, sizeof(result_buf),
                     "Client %d disconnected.\n", src);
            MPI_Send(result_buf, (int)strlen(result_buf) + 1, MPI_CHAR,
                     src, TAG_RESULT, MPI_COMM_WORLD);
            active_clients--;
            printf("[Server] Client %d left. Remaining: %d\n",
                   src, active_clients);
            fflush(stdout);
            continue;
        }

        /* Thực thi lệnh và gửi lại kết quả */
        run_command(cmd_buf, result_buf, sizeof(result_buf));

        MPI_Send(result_buf, (int)strlen(result_buf) + 1, MPI_CHAR,
                 src, TAG_RESULT, MPI_COMM_WORLD);
    }

    printf("[Server] All clients disconnected. Shutting down.\n");
    fflush(stdout);
}

/* Script command cho từng client – anh có thể chỉnh cho đẹp hơn */
void client_script(int rank) {
    /* Ví dụ: mỗi client có một list khác nhau */
    const char *script1[] = {
        "hostname",
        "pwd",
        "ls",
        "exit"
    };
    const int script1_len = sizeof(script1) / sizeof(script1[0]);

    const char *script2[] = {
        "whoami",
        "date",
        "uname -a",
        "exit"
    };
    const int script2_len = sizeof(script2) / sizeof(script2[0]);

    const char **script = NULL;
    int script_len = 0;

    if (rank == 1) {
        script = script1;
        script_len = script1_len;
    } else {
        script = script2;
        script_len = script2_len;
    }

    char cmd_buf[MAX_CMD];
    char result_buf[MAX_OUTPUT];
    MPI_Status status;

    printf("[Client %d] Starting scripted remote shell.\n", rank);
    fflush(stdout);

    for (int i = 0; i < script_len; i++) {
        /* Chuẩn bị lệnh */
        memset(cmd_buf, 0, sizeof(cmd_buf));
        strncpy(cmd_buf, script[i], MAX_CMD - 1);

        printf("[Client %d] $ %s\n", rank, cmd_buf);
        fflush(stdout);

        /* Gửi lệnh cho server */
        MPI_Send(cmd_buf, MAX_CMD, MPI_CHAR,
                 0, TAG_CMD, MPI_COMM_WORLD);

        /* Nhận kết quả */
        MPI_Recv(result_buf, MAX_OUTPUT, MPI_CHAR,
                 0, TAG_RESULT, MPI_COMM_WORLD, &status);

        result_buf[MAX_OUTPUT - 1] = '\0';

        printf("[Client %d] --- result ---\n%s\n",
               rank, result_buf);
        fflush(stdout);
    }

    printf("[Client %d] Finished script.\n", rank);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    int rank, size;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 2) {
        if (rank == 0) {
            fprintf(stderr,
                    "Please run with at least 2 processes.\n"
                    "Example: mpirun -np 3 ./mpi_remote_shell\n");
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (rank == 0) {
        server_loop(size);
    } else {
        client_script(rank);
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}
