#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CMD       256
#define MAX_OUTPUT    4096

/* Tags cho từng loại message */
#define TAG_CMD_CLIENT    0   /* client -> dispatcher */
#define TAG_RESULT_CLIENT 1   /* dispatcher -> client */
#define TAG_CMD_WORKER    2   /* dispatcher -> worker */
#define TAG_RESULT_WORKER 3   /* worker -> dispatcher */

#define MAX_CLIENTS   64
#define HISTORY_SIZE  64

#define ENC_KEY       0x42   /* toy XOR key */

/* Struct chuyển job giữa dispatcher <-> worker */
typedef struct {
    int  client_rank;
    char cmd[MAX_CMD];
} WorkerRequest;

typedef struct {
    int  client_rank;
    char result[MAX_OUTPUT];
} WorkerResponse;

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

/* ======== Toy XOR "encryption" (client <-> dispatcher) ======== */
static void xor_buffer(char *buf, int len, unsigned char key) {
    for (int i = 0; i < len; i++) {
        buf[i] ^= key;
    }
}

static void send_encrypted_cmd_from_client(char *cmd_buf, int dest_rank) {
    xor_buffer(cmd_buf, MAX_CMD, ENC_KEY);
    MPI_Send(cmd_buf, MAX_CMD, MPI_CHAR, dest_rank, TAG_CMD_CLIENT, MPI_COMM_WORLD);
    xor_buffer(cmd_buf, MAX_CMD, ENC_KEY); /* restore local copy */
}

static void recv_encrypted_cmd_at_dispatcher(char *cmd_buf, MPI_Status *status) {
    MPI_Recv(cmd_buf, MAX_CMD, MPI_CHAR,
             MPI_ANY_SOURCE, TAG_CMD_CLIENT, MPI_COMM_WORLD, status);
    xor_buffer(cmd_buf, MAX_CMD, ENC_KEY);
}

static void send_encrypted_result_from_dispatcher(char *res_buf, int dest_rank) {
    xor_buffer(res_buf, MAX_OUTPUT, ENC_KEY);
    MPI_Send(res_buf, MAX_OUTPUT, MPI_CHAR, dest_rank, TAG_RESULT_CLIENT, MPI_COMM_WORLD);
    xor_buffer(res_buf, MAX_OUTPUT, ENC_KEY);
}

static void recv_encrypted_result_at_client(char *res_buf, int src_rank) {
    MPI_Status status;
    MPI_Recv(res_buf, MAX_OUTPUT, MPI_CHAR,
             src_rank, TAG_RESULT_CLIENT, MPI_COMM_WORLD, &status);
    xor_buffer(res_buf, MAX_OUTPUT, ENC_KEY);
}

/* ======== Run shell command on worker ======== */
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

