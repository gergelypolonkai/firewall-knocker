#ifndef _AUTH_SERVER_H
# define _AUTH_SERVER_H

/* Logging levels. These are the possible values for CURRENT_LOG_LEVEL above */
#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_DEBUG 2

/* The client_t struct. With this struct full client data can be
 * stored in a doubly-linked list */
typedef struct _client_t {
    int socket;
    char *ip;
    time_t last_reset;
    struct _client_t *previous;
    struct _client_t *next;
} client_t;

#endif /* _AUTH_SERVER_H */
