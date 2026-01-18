/*
 * TLS Interface Header
 * Wrapper around TLSe for kernel TLS support
 */

#ifndef TLS_H
#define TLS_H

#include "types.h"

/* Forward declaration */
struct tls_context;

/* Initialize TLS library */
int tls_init(void);

/* Create a new TLS context */
struct tls_context* tls_create_context(int is_server);

/* Set socket for TLS context */
void tls_set_socket(struct tls_context *tls, int socket_fd);

/* Perform TLS handshake */
int tls_handshake(struct tls_context *tls);

/* Read data from TLS connection */
int tls_read(struct tls_context *tls, void *buffer, size_t len);

/* Write data to TLS connection */
int tls_write(struct tls_context *tls, const void *buffer, size_t len);

/* Close TLS connection */
void tls_close(struct tls_context *tls);

/* Load certificate (for server) */
int tls_load_certificate(struct tls_context *tls, const char *cert_file, const char *key_file);

/* Set hostname for SNI (for client) */
int tls_set_hostname(struct tls_context *tls, const char *hostname);

#endif /* TLS_H */
