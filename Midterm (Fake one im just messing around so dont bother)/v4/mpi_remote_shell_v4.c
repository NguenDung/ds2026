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

#define ENC_KEY       0x42   /* toy XOR key */

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

/* ======== Toy XOR "encryption" ======== */
static void xor_buffer(char *buf, int len, unsigned char key) {
    for (int i = 0; i < len; i++) {
        buf[i] ^= key;
    }
}

/* Gửi chuỗi với XOR (gửi đủ MAX_CMD / MAX_OUTPUT cho đơn giản) */
static void send_encrypted_cmd(char *cmd_buf, int dest_rank) {
    xor_buffer(cmd_buf, MAX_CMD, ENC_KEY);
    MPI_Send(cmd_buf, MAX_CMD, MPI_CHAR, dest_rank, TAG_CMD, MPI_COMM_WORLD);
    xor_buffer(cmd_buf, MAX_CMD, ENC_KEY); /* restore local copy */
}

static void recv_encrypted_cmd(char *cmd_buf, MPI_Status *status) {
    MPI_Recv(cmd_buf, MAX_CMD, MPI_CHAR,
             MPI_ANY_SOURCE, TAG_CMD, MPI_COMM_WORLD, status);
    xor_buffer(cmd_buf, MAX_CMD, ENC_KEY);
}

static void send_encrypted_result(char *res_buf, int dest_rank) {
    xor_buffer(res_buf, MAX_OUTPUT, ENC_KEY);
    MPI_Send(res_buf, MAX_OUTPUT, MPI_CHAR, dest_rank, TAG_RESULT, MPI_COMM_WORLD);
    xor_buffer(res_buf, MAX_OUTPUT, ENC_KEY);
}

static void recv_encrypted_result(char *res_buf, int src_rank) {
    MPI_Status status;
    MPI_Recv(res_buf, MAX_OUTPUT, MPI_CHAR,
             src_rank, TAG_RESULT, MPI_COMM_WORLD, &status);
    xor_buffer(res_buf, MAX_OUTPUT, ENC_KEY);
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
        size_t mlen = strlen(outbuf);
        if (mlen + 32 < outbuf_size) {
            snprintf(outbuf + mlen, outbuf_size - mlen,
                     "\n[truncated output]\n");
        }
    }
}

/* ======== Security policy: block some dangerous commands ======== */
static int is_blocked_command(const char *cmd) {
    if (strstr(cmd, "rm -rf") != NULL) return 1;
    if (strstr(cmd, ":(){:|:&};:") != NULL) return 1;
    if (strncmp(cmd, "mkfs", 4) == 0) return 1;
    if (strncmp(cmd, "dd ", 3) == 0 && strstr(cmd, " /dev/") != NULL) return 1;
    return 0;
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
    FILE *logcsv = fopen("server_log.csv", "a");
    if (!logf)  perror("fopen server_log.txt");
    if (!logcsv) perror("fopen server_log.csv");

    printf("[Server] Starting remote shell for %d clients.\n",
           active_clients);
    fflush(stdout);

    char cmd_buf[MAX_CMD];
    char result_buf[MAX_OUTPUT];

    while (active_clients > 0) {
        MPI_Status status;

        recv_encrypted_cmd(cmd_buf, &status);
        int src = status.MPI_SOURCE;
        cmd_buf[MAX_CMD - 1] = '\0';

        /* Logging + history */
        char ts[64];
        timestamp(ts, sizeof(ts));
        if (logf) {
            fprintf(logf, "[%s] client %d: %s\n", ts, src, cmd_buf);
            fflush(logf);
        }
        if (logcsv) {
            fprintf(logcsv, "%s,%d,\"%s\"\n", ts, src, cmd_buf);
            fflush(logcsv);
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
            send_encrypted_result(result_buf, src);
            if (active[src]) {
                active[src] = 0;
                active_clients--;
            }
            printf("[Server] Client %d left. Remaining: %d\n",
                   src, active_clients);
            fflush(stdout);
            continue;
        }

        /* Chặn command nguy hiểm */
        if (is_blocked_command(cmd_buf)) {
            snprintf(result_buf, sizeof(result_buf),
                     "Command blocked by security policy.\n");
            send_encrypted_result(result_buf, src);
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
        } else if (strcmp(cmd_buf, "__help") == 0) {
            snprintf(result_buf, sizeof(result_buf),
                     "Available special commands:\n"
                     "  __help           - show this help\n"
                     "  __clients        - list active clients\n"
                     "  __history        - show recent commands\n"
                     "  __serverinfo     - show server system info\n"
                     "  __get <path>     - read a file on the server\n"
                     "  exit / quit      - close the client session\n");
        } else {
            /* Normal shell command */
            run_command(cmd_buf, result_buf, sizeof(result_buf));
        }

        send_encrypted_result(result_buf, src);
    }

    printf("[Server] All clients disconnected. Shutting down.\n");
    fflush(stdout);

    if (logf) fclose(logf);
    if (logcsv) fclose(logcsv);
}

