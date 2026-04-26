// version 2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "common.h"
#include "auth.h"
#include "tls.h"
#include "client_handler.h"
#include <sqlite3.h>

#define DB_FILE "authenticate.db"

/* ── globals ─────────────────────────────────────────────────────────── */
static Client client_list[MAX_CLIENTS];
static int    cnt = 0;
static Room   rooms[MAX_ROOMS];
static int    room_cnt = 0;

/* ── log buffer ──────────────────────────────────────────────────────── */
#define MAX_LOG 12
static char log_entries[MAX_LOG][256];
static int  log_cnt = 0;

/* ── room helpers ────────────────────────────────────────────────────── */
static Room *find_or_create_room(const char *name) {
    for (int i = 0; i < room_cnt; i++)
        if (strcmp(rooms[i].name, name) == 0) return &rooms[i];
    if (room_cnt >= MAX_ROOMS) return NULL;
    Room *r = &rooms[room_cnt++];
    memset(r, 0, sizeof(*r));
    strncpy(r->name, name, MAX_ROOM_NAME - 1);
    return r;
}

static void room_add(Room *r, int client_id) {
    for (int i = 0; i < r->cnt; i++)
        if (r->members[i] == client_id) return;
    if (r->cnt < MAX_MEMBERS)
        r->members[r->cnt++] = client_id;
}

static void room_remove(Room *r, int client_id) {
    for (int i = 0; i < r->cnt; i++) {
        if (r->members[i] == client_id) {
            r->members[i] = r->members[--r->cnt];
            return;
        }
    }
}

/* ── client lookup ───────────────────────────────────────────────────── */
static int ipc_fd_for(int client_id) {
    for (int i = 0; i < cnt; i++)
        if (client_list[i].client_id == client_id) return client_list[i].ipc_fd;
    return -1;
}

/* ── broadcast helpers ───────────────────────────────────────────────── */
static void broadcast_room(const char *room_name, const Message *msg, int sender_id) {
    Room *r = find_or_create_room(room_name);
    if (!r) return;
    for (int i = 0; i < r->cnt; i++) {
        if (r->members[i] == sender_id) continue;
        int fd = ipc_fd_for(r->members[i]);
        if (fd >= 0) write(fd, msg, sizeof(*msg));
    }
}

static void send_private(int target_id, const Message *msg) {
    int fd = ipc_fd_for(target_id);
    if (fd >= 0) write(fd, msg, sizeof(*msg));
}

static void send_system(int client_id, const char *text) {
    Message m = {0};
    m.type      = MSG_SYSTEM;
    m.sender_id = 0;
    strncpy(m.buffer, text, MAX_BUFFER - 1);
    int fd = ipc_fd_for(client_id);
    if (fd >= 0) write(fd, &m, sizeof(m));
}

/* ── SIGCHLD ─────────────────────────────────────────────────────────── */
static void sigchld_handler(int s) {
    (void)s;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* ── timestamp ───────────────────────────────────────────────────────── */
static void get_time(char *buf, int len) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, len, "%H:%M:%S", tm);
}

/* ── log event ───────────────────────────────────────────────────────── */
static void log_event(const char *fmt, ...) {
    char timebuf[16];
    get_time(timebuf, sizeof(timebuf));

    char msg[230];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (log_cnt == MAX_LOG) {
        for (int i = 0; i < MAX_LOG - 1; i++)
            strncpy(log_entries[i], log_entries[i + 1], 256);
        log_cnt = MAX_LOG - 1;
    }
    snprintf(log_entries[log_cnt++], 256, "[%s] %s", timebuf, msg);
}

