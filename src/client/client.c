
// version 2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ncurses.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "common.h"
#include "tls.h"

/* ── color pairs ─────────────────────────────────────────────────────── */
#define COL_BORDER   1   // cyan
#define COL_CHAT     2   // green
#define COL_SYSTEM   3   // yellow
#define COL_PM       4   // magenta
#define COL_ERROR    5   // red
#define COL_INACTIVE 6   // white
#define COL_HEADER   7   // cyan bold

/* ── windows ─────────────────────────────────────────────────────────── */
static WINDOW *chat_win;
static WINDOW *side_win;
static WINDOW *input_win;
static WINDOW *chat_box;
static WINDOW *side_box;
static WINDOW *input_box;

/* ── state ───────────────────────────────────────────────────────────── */
static char  rooms_joined[MAX_ROOMS][MAX_ROOM_NAME];
static int   room_cnt = 0;
static char  active_room[MAX_ROOM_NAME] = {0};
static char  username[MAX_USERNAME]     = {0};
static char  input_buf[MAX_BUFFER]      = {0};
static int   input_len = 0;

static void init_windows(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int side_w  = 22;              // ← increased from 20
    int chat_w  = cols - side_w;
    int chat_h  = rows - 3;
    int input_h = 3;

    chat_box  = newwin(chat_h,     chat_w,     0,      0);
    side_box  = newwin(chat_h,     side_w,     0,      chat_w);
    input_box = newwin(input_h,    cols,       chat_h, 0);

    chat_win  = newwin(chat_h - 2, chat_w - 2, 1,      1);
    side_win  = newwin(chat_h - 2, side_w - 2, 1,      chat_w + 1);
    input_win = newwin(1,          cols - 4,   chat_h + 1, 2);

    scrollok(chat_win, TRUE);
    keypad(input_win, TRUE);

    wattron(chat_box, COLOR_PAIR(COL_BORDER));
    box(chat_box, 0, 0);
    mvwprintw(chat_box, 0, 2, " CHAT ");
    wattroff(chat_box, COLOR_PAIR(COL_BORDER));

    wattron(side_box, COLOR_PAIR(COL_BORDER));
    box(side_box, 0, 0);
    mvwprintw(side_box, 0, 2, " INFO ");
    wattroff(side_box, COLOR_PAIR(COL_BORDER));

    wattron(input_box, COLOR_PAIR(COL_SYSTEM));
    box(input_box, 0, 0);
    mvwprintw(input_box, 0, 2, " You ");
    wattroff(input_box, COLOR_PAIR(COL_SYSTEM));

    wrefresh(chat_box);
    wrefresh(side_box);
    wrefresh(input_box);
    wrefresh(chat_win);
    wrefresh(side_win);
    wrefresh(input_win);
}

static void redraw_sidebar(void) {
    wclear(side_win);
    int w = getmaxx(side_win);     // actual inner width
    int h = getmaxy(side_win);

    // username
    wattron(side_win, COLOR_PAIR(COL_SYSTEM) | A_BOLD);
    mvwprintw(side_win, 0, 0, "%-*.*s", w, w, username);
    wattroff(side_win, COLOR_PAIR(COL_SYSTEM) | A_BOLD);

    // rooms header
    wattron(side_win, COLOR_PAIR(COL_HEADER) | A_BOLD);
    mvwprintw(side_win, 2, 0, "ROOMS");
    wattroff(side_win, COLOR_PAIR(COL_HEADER) | A_BOLD);

    // ASCII divider — no unicode
    wattron(side_win, COLOR_PAIR(COL_BORDER));
    mvwhline(side_win, 3, 0, '-', w);   // ← use mvwhline instead of unicode string
    wattroff(side_win, COLOR_PAIR(COL_BORDER));

    for (int i = 0; i < room_cnt; i++) {
        if (strcmp(rooms_joined[i], active_room) == 0) {
            wattron(side_win, COLOR_PAIR(COL_CHAT) | A_BOLD);
            mvwprintw(side_win, 4 + i, 0, "> %-*.*s", w-2, w-2, rooms_joined[i]);
            wattroff(side_win, COLOR_PAIR(COL_CHAT) | A_BOLD);
        } else {
            wattron(side_win, COLOR_PAIR(COL_INACTIVE));
            mvwprintw(side_win, 4 + i, 0, "  %-*.*s", w-2, w-2, rooms_joined[i]);
            wattroff(side_win, COLOR_PAIR(COL_INACTIVE));
        }
    }

    // bottom divider + commands
    wattron(side_win, COLOR_PAIR(COL_BORDER));
    mvwhline(side_win, h - 5, 0, '-', w);  // ← ASCII line
    wattroff(side_win, COLOR_PAIR(COL_BORDER));

    wattron(side_win, COLOR_PAIR(COL_SYSTEM));
    mvwprintw(side_win, h - 4, 0, "/join <room>");
    mvwprintw(side_win, h - 3, 0, "/leave <room>");
    mvwprintw(side_win, h - 2, 0, "/msg <id> <txt>");
    wattroff(side_win, COLOR_PAIR(COL_SYSTEM));

    wrefresh(side_win);
}