/* ======== CLIENT SIDE ======== */

/* Đọc script từ file script_<rank>.txt nếu có, ngược lại dùng default */
static int load_script_from_file(int rank, char script[][MAX_CMD], int max_lines) {
    char filename[64];
    snprintf(filename, sizeof(filename), "script_%d.txt", rank);

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return 0; /* không có file */
    }

    int count = 0;
    while (count < max_lines && fgets(script[count], MAX_CMD, fp)) {
        size_t len = strlen(script[count]);
        if (len > 0 && script[count][len - 1] == '\n') {
            script[count][len - 1] = '\0';
            len--;
        }
        if (len == 0 || script[count][0] == '#') {
            continue; /* skip empty & comments */
        }
        count++;
    }
    fclose(fp);
    return count;
}

/* Script sẵn vài lệnh cho mỗi client */
static void scripted_client(int rank) {
    char script[64][MAX_CMD];
    int script_len = load_script_from_file(rank, script, 64);

    if (script_len == 0) {
        /* fallback hard-coded */
        if (rank == 1) {
            const char *def1[] = {
                "__serverinfo",
                "__clients",
                "pwd",
                "__get /etc/hostname",
                "__history",
                "exit"
            };
            script_len = sizeof(def1) / sizeof(def1[0]);
            for (int i = 0; i < script_len; i++) {
                strncpy(script[i], def1[i], MAX_CMD - 1);
                script[i][MAX_CMD - 1] = '\0';
            }
        } else {
            const char *def2[] = {
                "whoami",
                "date",
                "uname -r",
                "__clients",
                "exit"
            };
            script_len = sizeof(def2) / sizeof(def2[0]);
            for (int i = 0; i < script_len; i++) {
                strncpy(script[i], def2[i], MAX_CMD - 1);
                script[i][MAX_CMD - 1] = '\0';
            }
        }
    }

    char cmd_buf[MAX_CMD];
    char result_buf[MAX_OUTPUT];

    printf("[Client %d] Starting scripted remote shell.\n", rank);
    fflush(stdout);

    for (int i = 0; i < script_len; i++) {
        memset(cmd_buf, 0, sizeof(cmd_buf));
        strncpy(cmd_buf, script[i], MAX_CMD - 1);

        printf("[Client %d] $ %s\n", rank, cmd_buf);
        fflush(stdout);

        send_encrypted_cmd(cmd_buf, 0);
        recv_encrypted_result(result_buf, 0);
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

    printf("[Client %d] Starting benchmark: %d echo commands.\n",
           rank, iterations);
    fflush(stdout);

    double t0 = MPI_Wtime();
    for (int i = 0; i < iterations; i++) {
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "echo bench_%d_from_client_%d", i, rank);

        send_encrypted_cmd(cmd_buf, 0);
        recv_encrypted_result(result_buf, 0);
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

    printf("[Client %d] Interactive mode. Type commands, 'exit' to quit.\n",
           rank);
    fflush(stdout);

    while (1) {
        printf("[Client %d]$ ", rank);
        fflush(stdout);

        if (!fgets(cmd_buf, sizeof(cmd_buf), stdin)) {
            strncpy(cmd_buf, "exit", sizeof(cmd_buf));
        } else {
            size_t len = strlen(cmd_buf);
            if (len > 0 && cmd_buf[len - 1] == '\n') {
                cmd_buf[len - 1] = '\0';
            }
            if (cmd_buf[0] == '\0') {
                continue;
            }
        }

        send_encrypted_cmd(cmd_buf, 0);
        recv_encrypted_result(result_buf, 0);
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
                    "Example (scripted):   mpirun -np 3 ./mpi_remote_shell_v4\n"
                    "Example (interactive): mpirun -np 3 ./mpi_remote_shell_v4 interactive\n");
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