/* ── dashboard ───────────────────────────────────────────────────────── */
static void print_dashboard(void) {
    // clear screen
    printf("\033[2J\033[H");

    // ── header ──
    printf("\033[1;36m╔═══════════════════════════════════════════════════╗\033[0m\n");
    printf("\033[1;36m║\033[0m");
    printf("\033[1;37m        🖧  MULTI-PROCESS CHAT SERVER               \033[0m");
    printf("\033[1;36m║\033[0m\n");
    printf("\033[1;36m╚═══════════════════════════════════════════════════╝\033[0m\n");

    // ── online clients ──
    printf("\033[1;33m  ONLINE CLIENTS: \033[1;32m%d\033[0m\n\n", cnt);

    for (int i = 0; i < cnt; i++) {
        printf("  \033[36m[id:%d]\033[0m  \033[32m%-20s\033[0m %s\n",
               client_list[i].client_id,
               client_list[i].username[0]
                   ? client_list[i].username
                   : "(authenticating)",
               client_list[i].authenticated
                   ? "\033[32m● online\033[0m"
                   : "\033[33m○ authing\033[0m");
    }

    // ── rooms ──
    printf("\n\033[1;36m───────────────────────────────────────────────────\033[0m\n");
    printf("\033[1;33m  ACTIVE ROOMS\033[0m\n");
    printf("\033[1;36m───────────────────────────────────────────────────\033[0m\n");

    if (room_cnt == 0) {
        printf("\033[90m  no active rooms yet\033[0m\n");
    } else {
        for (int i = 0; i < room_cnt; i++) {
            if (rooms[i].cnt == 0) continue;
            printf("  \033[1;32m#%-15s\033[0m (\033[33m%d members\033[0m) → ",
                   rooms[i].name, rooms[i].cnt);
            for (int j = 0; j < rooms[i].cnt; j++) {
                for (int k = 0; k < cnt; k++) {
                    if (client_list[k].client_id == rooms[i].members[j]) {
                        printf("\033[32m%s\033[0m", client_list[k].username);
                        if (j < rooms[i].cnt - 1) printf(", ");
                    }
                }
            }
            printf("\n");
        }
    }

    // ── activity log ──
    printf("\n\033[1;36m───────────────────────────────────────────────────\033[0m\n");
    printf("\033[1;33m  ACTIVITY LOG\033[0m\n");
    printf("\033[1;36m───────────────────────────────────────────────────\033[0m\n");

    for (int i = 0; i < log_cnt; i++) {
        if      (strstr(log_entries[i], "joined"))  printf("\033[32m  %s\033[0m\n",  log_entries[i]);
        else if (strstr(log_entries[i], "left"))    printf("\033[31m  %s\033[0m\n",  log_entries[i]);
        else if (strstr(log_entries[i], "PM"))      printf("\033[35m  %s\033[0m\n",  log_entries[i]);
        else if (strstr(log_entries[i], "LOGIN"))   printf("\033[33m  %s\033[0m\n",  log_entries[i]);
        else if (strstr(log_entries[i], "MSG"))     printf("\033[37m  %s\033[0m\n",  log_entries[i]);
        else                                        printf("\033[90m  %s\033[0m\n",  log_entries[i]);
    }

    printf("\033[1;36m───────────────────────────────────────────────────\033[0m\n");
    fflush(stdout);
}

