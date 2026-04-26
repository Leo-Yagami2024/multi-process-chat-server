#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "common.h"
#include "client_handler.h"
#include "auth.h"

/* ── auth handshake ──────────────────────────────────────────────────── */
int do_auth(SSL *ssl, char *out_username) {
    char buf[256];
    SSL_write(ssl, "AUTH: LOGIN <user> <pass> | REGISTER <user> <pass>\n", 51);

    for (int attempts = 0; attempts < 3; attempts++) {
        memset(buf, 0, sizeof(buf));
        int n = SSL_read(ssl, buf, sizeof(buf) - 1);
        if (n <= 0) return 0;

        buf[strcspn(buf, "\r\n")] = 0;

        char cmd[16], user[MAX_USERNAME], pass[MAX_USERNAME];
        if (sscanf(buf, "%15s %31s %31s", cmd, user, pass) != 3) {
            SSL_write(ssl, "FAIL: bad format\n", 17);
            continue;
        }

        int ok = 0;
        if (strcmp(cmd, "LOGIN") == 0)         ok = authenticate(user, pass);
        else if (strcmp(cmd, "REGISTER") == 0) ok = register_user(user, pass);

        if (ok) {
            strncpy(out_username, user, MAX_USERNAME - 1);
            char reply[64];
            snprintf(reply, sizeof(reply), "OK %s\n", user);
            SSL_write(ssl, reply, strlen(reply));
            return 1;
        }
        SSL_write(ssl, "FAIL: invalid credentials\n", 26);
    }
    return 0;
}

/* ── message parsing ─────────────────────────────────────────────────── */
static int parse_message(const char *raw, Message *msg,
                          const char *active_room) {
    if (raw[0] == '/') {
        char cmd[16], arg1[MAX_ROOM_NAME], rest[MAX_BUFFER];
        int parts = sscanf(raw + 1, "%15s %31s %511[^\n]", cmd, arg1, rest);

        if (strcmp(cmd, "join") == 0 && parts >= 2) {
            msg->type = MSG_JOIN;
            strncpy(msg->room_name, arg1, MAX_ROOM_NAME - 1);
            return 1;
        }
        if (strcmp(cmd, "leave") == 0 && parts >= 2) {
            msg->type = MSG_LEAVE;
            strncpy(msg->room_name, arg1, MAX_ROOM_NAME - 1);
            return 1;
        }
        if (strcmp(cmd, "msg") == 0 && parts == 3) {
            msg->type        = MSG_PRIVATE;
            msg->reciever_id = atoi(arg1);
            strncpy(msg->buffer, rest, MAX_BUFFER - 1);
            return 1;
        }
        return 0;
    }

    if (active_room[0] == '\0') {
        return -1;
    }
    msg->type = MSG_BROADCAST;
    strncpy(msg->room_name, active_room, MAX_ROOM_NAME - 1);
    strncpy(msg->buffer, raw, MAX_BUFFER - 1);
    return 1;
}

/* ── main handler loop ───────────────────────────────────────────────── */
void handle_client(SSL *ssl, int ipc_fd, int client_id) {
    char username[MAX_USERNAME] = {0};

    if (!do_auth(ssl, username)) {
        SSL_write(ssl, "AUTH FAILED. Closing.\n", 22);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(ipc_fd);
        return;
    }

    {
        Message login_msg = {0};
        login_msg.type      = MSG_SYSTEM;
        login_msg.sender_id = client_id;
        strncpy(login_msg.username, username, MAX_USERNAME - 1);
        snprintf(login_msg.buffer, MAX_BUFFER, "__LOGIN__ %s", username);
        write(ipc_fd, &login_msg, sizeof(login_msg));
    }

    char active_room[MAX_ROOM_NAME] = {0};
    char raw[MAX_BUFFER];

    int client_fd = SSL_get_fd(ssl);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client_fd, &readfds);
        FD_SET(ipc_fd,    &readfds);
        int maxfd = (client_fd > ipc_fd) ? client_fd : ipc_fd;

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("select in handle_client");
            break;
        }

        /* ── input from this client ── */
        if (FD_ISSET(client_fd, &readfds)) {
            memset(raw, 0, MAX_BUFFER);
            int n = SSL_read(ssl, raw, MAX_BUFFER - 1);
            if (n <= 0) {
                printf("Client %d (%s) disconnected.\n", client_id, username);
                break;
            }

            raw[strcspn(raw, "\r\n")] = 0;

            Message msg = {0};
            msg.sender_id = client_id;
            strncpy(msg.username, username, MAX_USERNAME - 1);

            int r = parse_message(raw, &msg, active_room);
            if (r == -1) {
                SSL_write(ssl, "[!] Join a room first: /join <room>\n", 36);
                continue;
            }
            if (r == 0) continue;

            if (msg.type == MSG_JOIN)
                strncpy(active_room, msg.room_name, MAX_ROOM_NAME - 1);

            write(ipc_fd, &msg, sizeof(msg));
        }

        /* ── broadcast/private from parent ── */
        if (FD_ISSET(ipc_fd, &readfds)) {
            Message msg;
            int n = read(ipc_fd, &msg, sizeof(msg));
            if (n <= 0) break;

            char out[MAX_BUFFER + 64];
            if (msg.type == MSG_PRIVATE) {
                snprintf(out, sizeof(out),
                         "[PM from %s]: %s\n", msg.username, msg.buffer);
            } else if (msg.type == MSG_SYSTEM) {
                snprintf(out, sizeof(out), "[System]: %s\n", msg.buffer);
            } else {
                snprintf(out, sizeof(out),
                         "[%s] %s: %s\n", msg.room_name, msg.username, msg.buffer);
            }
            SSL_write(ssl, out, strlen(out));
        }
    }
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(ipc_fd);
}