/* ======== WORKER SIDE ======== */
static void worker_loop(int rank) {
    char history[HISTORY_SIZE][MAX_CMD];
    int hist_count = 0;

    char logname[64];
    snprintf(logname, sizeof(logname), "worker_%d_log.txt", rank);
    FILE *logf = fopen(logname, "a");
    if (!logf) {
        perror("fopen worker log");
    }

    printf("[Worker %d] Started.\n", rank);
    fflush(stdout);

    while (1) {
        WorkerRequest req;
        MPI_Status status;

        MPI_Recv(&req, sizeof(req), MPI_BYTE,
                 0, TAG_CMD_WORKER, MPI_COMM_WORLD, &status);

        /* Shutdown signal từ dispatcher */
        if (req.client_rank == -1 &&
            strcmp(req.cmd, "__shutdown_worker") == 0) {
            printf("[Worker %d] Received shutdown.\n", rank);
            fflush(stdout);
            break;
        }

        const char *cmd = req.cmd;
        int client_rank = req.client_rank;

        /* Logging history tại worker */
        if (cmd[0] != '\0' &&
            strcmp(cmd, "exit") != 0 &&
            strcmp(cmd, "quit") != 0) {
            if (hist_count < HISTORY_SIZE) {
                strncpy(history[hist_count], cmd, MAX_CMD - 1);
                history[hist_count][MAX_CMD - 1] = '\0';
                hist_count++;
            } else {
                for (int i = 1; i < HISTORY_SIZE; i++) {
                    strcpy(history[i - 1], history[i]);
                }
                strncpy(history[HISTORY_SIZE - 1], cmd, MAX_CMD - 1);
                history[HISTORY_SIZE - 1][MAX_CMD - 1] = '\0';
            }
        }

        if (logf) {
            char ts[64];
            timestamp(ts, sizeof(ts));
            fprintf(logf, "[%s] from client %d: %s\n", ts, client_rank, cmd);
            fflush(logf);
        }

        printf("[Worker %d] Handling command from client %d: '%s'\n",
               rank, client_rank, cmd);
        fflush(stdout);

        char result_buf[MAX_OUTPUT];

        /* Policy: chặn lệnh nguy hiểm */
        if (is_blocked_command(cmd)) {
            snprintf(result_buf, sizeof(result_buf),
                     "Command blocked by security policy.\n");
        }
        /* Special commands handled at worker */
        else if (strcmp(cmd, "__history") == 0) {
            int len = snprintf(result_buf, sizeof(result_buf),
                               "Worker %d history (max %d):\n",
                               rank, HISTORY_SIZE);
            for (int i = 0; i < hist_count && len < (int)sizeof(result_buf); i++) {
                len += snprintf(result_buf + len,
                                sizeof(result_buf) - len,
                                "%2d: %s\n", i + 1, history[i]);
            }
        } else if (strcmp(cmd, "__serverinfo") == 0) {
            run_command("uname -a; echo; hostname", result_buf,
                        sizeof(result_buf));
        } else if (strncmp(cmd, "__get ", 6) == 0) {
            const char *path = cmd + 6;
            while (*path == ' ') path++;
            if (*path == '\0') {
                snprintf(result_buf, sizeof(result_buf),
                         "Usage: __get <path>\n");
            } else {
                read_file_content(path, result_buf, sizeof(result_buf));
            }
        } else if (strcmp(cmd, "__help") == 0) {
            snprintf(result_buf, sizeof(result_buf),
                     "Available special commands:\n"
                     "  __help           - show this help\n"
                     "  __clients        - list active clients (handled by dispatcher)\n"
                     "  __history        - show worker command history\n"
                     "  __serverinfo     - show server system info\n"
                     "  __get <path>     - read a file on the server\n"
                     "  exit / quit      - close the client session\n");
        } else {
            /* Normal shell command */
            run_command(cmd, result_buf, sizeof(result_buf));
        }

        WorkerResponse resp;
        resp.client_rank = client_rank;
        strncpy(resp.result, result_buf, MAX_OUTPUT - 1);
        resp.result[MAX_OUTPUT - 1] = '\0';

        MPI_Send(&resp, sizeof(resp), MPI_BYTE,
                 0, TAG_RESULT_WORKER, MPI_COMM_WORLD);
    }

    if (logf) fclose(logf);
    printf("[Worker %d] Exiting.\n", rank);
    fflush(stdout);
}