/* ── route ───────────────────────────────────────────────────────────── */
static void route(const Message *msg) {
    char notice[MAX_BUFFER];
    switch (msg->type) {

        case MSG_JOIN: {
            Room *r = find_or_create_room(msg->room_name);
            if (!r) { send_system(msg->sender_id, "Room limit reached."); return; }
            room_add(r, msg->sender_id);
            snprintf(notice, sizeof(notice),
                     "%s joined room [%s]", msg->username, msg->room_name);
            send_system(msg->sender_id, notice);
            Message note = *msg;
            note.type = MSG_SYSTEM;
            strncpy(note.buffer, notice, MAX_BUFFER - 1);
            broadcast_room(msg->room_name, &note, msg->sender_id);
            log_event("%s joined [%s]", msg->username, msg->room_name);
            break;
        }

        case MSG_LEAVE: {
            Room *r = find_or_create_room(msg->room_name);
            room_remove(r, msg->sender_id);
            snprintf(notice, sizeof(notice),
                     "%s left room [%s]", msg->username, msg->room_name);
            send_system(msg->sender_id, notice);
            Message note = *msg;
            note.type = MSG_SYSTEM;
            strncpy(note.buffer, notice, MAX_BUFFER - 1);
            broadcast_room(msg->room_name, &note, msg->sender_id);
            log_event("%s left [%s]", msg->username, msg->room_name);
            break;
        }

        case MSG_BROADCAST:
            broadcast_room(msg->room_name, msg, msg->sender_id);
            log_event("MSG [%s] %s: %s", msg->room_name, msg->username, msg->buffer);
            break;

        case MSG_PRIVATE:
            send_private(msg->reciever_id, msg);
            log_event("PM: %s → client %d", msg->username, msg->reciever_id);
            break;

        case MSG_SYSTEM:
            if (strncmp(msg->buffer, "__LOGIN__", 9) == 0) {
                for (int i = 0; i < cnt; i++) {
                    if (client_list[i].client_id == msg->sender_id) {
                        strncpy(client_list[i].username, msg->username, MAX_USERNAME - 1);
                        client_list[i].authenticated = 1;
                    }
                }
                log_event("LOGIN: %s (id %d)", msg->username, msg->sender_id);
            }
            break;

        default: break;
    }

    print_dashboard();
}

/* ── main ────────────────────────────────────────────────────────────── */
void error(const char *msg) { perror(msg); exit(1); }

int main(int argc, char *argv[]) {

    if (argc < 2) error("Usage: server <port>");

    /* ── DB ── */
    sqlite3 *db;
    if (sqlite3_open(DB_FILE, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open DB\n");
        exit(1);
    }
    db_init(db);
    sqlite3_close(db);

    /* ── TLS ── */
    SSL_CTX *ctx = create_server_ctx();

    signal(SIGCHLD, sigchld_handler);

    int port   = atoi(argv[1]);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("socket");

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) error("bind");
    listen(sockfd, 5);

    /* ── initial dashboard ── */
    log_event("Server started on port %d", port);
    print_dashboard();

    static int next_id = 1;
    fd_set readfds;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        int maxfd = sockfd;

        for (int i = 0; i < cnt; i++) {
            FD_SET(client_list[i].ipc_fd, &readfds);
            if (client_list[i].ipc_fd > maxfd) maxfd = client_list[i].ipc_fd;
        }

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) continue;

        /* ── new connection ── */
        if (FD_ISSET(sockfd, &readfds)) {
            struct sockaddr_in cli_addr;
            socklen_t clilen = sizeof(cli_addr);
            int newsock = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);

            SSL *ssl = SSL_new(ctx);
            SSL_set_fd(ssl, newsock);

            if (SSL_accept(ssl) <= 0) {
                ERR_print_errors_fp(stderr);
                SSL_free(ssl);
                close(newsock);
                continue;
            }

            int id = next_id++;
            int sp[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, sp);

            pid_t pid = fork();
            if (pid == 0) {
                close(sockfd);
                close(sp[0]);
                handle_client(ssl, sp[1], id);
                exit(0);
            }
            SSL_free(ssl);
            close(newsock);
            close(sp[1]);

            Client c = {0};
            c.client_id = id;
            c.ipc_fd    = sp[0];
            c.pid       = pid;
            client_list[cnt++] = c;

            log_event("New connection → id %d", id);
            print_dashboard();
        }

        /* ── messages from children ── */
        for (int i = 0; i < cnt; i++) {
            if (!FD_ISSET(client_list[i].ipc_fd, &readfds)) continue;
            Message msg;
            int n = read(client_list[i].ipc_fd, &msg, sizeof(msg));
            if (n <= 0) continue;
            route(&msg);
        }
    }

    SSL_CTX_free(ctx);
    return 0;
}