/* ── print message to chat window ────────────────────────────────────── */
static void chat_print(const char *msg) {
    if (strncmp(msg, "[System]", 8) == 0) {
        wattron(chat_win, COLOR_PAIR(COL_SYSTEM));
        wprintw(chat_win, "%s\n", msg);
        wattroff(chat_win, COLOR_PAIR(COL_SYSTEM));
    } else if (strncmp(msg, "[PM", 3) == 0) {
        wattron(chat_win, COLOR_PAIR(COL_PM) | A_BOLD);
        wprintw(chat_win, "%s\n", msg);
        wattroff(chat_win, COLOR_PAIR(COL_PM) | A_BOLD);
    } else if (strncmp(msg, "[!", 2) == 0) {
        wattron(chat_win, COLOR_PAIR(COL_ERROR));
        wprintw(chat_win, "%s\n", msg);
        wattroff(chat_win, COLOR_PAIR(COL_ERROR));
    } else {
        wattron(chat_win, COLOR_PAIR(COL_CHAT));
        wprintw(chat_win, "%s\n", msg);
        wattroff(chat_win, COLOR_PAIR(COL_CHAT));
    }
    wrefresh(chat_win);
}

/* ── redraw input bar ────────────────────────────────────────────────── */
static void redraw_input(void) {
    wclear(input_win);
    wattron(input_win, COLOR_PAIR(COL_SYSTEM));
    mvwprintw(input_win, 0, 0, "%.*s", input_len, input_buf);
    wattroff(input_win, COLOR_PAIR(COL_SYSTEM));
    wmove(input_win, 0, input_len);
    wrefresh(input_win);
}


/* ── auth over TLS before ncurses starts ─────────────────────────────── */
static int do_auth_client(SSL *ssl) {
    char buf[256] = {0};

    // receive prompt
    SSL_read(ssl, buf, sizeof(buf) - 1);
    printf("%s", buf);
    fflush(stdout);

    // send credentials
    memset(buf, 0, sizeof(buf));
    fgets(buf, sizeof(buf), stdin);
    SSL_write(ssl, buf, strlen(buf));

    // receive OK/FAIL
    memset(buf, 0, sizeof(buf));
    SSL_read(ssl, buf, sizeof(buf) - 1);
    printf("%s", buf);
    fflush(stdout);

    if (strncmp(buf, "OK", 2) == 0) {
        // extract username from "OK alice\n"
        sscanf(buf, "OK %31s", username);
        return 1;
    }
    return 0;
}

/* ── main ────────────────────────────────────────────────────────────── */
void error(const char *msg) { perror(msg); exit(1); }

