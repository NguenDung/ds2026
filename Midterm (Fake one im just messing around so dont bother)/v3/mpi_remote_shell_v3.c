#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CMD       256
#define MAX_OUTPUT    4096

#define TAG_CMD       0
#define TAG_RESULT    1

#define MAX_CLIENTS   64
#define HISTORY_SIZE  64

/* ======== Utility: timestamp string for logging ======== */
static void timestamp(char *buf, size_t sz) {
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    if (!tm_now) {
        snprintf(buf, sz, "unknown-time");
        return;
    }
    strftime(buf, sz, "%Y-%m-%d %H:%M:%S", tm_now);
}

/* ======== Run shell command on server ======== */
static void run_command(const char *cmd, char *outbuf, size_t outbuf_size) {
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

/* ======== Read file content for __get ======== */
static void read_file_content(const char *path, char *outbuf, size_t outbuf_size) {
    outbuf[0] = '\0';

    FILE *fp = fopen(path, "r");
    if (!fp) {
        snprintf(outbuf, outbuf_size,
                 "Failed to open file: %s\n", path);
        return;
    }

    size_t len = 0;
    int c;
    while ((c = fgetc(fp)) != EOF && len + 1 < outbuf_size) {
        outbuf[len++] = (char)c;
    }
    outbuf[len] = '\0';
    fclose(fp);

    if (len == 0) {
        snprintf(outbuf, outbuf_size, "(empty file)\n");
    } else if (len + 1 == outbuf_size) {
        /* Truncated */
        size_t mlen = strlen(outbuf);
        if (mlen + 32 < outbuf_size) {
            snprintf(outbuf + mlen, outbuf_size - mlen,
                     "\n[truncated output]\n");
        }
    }
}

/* ======== SERVER SIDE ======== */
static void server_loop(int world_size) {
    int active_clients = world_size - 1;
    int active[MAX_CLIENTS] = {0};
    char history[HISTORY_SIZE][MAX_CMD];
    int hist_count = 0;

    for (int i = 1; i < world_size && i < MAX_CLIENTS; i++) {
        active[i] = 1;
    }

    FILE *logf = fopen("server_log.txt", "a");
    if (!logf) {
        perror("fopen server_log.txt");
    }

    printf("[Server] Starting remote shell for %d clients.\n",
           active_clients);
    fflush(stdout);

    char cmd_buf[MAX_CMD];
    char result_buf[MAX_OUTPUT];

    while (active_clients > 0) {
        MPI_Status status;

        MPI_Recv(cmd_buf, MAX_CMD, MPI_CHAR,
                 MPI_ANY_SOURCE, TAG_CMD,
                 MPI_COMM_WORLD, &status);

        int src = status.MPI_SOURCE;
        cmd_buf[MAX_CMD - 1] = '\0';

        /* Logging + history (không lưu exit/quit) */
        char ts[64];
        timestamp(ts, sizeof(ts));
        if (logf) {
            fprintf(logf, "[%s] client %d: %s\n", ts, src, cmd_buf);
            fflush(logf);
        }
        if (cmd_buf[0] != '\0' &&
            strcmp(cmd_buf, "exit") != 0 &&
            strcmp(cmd_buf, "quit") != 0) {
            if (hist_count < HISTORY_SIZE) {
                strncpy(history[hist_count], cmd_buf, MAX_CMD - 1);
                history[hist_count][MAX_CMD - 1] = '\0';
                hist_count++;
            } else {
                for (int i = 1; i < HISTORY_SIZE; i++) {
                    strcpy(history[i - 1], history[i]);
                }
                strncpy(history[HISTORY_SIZE - 1], cmd_buf, MAX_CMD - 1);
                history[HISTORY_SIZE - 1][MAX_CMD - 1] = '\0';
            }
        }

        printf("[Server] Received from client %d: '%s'\n", src, cmd_buf);
        fflush(stdout);

        /* Client thoát */
        if (strcmp(cmd_buf, "exit") == 0 || strcmp(cmd_buf, "quit") == 0) {
            snprintf(result_buf, sizeof(result_buf),
                     "Client %d disconnected.\n", src);
            MPI_Send(result_buf, (int)strlen(result_buf) + 1, MPI_CHAR,
                     src, TAG_RESULT, MPI_COMM_WORLD);
            if (active[src]) {
                active[src] = 0;
                active_clients--;
            }
            printf("[Server] Client %d left. Remaining: %d\n",
                   src, active_clients);
            fflush(stdout);
            continue;
        }

        /* ====== Special control commands ====== */
        if (strcmp(cmd_buf, "__clients") == 0) {
            int len = snprintf(result_buf, sizeof(result_buf),
                               "Active clients: ");
            for (int i = 1; i < world_size; i++) {
                if (active[i]) {
                    len += snprintf(result_buf + len,
                                    sizeof(result_buf) - len,
                                    "%d ", i);
                }
            }
            snprintf(result_buf + len,
                     sizeof(result_buf) - len, "\n");
        } else if (strcmp(cmd_buf, "__history") == 0) {
            int len = snprintf(result_buf, sizeof(result_buf),
                               "Command history (max %d):\n",
                               HISTORY_SIZE);
            for (int i = 0; i < hist_count && len < (int)sizeof(result_buf); i++) {
                len += snprintf(result_buf + len,
                                sizeof(result_buf) - len,
                                "%2d: %s\n", i + 1, history[i]);
            }
        } else if (strcmp(cmd_buf, "__serverinfo") == 0) {
            run_command("uname -a; echo; hostname", result_buf,
                        sizeof(result_buf));
        } else if (strncmp(cmd_buf, "__get ", 6) == 0) {
            const char *path = cmd_buf + 6;
            while (*path == ' ') path++;
            if (*path == '\0') {
                snprintf(result_buf, sizeof(result_buf),
                         "Usage: __get <path>\n");
            } else {
                read_file_content(path, result_buf, sizeof(result_buf));
            }
        } else {
            /* Normal shell command */
            run_command(cmd_buf, result_buf, sizeof(result_buf));
        }

        MPI_Send(result_buf, (int)strlen(result_buf) + 1, MPI_CHAR,
                 src, TAG_RESULT, MPI_COMM_WORLD);
    }

    printf("[Server] All clients disconnected. Shutting down.\n");
    fflush(stdout);

    if (logf) fclose(logf);
}

/* ======== CLIENT SIDE ======== */

/* Script sẵn vài lệnh cho mỗi client */
static void scripted_client(int rank) {
    const char *script1[] = {
        "__serverinfo",
        "__clients",
        "pwd",
        "__get /etc/hostname",
        "__history",
        "exit"
    };
    const int script1_len = sizeof(script1) / sizeof(script1[0]);

    const char *script2[] = {
        "whoami",
        "date",
        "uname -r",
        "__clients",
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
        memset(cmd_buf, 0, sizeof(cmd_buf));
        strncpy(cmd_buf, script[i], MAX_CMD - 1);

        printf("[Client %d] $ %s\n", rank, cmd_buf);
        fflush(stdout);

        MPI_Send(cmd_buf, MAX_CMD, MPI_CHAR, 0, TAG_CMD, MPI_COMM_WORLD);

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

/* Benchmark: gửi nhiều lệnh nhỏ, đo throughput */
static void benchmark_client(int rank, int iterations) {
    char cmd_buf[MAX_CMD];
    char result_buf[MAX_OUTPUT];
    MPI_Status status;

    printf("[Client %d] Starting benchmark: %d echo commands.\n",
           rank, iterations);
    fflush(stdout);

    double t0 = MPI_Wtime();
    for (int i = 0; i < iterations; i++) {
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "echo bench_%d_from_client_%d", i, rank);

        MPI_Send(cmd_buf, MAX_CMD, MPI_CHAR,
                 0, TAG_CMD, MPI_COMM_WORLD);

        MPI_Recv(result_buf, MAX_OUTPUT, MPI_CHAR,
                 0, TAG_RESULT, MPI_COMM_WORLD, &status);
    }
    double t1 = MPI_Wtime();
    double elapsed = t1 - t0;
    double cps = (elapsed > 0.0) ? (iterations / elapsed) : 0.0;

    printf("[Client %d] Benchmark done: time = %.4f s, "
           "throughput = %.2f cmd/s\n",
           rank, elapsed, cps);
    fflush(stdout);
}

/* "Level 3" scripted + benchmark client */
static void client_level3_scripted(int rank) {
    scripted_client(rank);

    /* benchmark chỉ cho 1 client để log đỡ loạn, ví dụ rank 1 */
    if (rank == 1) {
        benchmark_client(rank, 50);
    }
}

/* Interactive client mode */
static void client_interactive(int rank) {
    char cmd_buf[MAX_CMD];
    char result_buf[MAX_OUTPUT];
    MPI_Status status;

    printf("[Client %d] Interactive mode. Type commands, 'exit' to quit.\n",
           rank);
    fflush(stdout);

    while (1) {
        printf("[Client %d]$ ", rank);
        fflush(stdout);

        if (!fgets(cmd_buf, sizeof(cmd_buf), stdin)) {
            /* EOF -> coi như exit */
            strncpy(cmd_buf, "exit", sizeof(cmd_buf));
        } else {
            /* bỏ '\n' */
            size_t len = strlen(cmd_buf);
            if (len > 0 && cmd_buf[len - 1] == '\n') {
                cmd_buf[len - 1] = '\0';
            }
            if (cmd_buf[0] == '\0') {
                continue; /* bỏ lệnh rỗng */
            }
        }

        MPI_Send(cmd_buf, MAX_CMD, MPI_CHAR,
                 0, TAG_CMD, MPI_COMM_WORLD);

        MPI_Recv(result_buf, MAX_OUTPUT, MPI_CHAR,
                 0, TAG_RESULT, MPI_COMM_WORLD, &status);
        result_buf[MAX_OUTPUT - 1] = '\0';

        printf("[Client %d] --- result ---\n%s\n",
               rank, result_buf);
        fflush(stdout);

        if (strcmp(cmd_buf, "exit") == 0 ||
            strcmp(cmd_buf, "quit") == 0) {
            break;
        }
    }

    printf("[Client %d] Interactive session ended.\n", rank);
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
                    "Example (scripted):   mpirun -np 3 ./mpi_remote_shell_v3\n"
                    "Example (interactive): mpirun -np 3 ./mpi_remote_shell_v3 interactive\n");
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    if (size > MAX_CLIENTS) {
        if (rank == 0) {
            fprintf(stderr,
                    "MAX_CLIENTS (%d) exceeded, "
                    "please recompile with higher value.\n",
                    MAX_CLIENTS);
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    int interactive_mode = 0;
    if (argc > 1 && strcmp(argv[1], "interactive") == 0) {
        interactive_mode = 1;
    }

    if (rank == 0) {
        server_loop(size);
    } else {
        if (interactive_mode) {
            client_interactive(rank);
        } else {
            client_level3_scripted(rank);
        }
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}
