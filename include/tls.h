# ifndef TLS_H
# define TLS_H

# include <openssl/ssl.h>
# include <openssl/err.h>

#define CERT_FILE "/home/leo-balamuthu/Desktop/Multi-Process Chat server/certs/server.crt"
#define KEY_FILE  "/home/leo-balamuthu/Desktop/Multi-Process Chat server/certs/server.key"

SSL_CTX *create_server_ctx(void);
SSL_CTX *create_client_ctx(void);
void tls_error(const char *msg);

# endif