/* ======== DISPATCHER SIDE (rank 0) ======== */
static void dispatcher_loop(int world_size, int num_workers) {
    int active_clients[MAX_CLIENTS] = {0};
    int total_clients = 0;

    /* client ranks = (num_workers + 1) .. (world_size - 1) */
    for (int r = num_workers + 1; r < world_size && r < MAX_CLIENTS; r++) {
        active_clients[r] = 1;
        total_clients++;
    }

    FILE *logf = fopen("dispatcher_log.txt", "a");
    FILE *logcsv = fopen("dispatcher_log.csv", "a");
    if (!logf)  perror("fopen dispatcher_log.txt");
    if (!logcsv) perror("fopen dispatcher_log.csv");

    printf("[Dispatcher] Started with %d workers, %d clients.\n",
           num_workers, total_clients);
    fflush(stdout);

    char cmd_buf[MAX_CMD];
    char result_buf[MAX_OUTPUT];
    int next_worker_index = 0;

    while (total_clients > 0) {
        MPI_Status status;

        /* Nhận lệnh từ bất kỳ client nào */
        recv_encrypted_cmd_at_dispatcher(cmd_buf, &status);
        int client_rank = status.MPI_SOURCE;
        cmd_buf[MAX_CMD - 1] = '\0';

        /* ignore if rank not in client range */
        if (client_rank <= num_workers || client_rank >= world_size) {
            continue;
        }

        char ts[64];
        timestamp(ts, sizeof(ts));
        if (logf) {
            fprintf(logf, "[%s] from client %d: %s\n", ts, client_rank, cmd_buf);
            fflush(logf);
        }
        if (logcsv) {
            fprintf(logcsv, "%s,%d,\"%s\"\n", ts, client_rank, cmd_buf);
            fflush(logcsv);
        }

        printf("[Dispatcher] Received from client %d: '%s'\n",
               client_rank, cmd_buf);
        fflush(stdout);

        /* Client thoát */
        if (strcmp(cmd_buf, "exit") == 0 || strcmp(cmd_buf, "quit") == 0) {
            snprintf(result_buf, sizeof(result_buf),
                     "Client %d disconnected.\n", client_rank);
            send_encrypted_result_from_dispatcher(result_buf, client_rank);
            if (active_clients[client_rank]) {
                active_clients[client_rank] = 0;
                total_clients--;
            }
            printf("[Dispatcher] Client %d left. Remaining clients: %d\n",
                   client_rank, total_clients);
            fflush(stdout);
            continue;
        }

        /* __clients handled tại dispatcher (vì nó biết danh sách client) */
        if (strcmp(cmd_buf, "__clients") == 0) {
            int len = snprintf(result_buf, sizeof(result_buf),
                               "Active clients: ");
            for (int r = num_workers + 1; r < world_size; r++) {
                if (active_clients[r]) {
                    len += snprintf(result_buf + len,
                                    sizeof(result_buf) - len,
                                    "%d ", r);
                }
            }
            snprintf(result_buf + len,
                     sizeof(result_buf) - len, "\n");
            send_encrypted_result_from_dispatcher(result_buf, client_rank);
            continue;
        }

        /* Các lệnh khác forward xuống worker */
        int worker_rank = 1 + (next_worker_index % num_workers);
        next_worker_index++;

        WorkerRequest req;
        req.client_rank = client_rank;
        strncpy(req.cmd, cmd_buf, MAX_CMD - 1);
        req.cmd[MAX_CMD - 1] = '\0';

        MPI_Send(&req, sizeof(req), MPI_BYTE,
                 worker_rank, TAG_CMD_WORKER, MPI_COMM_WORLD);

        WorkerResponse resp;
        MPI_Status wstatus;
        MPI_Recv(&resp, sizeof(resp), MPI_BYTE,
                 worker_rank, TAG_RESULT_WORKER, MPI_COMM_WORLD, &wstatus);

        /* Forward kết quả lại cho đúng client */
        strncpy(result_buf, resp.result, MAX_OUTPUT - 1);
        result_buf[MAX_OUTPUT - 1] = '\0';

        send_encrypted_result_from_dispatcher(result_buf, resp.client_rank);
    }

    /* Gửi tín hiệu shutdown cho tất cả worker */
    for (int w = 1; w <= num_workers; w++) {
        WorkerRequest req;
        req.client_rank = -1;
        strncpy(req.cmd, "__shutdown_worker", MAX_CMD - 1);
        req.cmd[MAX_CMD - 1] = '\0';

        MPI_Send(&req, sizeof(req), MPI_BYTE,
                 w, TAG_CMD_WORKER, MPI_COMM_WORLD);
    }

    if (logf) fclose(logf);
    if (logcsv) fclose(logcsv);

    printf("[Dispatcher] All clients disconnected. Shutting down workers.\n");
    fflush(stdout);
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

/* Scripted client */
static void scripted_client(int rank, int dispatcher_rank) {
    char script[64][MAX_CMD];
    int script_len = load_script_from_file(rank, script, 64);

    if (script_len == 0) {
        /* fallback hard-coded */
        if (rank == 2) {
            const char *def1[] = {
                "__serverinfo",
                "__clients",
                "pwd",
                "__get /etc/hostname",
                "__history",
                "__help",
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

        send_encrypted_cmd_from_client(cmd_buf, dispatcher_rank);
        recv_encrypted_result_at_client(result_buf, dispatcher_rank);
        result_buf[MAX_OUTPUT - 1] = '\0';

        printf("[Client %d] --- result ---\n%s\n",
               rank, result_buf);
        fflush(stdout);
    }

    printf("[Client %d] Finished script.\n", rank);
    fflush(stdout);
}

/* Benchmark: gửi nhiều lệnh nhỏ, đo throughput */
static void benchmark_client(int rank, int dispatcher_rank, int iterations) {
    char cmd_buf[MAX_CMD];
    char result_buf[MAX_OUTPUT];

    printf("[Client %d] Starting benchmark: %d echo commands.\n",
           rank, iterations);
    fflush(stdout);

    double t0 = MPI_Wtime();
    for (int i = 0; i < iterations; i++) {
        snprintf(cmd_buf, sizeof(cmd_buf),
                 "echo bench_%d_from_client_%d", i, rank);

        send_encrypted_cmd_from_client(cmd_buf, dispatcher_rank);
        recv_encrypted_result_at_client(result_buf, dispatcher_rank);
    }
    double t1 = MPI_Wtime();
    double elapsed = t1 - t0;
    double cps = (elapsed > 0.0) ? (iterations / elapsed) : 0.0;

    printf("[Client %d] Benchmark done: time = %.4f s, "
           "throughput = %.2f cmd/s\n",
           rank, elapsed, cps);
    fflush(stdout);
}

/* Interactive client mode */
static void client_interactive(int rank, int dispatcher_rank) {
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

        send_encrypted_cmd_from_client(cmd_buf, dispatcher_rank);
        recv_encrypted_result_at_client(result_buf, dispatcher_rank);
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

/* Client wrapper: scripted + benchmark hoặc interactive */
static void client_main(int rank, int world_size, int num_workers,
                        int interactive_mode) {
    int dispatcher_rank = 0;

    if (interactive_mode) {
        client_interactive(rank, dispatcher_rank);
    } else {
        scripted_client(rank, dispatcher_rank);

        /* Chỉ 1 client benchmark cho gọn, ví dụ client đầu tiên */
        int first_client_rank = num_workers + 1;
        if (rank == first_client_rank) {
            benchmark_client(rank, dispatcher_rank, 50);
        }
    }
}

/* ======== MAIN ======== */
int main(int argc, char *argv[]) {
    int rank, size;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int interactive_mode = 0;
    if (argc > 1 && strcmp(argv[1], "interactive") == 0) {
        interactive_mode = 1;
    }

    /* Cần tối thiểu: 1 dispatcher + 1 worker + 1 client = 3 process */
    if (size < 3) {
        if (rank == 0) {
            fprintf(stderr,
                    "Please run with at least 3 processes.\n"
                    "Example (scripted):   mpirun -np 4 ./mpi_remote_shell_v5\n"
                    "Example (interactive): mpirun -np 4 ./mpi_remote_shell_v5 interactive\n");
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    /* Chọn số worker: ở đây cho đơn giản: 1 hoặc 2 tuỳ size */
    int num_workers = 1;
    if (size >= 5) {
        num_workers = 2;
    }
    /* Đảm bảo vẫn còn ít nhất 1 client */
    if (num_workers > size - 2) {
        num_workers = size - 2;
    }

    if (rank == 0) {
        dispatcher_loop(size, num_workers);
    } else if (rank >= 1 && rank <= num_workers) {
        worker_loop(rank);
    } else {
        client_main(rank, size, num_workers, interactive_mode);
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}

