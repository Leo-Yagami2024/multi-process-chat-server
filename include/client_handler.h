# ifndef CLIENT_HANDLER
# define CLIENT_HANDLER

# include "common.h"
#include <openssl/ssl.h>

int do_auth(SSL *ssl, char *out_username);

void handle_client(SSL *ssl, int ipc_fd, int client_id);

# endif