int main(int argc, char *argv[]) {
    if (argc < 3) error("Usage: client <host> <port>");

    /* ── socket + connect ── */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("socket");

    struct hostent *server = gethostbyname(argv[1]);
    if (!server) error("host not found");

    struct sockaddr_in ser_add = {0};
    ser_add.sin_family = AF_INET;
    memcpy(&ser_add.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    ser_add.sin_port = htons(atoi(argv[2]));

    if (connect(sockfd, (struct sockaddr *)&ser_add, sizeof(ser_add)) < 0)
        error("connect");

    /* ── TLS ── */
    SSL_CTX *ctx = create_client_ctx();
    SSL *ssl     = SSL_new(ctx);
    SSL_set_fd(ssl, sockfd);

    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        return 1;
    }
    printf("[TLS] Connected with %s\n", SSL_get_cipher(ssl));

    /* ── auth before ncurses ── */
    if (!do_auth_client(ssl)) {
        printf("Auth failed. Exiting.\n");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sockfd);
        return 1;
    }

    /* ── init ncurses ── */
    initscr();
    cbreak();
    noecho();
    start_color();
    use_default_colors();

    init_pair(COL_BORDER,   COLOR_CYAN,    COLOR_BLACK);
    init_pair(COL_CHAT,     COLOR_GREEN,   COLOR_BLACK);
    init_pair(COL_SYSTEM,   COLOR_YELLOW,  COLOR_BLACK);
    init_pair(COL_PM,       COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COL_ERROR,    COLOR_RED,     COLOR_BLACK);
    init_pair(COL_INACTIVE, COLOR_WHITE,   COLOR_BLACK);
    init_pair(COL_HEADER,   COLOR_CYAN,    COLOR_BLACK);

    init_windows();
    redraw_sidebar();

    char buffer[MAX_BUFFER];

    /* ── main loop ── */
    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd,        &readfds);
        FD_SET(STDIN_FILENO,  &readfds);

        struct timeval tv = {0, 50000};  // 50ms timeout for responsive input
        select(sockfd + 1, &readfds, NULL, NULL, &tv);

        /* ── keyboard input ── */
        wtimeout(input_win, 0);
        int ch = wgetch(input_win);
        if (ch != ERR) {
            if (ch == '\n' || ch == KEY_ENTER) {
                if (input_len > 0) {
                    input_buf[input_len] = '\0';

                    // track rooms locally for sidebar
                    if (strncmp(input_buf, "/join ", 6) == 0) {
                        char room[MAX_ROOM_NAME];
                        sscanf(input_buf + 6, "%31s", room);
                        strncpy(active_room, room, MAX_ROOM_NAME - 1);
                        // add to local list if not present
                        int found = 0;
                        for (int i = 0; i < room_cnt; i++)
                            if (strcmp(rooms_joined[i], room) == 0) { found = 1; break; }
                        if (!found && room_cnt < MAX_ROOMS)
                            strncpy(rooms_joined[room_cnt++], room, MAX_ROOM_NAME - 1);
                        redraw_sidebar();
                    } else if (strncmp(input_buf, "/leave ", 7) == 0) {
                        char room[MAX_ROOM_NAME];
                        sscanf(input_buf + 7, "%31s", room);
                        // remove from local list
                        for (int i = 0; i < room_cnt; i++) {
                            if (strcmp(rooms_joined[i], room) == 0) {
                                strncpy(rooms_joined[i], rooms_joined[--room_cnt], MAX_ROOM_NAME - 1);
                                break;
                            }
                        }
                        if (strcmp(active_room, room) == 0)
                            active_room[0] = '\0';
                        redraw_sidebar();
                    }

                    // echo own message in chat
                    wattron(chat_win, COLOR_PAIR(COL_SYSTEM) | A_BOLD);
                    wprintw(chat_win, "You: %s\n", input_buf);
                    wattroff(chat_win, COLOR_PAIR(COL_SYSTEM) | A_BOLD);
                    wrefresh(chat_win);

                    SSL_write(ssl, input_buf, input_len);

                    if (strncmp(input_buf, "Bye", 3) == 0) break;

                    input_len = 0;
                    memset(input_buf, 0, MAX_BUFFER);
                    redraw_input();
                }
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
                if (input_len > 0) {
                    input_buf[--input_len] = '\0';
                    redraw_input();
                }
            } else if (ch >= 32 && ch < 127 && input_len < MAX_BUFFER - 1) {
                input_buf[input_len++] = (char)ch;
                redraw_input();
            }
        }

        /* ── incoming message from server ── */
        if (FD_ISSET(sockfd, &readfds)) {
            memset(buffer, 0, MAX_BUFFER);
            int n = SSL_read(ssl, buffer, MAX_BUFFER - 1);
            if (n <= 0) {
                chat_print("[Error]: Server disconnected");
                break;
            }
            // strip trailing newline for clean display
            buffer[strcspn(buffer, "\n")] = 0;
            chat_print(buffer);
        }
    }

    /* ── cleanup ── */
    endwin();
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(sockfd);
    return 0;
}