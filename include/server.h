#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <mysql/mysql.h>

#define ERR_EXIT(a) do { \
    perror(a); \
    if (ERR_peek_error() != 0) { \
        ERR_print_errors_fp(stderr); \
    } \
    exit(1); \
} while(0)

/*
    MySQL configurations
*/
#define QUERY_SIZE 128
#define SERVER "localhost"
#define USER "root"
#define PASSWORD "Ygw03022316047" // Set your MySQL password here :)

/*
    static variables
*/
#define RESET   "\033[0m"
#define GRAY    "\033[90m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"

#define USERNAME_LEN 16
#define PASSWORD_LEN 16
#define FILENAME_LEN 128
#define BUFFER_SIZE 128
#define PIPE_SIZE 4096
#define WEB_SIZE 8192
#define CHUNK_SIZE 2097152  // 2MB

#define MAX_CLIENTS 10
#define CERT_FILE "certs/server.crt"
#define KEY_FILE "certs/server.key"

/*
    struct declarations
*/
typedef struct {
    char hostname[512];     // server's hostname
    unsigned short port;    // port to listen
    int listen_fd;          // fd to wait for a new connection
    SSL_CTX *ctx;
    int file_counter;
} server;

typedef struct {
    // char host[512];      // client's host
    int serve;                      // check if idle
    char username[USERNAME_LEN];    // check if login
    int stage;                      // indicate option, 0 for nothing, 1 for chat, 2 for file
    char mail[BUFFER_SIZE];         // indicate invite or mail (for chat feature)
} tinfo;

typedef struct node {
    int conn_fd;
    struct node* next;
} Node;

typedef struct {
    pthread_t *worker_thread;
    int num_threads;
    Node *conn_queue;
    Node *conn_tail;
    int pending;
    int curr_cli;
    pthread_mutex_t plock;
    pthread_cond_t wcond;   // wake up worker_thread
    pthread_cond_t mcond;   // wake up main_thread
} tpool;

typedef struct {
    char method[8];
    char path[16];
    char http_version[16];
} http_request;

/*
    function declarations
*/
void tpool_init(int n_threads);
void reset_info(int i);
void tinfo_init(int n_threads);
int get_tinfo_idx();
Node *genNode(int conn_fd);
void insertNode(int conn_fd);
void deleteNode();

void register_request(char *request);
void login_request(char *request, int tinfo_idx);
static void init_db();
void list_request(char *request, int tinfo_idx);
int invite_request(char *request, int tinfo_idx, int pair_idx);

void parse_http_request(http_request *req, char *request);
int parse_http_request_v1(http_request *req, char *request);
void upload_request(SSL *ssl, char *request);
void list_file_request(char *request);
void download_request(SSL *ssl, char *request);

void signal_handler(int sig);
void initialize_openssl();
void cleanup_openssl();
SSL_CTX *create_context();
void configure_context(SSL_CTX *ctx);
static void init_server(char** argv);
void shutdown_server();

#endif