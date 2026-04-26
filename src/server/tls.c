# include <stdio.h>
# include <stdlib.h>

# include "tls.h"

void tls_error(const char *msg){
    fprintf(stderr, "%s\n", msg);
    ERR_print_errors_fp(stderr);
    exit(1);
}


SSL_CTX *create_server_ctx(void){
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if(!ctx) tls_error("Failed to create server side context!");

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION); 

    if(SSL_CTX_use_certificate_file(ctx, CERT_FILE, SSL_FILETYPE_PEM) <= 0){
        tls_error("Failed to load server certificate..");
    }

    if(SSL_CTX_use_PrivateKey_file(ctx, KEY_FILE, SSL_FILETYPE_PEM) <= 0){
        tls_error("Failed to load server Private Keys..");
    }

    if(!SSL_CTX_check_private_key(ctx)){
        tls_error("Private key does not match certificate...");
    }

    return ctx;
}

SSL_CTX *create_client_ctx(void){
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if(!ctx) tls_error("Failed to create client side context...");

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION); 

    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    return ctx;
}