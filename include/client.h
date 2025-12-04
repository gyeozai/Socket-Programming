#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <termios.h>
#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define ERR_EXIT(a) do { \
    perror(a); \
    if (ERR_peek_error() != 0) { \
        ERR_print_errors_fp(stderr); \
    } \
    exit(1); \
} while(0)

/*
    static variables
*/
const char *board_text[] = {
    "\n==============================\n",
    "==============================\n",
    "==============================\n\n",
    "You're outside from someplace.\n",
    "Welcome to SECRET chatIN PLACE\n"
};

#define RESET   "\033[0m"
#define GRAY    "\033[90m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BOTTOM  "\033[999;0H\033[K"

#define USERNAME_LEN 16
#define PASSWORD_LEN 16
#define FILENAME_LEN 128
#define COMMAND_SIZE 4
#define BUFFER_SIZE 128
#define PIPE_SIZE 4096
#define CHUNK_SIZE 2097152 // 2MB
#define MAX_FRAMES 9999

/*
    struct declarations
*/
typedef struct {
    char* ip;               // server's ip
    unsigned short port;    // server's port
    int conn_fd;            // fd to talk with server
    char username[USERNAME_LEN];        // client's username
    char pair_username[USERNAME_LEN];   // chat pair's username
    char filename[FILENAME_LEN];        // for file transfer
    int stage;
    int invite;
    SSL_CTX *ctx;
    SSL *ssl;
    char buf[BUFFER_SIZE];
    char *buf_pipe;
} client;

typedef struct {
    pthread_t *polling_thread;
    pthread_mutex_t slock;  // lock for socket, stage_val, etc.
    pthread_cond_t pcond;   // wake up polling_thread
    pthread_cond_t mcond;   // wake up main thread
} tpoll;

typedef struct {
    char http_version[16];
    int status_code;
    char status_text[16];
} http_response;

typedef struct {
    int terminal_h;
    pthread_mutex_t clock;
    char **messages;
    int max_messages;
    int message_count;
    int start_index;
    int first;
    char current_input[BUFFER_SIZE];
    char ch;
    int input_length;
} chatbox;

typedef struct {
    pthread_t *stream_thread;
    pthread_mutex_t slock;
    pthread_cond_t pcond;
    pthread_cond_t mcond;
    GtkWidget *image;
    GtkWidget *progress_bar;
    GtkWidget *scale;
    GtkWidget *pause_button;
    int total_frames;
    gboolean is_paused;
    char *frame[MAX_FRAMES];
    int total_frames;
} VideoPlayer;

/*
    function declarations
*/
void board_request(int mode);
void register_request();
int login_request();
void logout_request();

void chat_request();
void enable_non_blocking_input();
void disable_non_blocking_input();
int get_terminal_height();
void refresh_terminal();
void init_chatbox();
void reset_chatbox();
void stop_chatbox();
void destroy_chatbox();

void parse_http_response(http_response *res, char *response);
int parse_http_response_v1(http_response *res, char *response);
static void gtk_file_selection(GtkWidget *widget, gint response_id, gpointer data);
static void gtk_folder_selection(GtkWidget *widget, gint response_id, gpointer data);
void upload_request(int argc, char **argv);
void download_request(int argc, char **argv);

void init_player();
void reset_player(VideoPlayer *player, int total_frames);
void destroy_player(VideoPlayer *player, int total_frames);
void display_frame(const char *image_path);
void update_progress_bar(int current_frame);
void update_scale_position(int current_frame);
gboolean play_video(gpointer data);
void on_scale_value_changed(GtkRange *range, gpointer data);
void on_pause_button_clicked(GtkButton *button, gpointer data);
void apply_player(int argc, char **argv, int total_frames);

void signal_handler(int sig);
void initialize_openssl();
void cleanup_openssl();
SSL_CTX *create_context();
static void init_client(char** argv);
void shutdown_client();

#endif