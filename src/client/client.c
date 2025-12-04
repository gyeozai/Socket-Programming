#include "client.h"

client cli;
tpoll poll;
volatile sig_atomic_t conn = 0;
chatbox cbox;
struct termios oldt, newt;
VideoPlayer player;
int argc_p;
char *argv_p[3];

static void *polling_thread() {
    if (pthread_detach(pthread_self()) != 0) {
        ERR_EXIT("[ERROR] pthread_detach error.\n");
    }
    int check = 0; // first time to wake up
    
    pthread_mutex_lock(&poll.slock);
    while (conn) {
        if (check) {
            pthread_mutex_lock(&poll.slock);
        } else {
            pthread_cond_signal(&poll.mcond);
            check++;
        }
        // STEP1: wait for mission here
        // printf("%s[LOG] Polling thread is waiting for mission.%s\n", GREEN, RESET);
        pthread_cond_wait(&poll.pcond, &poll.slock);
        // printf("%s[LOG] Polling thread is handling mission.%s\n", GREEN, RESET);
        if (!conn) {
            break;
        }
        pthread_mutex_unlock(&poll.slock);

        // STEP2: handle mission here
        while (conn) {
            // printf("%s[LOG] Polling thread is polling.%s\n", GREEN, RESET);
            pthread_mutex_lock(&poll.slock);
            if (strlen(cli.username) == 0 && cli.stage > 1) {
                pthread_mutex_unlock(&poll.slock);
                break;
            }
            if (cli.stage == 0) {
                // STEP2A: get chat invitation
                memset(cli.buf_pipe, 0, PIPE_SIZE);
                sprintf(cli.buf_pipe,
                    "GET /check_mail HTTP/1.1\r\n"
                    "X-mode: console-mode\r\n"
                    "\r\n"
                );
                if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                    ERR_EXIT("[ERROR] SSL_write failed.\n");
                }
                if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                    ERR_EXIT("[ERROR] SSL_read failed.\n");
                }
                http_response res;
                parse_http_response(&res, cli.buf_pipe);
                if (res.status_code == 200) {
                    cli.invite = 1;
                    sscanf(cli.buf_pipe, "src_username=%s", cli.pair_username);
                    printf("%s%s, you have a chat invitation from %s. (accept/reject): ", BOTTOM, cli.username, cli.pair_username);
                    fflush(stdout);
                    pthread_mutex_unlock(&poll.slock);
                    break;
                }
            } else {
                // STEP2B: get chat message
                memset(cli.buf_pipe, 0, PIPE_SIZE);
                sprintf(cli.buf_pipe,
                    "GET /chat/get HTTP/1.1\r\n"
                    "X-mode: console-mode\r\n"
                    "\r\n"
                );
                if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                    ERR_EXIT("[ERROR] SSL_write failed.\n");
                }
                if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                    ERR_EXIT("[ERROR] SSL_read failed.\n");
                }
                http_response res;
                parse_http_response(&res, cli.buf_pipe);
                if (res.status_code == 200) {
                    // debug
                    pthread_mutex_lock(&cbox.clock);
                    sprintf(cbox.messages[(cbox.start_index + cbox.message_count) % cbox.max_messages], "%s%s: %s%s", YELLOW, cli.pair_username, cli.buf_pipe, RESET);
                    if (cbox.message_count < cbox.max_messages) {
                        cbox.message_count++;
                    } else {
                        cbox.start_index = (cbox.start_index + 1) % cbox.max_messages;
                    }
                    pthread_mutex_unlock(&cbox.clock);
                    refresh_terminal();
                } else {
                    if (!strcmp(res.status_text, "leave")) {
                        stop_chatbox();
                        printf("\n%s[LOG] %s has left the chatroom.%s\n", GREEN, cli.pair_username, RESET);
                        printf("%s, please enter your command (chat/file/logout): ", cli.username);
                        fflush(stdout);
                        cli.stage = 0;
                        pthread_mutex_unlock(&poll.slock);
                        // break;
                    }
                }
            }
            pthread_mutex_unlock(&poll.slock);
            sleep(1);
        }
    }

    printf("%s[LOG] Polling thread left.%s\n", GREEN, RESET);
    pthread_cond_signal(&poll.mcond);
    pthread_mutex_unlock(&poll.slock);
    return NULL;
}

static void *streaming_thread () {
    if (pthread_detach(pthread_self()) != 0) {
        ERR_EXIT("[ERROR] pthread_detach error.\n");
    }
    int check = 0; // first time to wake up
    
    pthread_mutex_lock(&player.slock);
    while (conn) {
        if (check) {
            pthread_mutex_lock(&player.slock);
        } else {
            pthread_cond_signal(&player.mcond);
            check++;
        }
        // STEP1: wait for mission here
        // printf("%s[LOG] Streaming thread is waiting for mission.%s\n", GREEN, RESET);
        pthread_cond_wait(&player.pcond, &player.slock);
        // printf("%s[LOG] Streaming thread is handling mission.%s\n", GREEN, RESET);
        if (!conn) {
            break;
        }
        pthread_mutex_unlock(&player.slock);

        // STEP2: handle mission here
        apply_player(argc_p, argv_p, player.total_frames);
    }

    printf("%s[LOG] Streaming thread left.%s\n", GREEN, RESET);
    pthread_cond_signal(&player.mcond);
    pthread_mutex_unlock(&player.slock);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        ERR_EXIT("[ERROR] Usage: <ip> <port>.\n");
    }
    argc_p = argc;
    argv_p[0] = argv[0];
    argv_p[1] = argv[1];
    argv_p[2] = argv[2];
    fprintf(stderr, "%s[LOG] Connecting to %s %s.%s\n", GREEN, argv[1], argv[2], RESET);
    init_client(argv);
    init_chatbox();
    fprintf(stderr, "%s[LOG] Connected to the server.%s\n", GREEN, RESET);

    cli.buf_pipe = (char *)malloc(PIPE_SIZE);
    if (cli.buf_pipe == NULL) {
        ERR_EXIT("[ERROR] Unable to allocate memory for buf_pipe.\n");
    }
    // Creating polling thread
    poll.polling_thread = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_mutex_lock(&poll.slock);
    if (pthread_create(poll.polling_thread, NULL, polling_thread, NULL) != 0) {
        ERR_EXIT("[ERROR] Unable to create polling thread.\n");
    }
    pthread_cond_wait(&poll.mcond, &poll.slock);
    pthread_mutex_unlock(&poll.slock);

    // Creating streaming thread
    player.stream_thread = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_mutex_lock(&player.slock);
    if (pthread_create(player.stream_thread, NULL, streaming_thread, NULL) != 0) {
        ERR_EXIT("[ERROR] Unable to create streaming thread.\n");
    }
    pthread_cond_wait(&player.mcond, &player.slock);
    pthread_mutex_unlock(&player.slock);

    // TODO: handle user's input
    board_request(0);
    while (conn) {
        pthread_mutex_lock(&poll.slock);
        if (cli.stage == 0) {
            if (!cli.invite) {
                if (strlen(cli.username) == 0) {
                    printf("%sPlease enter your command (register/login/exit): ", BOTTOM);
                } else {
                    printf("%s%s, please enter your command (chat/file/logout): ", BOTTOM, cli.username);
                }
            } else {
                printf("%s%s, you have a chat invitation from %s. (accept/reject): ", BOTTOM, cli.username, cli.pair_username);
            }
        }
        fflush(stdout);
        pthread_mutex_unlock(&poll.slock);

        if (cli.stage == 1) {
            // getchar();
            refresh_terminal();
            while (1) {
                cbox.ch = getchar();
                pthread_mutex_lock(&cbox.clock);
                if (cbox.ch == '\n') {
                    if (cbox.input_length > 0) {
                        sprintf(cbox.messages[(cbox.start_index + cbox.message_count) % cbox.max_messages], "%s: %s", cli.username, cbox.current_input);
                        strcpy(cli.buf, cbox.current_input);
                        if (cbox.message_count < cbox.max_messages) {
                            cbox.message_count++;
                        } else {
                            cbox.start_index = (cbox.start_index + 1) % cbox.max_messages;
                        }
                        cbox.input_length = 0;
                        cbox.current_input[0] = '\0';
                    }
                    pthread_mutex_unlock(&cbox.clock);
                    refresh_terminal();
                    break;
                } else if (cbox.ch == 127) {
                    if (cbox.input_length > 0) {
                        cbox.input_length--;
                        cbox.current_input[cbox.input_length] = '\0';
                    }
                } else if (cbox.input_length < BUFFER_SIZE - 1) {
                    cbox.current_input[cbox.input_length++] = cbox.ch;
                    cbox.current_input[cbox.input_length] = '\0';
                }
                pthread_mutex_unlock(&cbox.clock);
                refresh_terminal();
            }
        } else {
            scanf("%s", cli.buf);
            // fgets(cli.buf, BUFFER_SIZE, stdin);
            // cli.buf[strlen(cli.buf) - 1] = '\0';
        }

        pthread_mutex_lock(&poll.slock);
        if (cli.stage == 0) {
            pthread_cond_signal(&poll.pcond);
            if (!cli.invite) {
                if (!strncmp(cli.buf, "regi", COMMAND_SIZE)) {
                    register_request();
                    pthread_mutex_unlock(&poll.slock);
                } else if (!strncmp(cli.buf, "logi", COMMAND_SIZE)) {
                    if (login_request()) {
                        board_request(0);
                    }
                    pthread_mutex_unlock(&poll.slock);
                    pthread_cond_signal(&poll.pcond);
                } else if (!strncmp(cli.buf, "exit", COMMAND_SIZE)) {
                    conn = 0;
                    pthread_mutex_unlock(&poll.slock);
                    break;
                } else if (!strncmp(cli.buf, "chat", COMMAND_SIZE)) {
                    chat_request();
                    pthread_mutex_unlock(&poll.slock);
                    pthread_cond_signal(&poll.pcond);
                } else if (!strncmp(cli.buf, "logo", COMMAND_SIZE)) {
                    logout_request();
                    pthread_mutex_unlock(&poll.slock);
                } else if (!strncmp(cli.buf, "file", COMMAND_SIZE)) {
                    printf("%sPlease enter your command (upload/download): ", BOTTOM);
                    scanf("%s", cli.buf);
                    if (!strncmp(cli.buf, "uplo", COMMAND_SIZE)) {
                        upload_request(argc, argv);
                    } else if (!strncmp(cli.buf, "down", COMMAND_SIZE)) {
                        download_request(argc, argv);
                    } else {
                        printf("%s[ERROR] Invalid command.%s\n", RED, RESET);
                    }
                    pthread_mutex_unlock(&poll.slock);
                } else if (!strncmp(cli.buf, "stre", COMMAND_SIZE)) {
                    stream_request();
                    pthread_mutex_unlock(&poll.slock);
                    pthread_cond_signal(&poll.pcond);
                } else {
                    printf("%s[ERROR] Invalid command.%s\n", RED, RESET);
                    pthread_mutex_unlock(&poll.slock);
                }
            } else {
                if (!strncmp(cli.buf, "acce", COMMAND_SIZE)) {
                    cli.invite = 0;
                    memset(cli.buf_pipe, 0, PIPE_SIZE);
                    sprintf(cli.buf_pipe,
                        "POST /chat/accept HTTP/1.1\r\n"
                        "X-mode: console-mode\r\n"
                        "\r\n"
                    );
                    if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                        ERR_EXIT("[ERROR] SSL_write failed.\n");
                    }
                    if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                        ERR_EXIT("[ERROR] SSL_read failed.\n");
                    }
                    http_response res;
                    parse_http_response(&res, cli.buf_pipe);
                    if (res.status_code == 200) {
                        cli.stage = 1;
                        reset_chatbox();
                        printf("%s[LOG] You have accepted the chat invitation.%s\n", GREEN, RESET);
                        fflush(stdout);
                    } else {
                        printf("%s[ERROR] Accept failed.%s\n", RED, RESET);
                        fflush(stdout);
                    }
                } else if (!strncmp(cli.buf, "reje", COMMAND_SIZE)) {
                    cli.invite = 0;
                    memset(cli.buf_pipe, 0, PIPE_SIZE);
                    sprintf(cli.buf_pipe,
                        "POST /chat/reject HTTP/1.1\r\n"
                        "X-mode: console-mode\r\n"
                        "\r\n"
                    );
                    if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                        ERR_EXIT("[ERROR] SSL_write failed.\n");
                    }
                    if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                        ERR_EXIT("[ERROR] SSL_read failed.\n");
                    }
                    printf("%s[LOG] You have rejected the chat invitation.%s\n", GREEN, RESET);
                    fflush(stdout);
                } else {
                    printf("%s[ERROR] Invalid command.%s\n", RED, RESET);
                }
                pthread_mutex_unlock(&poll.slock);
                pthread_cond_signal(&poll.pcond);
            }
        } else if (cli.stage == 1) {
            pthread_cond_signal(&poll.pcond); // debug
            if (!strncmp(cli.buf, "leav", COMMAND_SIZE)) {
                memset(cli.buf_pipe, 0, PIPE_SIZE);
                sprintf(cli.buf_pipe,
                    "POST /chat/leave HTTP/1.1\r\n"
                    "X-mode: console-mode\r\n"
                    "\r\n"
                );
                if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                    ERR_EXIT("[ERROR] SSL_write failed.\n");
                }
                if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                    ERR_EXIT("[ERROR] SSL_read failed.\n");
                }
                stop_chatbox();
                printf("\n%s[LOG] You have left the chatroom.%s\n", GREEN, RESET);
                cli.stage = 0;
                pthread_mutex_unlock(&poll.slock);
            } else {
                if (cbox.first) {
                    cbox.first = 0;
                    pthread_mutex_unlock(&poll.slock);
                    continue;
                }
                memset(cli.buf_pipe, 0, PIPE_SIZE);
                sprintf(cli.buf_pipe,
                    "POST /chat/send HTTP/1.1\r\n"
                    "X-mode: console-mode\r\n"
                    "\r\n"
                    "%s",
                    cli.buf
                );
                if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                    ERR_EXIT("[ERROR] SSL_write failed.\n");
                }
                if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                    ERR_EXIT("[ERROR] SSL_read failed.\n");
                }
                http_response res;
                parse_http_response(&res, cli.buf_pipe);
                if (res.status_code == 401) {
                    stop_chatbox();
                    printf("\n%s[LOG] %s has left the chatroom.%s\n", GREEN, cli.pair_username, RESET);
                    fflush(stdout);
                    cli.stage = 0;
                }
                pthread_mutex_unlock(&poll.slock);
            }
        }
    }
    shutdown_client();
    fprintf(stderr, "%s[LOG] Client has been shut down.%s\n", GREEN, RESET);
    return 0;
}

// ======================================================================================================

void board_request(int mode) {
    if (strlen(cli.username) == 0) {
        fputs(board_text[0], stdout);
        fflush(stdout);
        fputs(board_text[3], stdout);
        fflush(stdout);
    } else {
        fputs(board_text[0], stdout);
        fflush(stdout);
        fputs(board_text[4], stdout);
        fflush(stdout);
    }
    fputs(board_text[1], stdout);
    fflush(stdout);

    switch (mode) {
        case 1:
            printf("Online user(s):\n%s\n", cli.buf_pipe);
            fflush(stdout);
            break;
        case 2:
            printf("File(s) available for download:\n%s\n", cli.buf_pipe);
            fflush(stdout);
            break;
        default:
            break;
    }
    
    fputs(board_text[2], stdout);
    fflush(stdout);
}

void register_request() {
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
    printf("%sPlease enter your username: ", BOTTOM);
    fflush(stdout);
    scanf("%s", username);
    printf("%sPlease enter your password: ", BOTTOM);
    fflush(stdout);
    scanf("%s", password);

    memset(cli.buf_pipe, 0, PIPE_SIZE);
    sprintf(cli.buf_pipe, 
        "POST /register HTTP/1.1\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
        "username=%s&password=%s",

        username, password
    );

    if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed\n");
    }

    if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_read failed\n");
    }

    http_response res;
    parse_http_response(&res, cli.buf_pipe);
    if (res.status_code == 200) {
        printf("%s[LOG] Register success.%s\n", GREEN, RESET);
        fflush(stdout);
    } else {
        printf("%s[ERROR] Register failed.%s\n", RED, RESET);
        fflush(stdout);
    }
    return;
}

int login_request () {
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
    printf("%sPlease enter your username: ", BOTTOM);
    fflush(stdout);
    scanf("%s", username);
    printf("%sPlease enter your password: ", BOTTOM);
    fflush(stdout);
    scanf("%s", password);

    memset(cli.buf_pipe, 0, PIPE_SIZE);
    sprintf(cli.buf_pipe, 
        "POST /login HTTP/1.1\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
        "username=%s&password=%s",

        username, password
    );
    if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }

    if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_read failed.\n");
    }
    http_response res;
    parse_http_response(&res, cli.buf_pipe);
    if (res.status_code == 200) {
        strcpy(cli.username, username);
        printf("%s[LOG] Login success.%s\n", GREEN, RESET);
        fflush(stdout);
        return 1;
    } else {
        if (!strcmp(res.status_text, "fail")) {
            printf("%s[ERROR] Login failed.%s\n", RED, RESET);
        } else if (!strcmp(res.status_text, "usrf")) {
            printf("%s[ERROR] User not found.%s\n", RED, RESET);
        } else if (!strcmp(res.status_text, "pwdf")) {
            printf("%s[ERROR] Wrong password.%s\n", RED, RESET);
        }
        fflush(stdout);
    }
    return 0; 
}

void logout_request () {
    memset(cli.buf_pipe, 0, PIPE_SIZE);
    sprintf(cli.buf_pipe, 
        "POST /logout HTTP/1.1\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
    );
    if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }

    if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_read failed.\n");
    }
    http_response res;
    parse_http_response(&res, cli.buf_pipe);
    if (res.status_code == 200) {
        memset(cli.username, 0, USERNAME_LEN);
        printf("%s[LOG] Logout success.%s\n", GREEN, RESET);
        fflush(stdout);
    } else {
        printf("%s[ERROR] Logout failed.%s\n", RED, RESET);
        fflush(stdout);
    }
    return; 
}

// ======================================================================================================

void chat_request () {
    memset(cli.buf_pipe, 0, PIPE_SIZE);
    sprintf(cli.buf_pipe, 
        "GET /list_online HTTP/1.1\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
    );
    if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }

    if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_read failed.\n");
    }
    http_response res;
    parse_http_response(&res, cli.buf_pipe);
    if (res.status_code == 200) {
        board_request(1);
        printf("%sPlease enter the username you want to chat with: ", BOTTOM);
        fflush(stdout);
        scanf("%s", cli.buf);
        printf("%s[LOG] Sending chat invitation, please wait.%s\n", GREEN, RESET);

        int found = 0;
        char num[5], user[USERNAME_LEN], status[10];
        char *token = strtok(cli.buf_pipe, "\n");
        while (token != NULL) {
            memset(status, 0, 10);
            sscanf(token, "%s %s %s", num, user, status);
            if (!strcmp(user, cli.buf) && strlen(status) == 0) {
                found = 1;
                break;
            }
            token = strtok(NULL, "\n");
        }
        // free(token);
        if (found) {
            num[strlen(num) - 1] = '\0';
            memset(cli.buf_pipe, 0, PIPE_SIZE);
            sprintf(cli.buf_pipe, 
                "POST /chat/invite HTTP/1.1\r\n"
                "X-mode: console-mode\r\n"
                "\r\n"
                "target_tinfo_idx=%s",

                num
            );
            if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                ERR_EXIT("[ERROR] SSL_write failed.\n");
            }
            
            if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                ERR_EXIT("[ERROR] SSL_read failed.\n");
            }
            parse_http_response(&res, cli.buf_pipe);
            if (res.status_code == 200) {
                cli.stage = 1;
                reset_chatbox();
                strcpy(cli.pair_username, user);
                printf("%s[LOG] Chat invitation is accepted, you can start chatting.%s\n", GREEN, RESET);
                fflush(stdout);
            } else {
                printf("%s[ERROR] Chat invitation is rejected.%s\n", RED, RESET);
                fflush(stdout);
            }
        } else {
            printf("%s[ERROR] User not valid or busy.%s\n", RED, RESET);
            fflush(stdout);
        }
    } else {
        printf("%s[ERROR] Invitation failed.%s\n", RED, RESET);
        fflush(stdout);
    }
    return; 
}

void enable_non_blocking_input() {
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

void disable_non_blocking_input() {
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

int get_terminal_height() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        perror("ioctl");
        return -1;
    }
    return ws.ws_row;
}

void refresh_terminal() {
    printf("\033[2J");

    pthread_mutex_lock(&cbox.clock);

    int display_count = cbox.message_count > cbox.max_messages ? cbox.max_messages : cbox.message_count;
    int offset = cbox.message_count > (cbox.terminal_h - 1) ? cbox.message_count - (cbox.terminal_h - 1) : 0;

    printf("\033[%d;0H%s\n", 1, board_text[0]);
    printf("\033[%d;0H%s\n", 2, board_text[4]);
    printf("\033[%d;0H%s\n", 3, board_text[1]);
    printf("\033[%d;0H%s%s%s\n", 4, YELLOW, "(Enter \"leave\" to leave the chatroom.)", RESET); 

    for (int i = offset; i < offset + display_count; i++) {
        int index = (cbox.start_index + i) % cbox.max_messages;
        printf("\033[%d;0H%s\n", i - offset + 6, cbox.messages[index]);
    }

    printf("\033[%d;0H%s\n", cbox.terminal_h - 1, board_text[1]);
    printf("\033[%d;0H%s, please enter a message: %s", cbox.terminal_h, cli.username, cbox.current_input);
    fflush(stdout);

    pthread_mutex_unlock(&cbox.clock);
}

void init_chatbox() {
    cbox.terminal_h = get_terminal_height();
    if (cbox.terminal_h == -1) {
        ERR_EXIT("[ERROR] Unable to get terminal height. Exiting.\n");
    }
    cbox.max_messages = cbox.terminal_h - 8;
    cbox.messages = malloc(cbox.max_messages * sizeof(char *));
    for (int i = 0; i < cbox.max_messages; i++) {
        cbox.messages[i] = malloc(BUFFER_SIZE);
    }
    pthread_mutex_init(&cbox.clock, NULL);
}

void reset_chatbox () {
    cbox.first = 1;
    cbox.message_count = 0;
    cbox.start_index = 0;
    cbox.input_length = 0;
    memset(cbox.current_input, 0, BUFFER_SIZE);

    enable_non_blocking_input();
}

void stop_chatbox() {
    cbox.first = 1;
    printf("\033[2J");
    disable_non_blocking_input();
}

void destroy_chatbox() {
    pthread_mutex_destroy(&cbox.clock);
    for (int i = 0; i < cbox.max_messages; i++) {
        free(cbox.messages[i]);
    }
    free(cbox.messages);
}

// ======================================================================================================

void parse_http_response(http_response *res, char *response) {
    // Parse the status line
    sscanf(response, "%s %d %s", res->http_version, &res->status_code, res->status_text);

    // Parse the headers
    char *headers_start = strstr(response, "\r\n") + 2; // Skip past the first "\r\n"
    char *header_end = strstr(headers_start, "\r\n\r\n"); // Find the end of the headers
    // if (header_end) {
    //     printf("\nHeaders:\n");
    //     while (headers_start < header_end) {
    //         char line[256];
    //         int len = 0;
    //         while (*headers_start != '\r' && *headers_start != '\n' && headers_start < header_end) {
    //             line[len++] = *headers_start++;
    //         }
    //         line[len] = '\0';
    //         printf("%s\n", line);
    //         if (*headers_start == '\r') {
    //             headers_start += 2; // Skip "\r\n"
    //         } else {
    //             headers_start++; // Skip "\n"
    //         }
    //     }
    // }

    // Parse the body
    char *body = header_end + 4; // Skip past the "\r\n\r\n"
    strcpy(response, body);
    return;
}

int parse_http_response_v1(http_response *res, char *response) {
    // Parse the status line (for response)
    sscanf(response, "%s %d %s", res->http_version, &res->status_code, res->status_text);

    // Parse the headers
    int content_length;
    char *headers_start = strstr(response, "\r\n") + 2; // Skip past the first "\r\n"
    char *header_end = strstr(headers_start, "\r\n\r\n"); // Find the end of the headers

    // Check the content-length
    char *content_length_start = strstr(headers_start, "Content-length: ");
    if (content_length_start) {
        sscanf(content_length_start, "Content-length: %d", &content_length);
    }

    // Parse the body
    char *body = header_end + 4; // Skip past the "\r\n\r\n"
    memcpy(response, body, content_length);
    return content_length;
}

static void gtk_file_selection(GtkWidget *widget, gint response_id, gpointer data) {
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(widget);
        char *filename = gtk_file_chooser_get_filename(chooser);
        strcpy(cli.filename, filename);
        g_free(filename);
    } else {
        printf("No file selected or action cancelled.\n");
    }
    gtk_widget_destroy(widget);
    gtk_main_quit();
}

static void gtk_folder_selection(GtkWidget *widget, gint response_id, gpointer data) {
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(widget);
        char *folder = gtk_file_chooser_get_filename(chooser);
        strcpy(cli.filename, folder);
        g_free(folder);
    } else {
        printf("No folder selected or action cancelled.\n");
    }
    gtk_widget_destroy(widget);
    gtk_main_quit();
}

void upload_request(int argc, char **argv) {
    gtk_init(&argc, &argv);
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select a File", NULL,
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Open", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    g_signal_connect(dialog, "response", G_CALLBACK(gtk_file_selection), NULL);
    gtk_widget_show(dialog);
    gtk_main();

    // STEP1: prepare for upload
    memset(cli.buf_pipe, 0, PIPE_SIZE);
    sprintf(cli.buf_pipe,
        "POST /file/upload HTTP/1.1\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
        "%s",
        cli.filename
    );
    if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }
    if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_read failed.\n");
    }
    http_response res;
    parse_http_response(&res, cli.buf_pipe);
    if (res.status_code == 200) {
        printf("%s[LOG] File upload is ready.%s\n", GREEN, RESET);
    } else {
        printf("%s[ERROR] File upload failed.%s\n", RED, RESET);
        return;
    }
    
    // free(cli.buf_pipe);
    // cli.buf_pipe = (char *)malloc(CHUNK_SIZE);
    int upload_fd = open(cli.filename, O_RDONLY);
    if (upload_fd < 0) {
        ERR_EXIT("[ERROR] Failed to open source file.\n");
    }
    char *tmp_buf = (char *)malloc(PIPE_SIZE - 128);
    memset(tmp_buf, 0, PIPE_SIZE - 128);
    
    // STEP2: upload file
    size_t bytes_read;
    while ((bytes_read = read(upload_fd, tmp_buf, PIPE_SIZE - 128)) > 0) {
        memset(cli.buf_pipe, 0, PIPE_SIZE);
        sprintf(cli.buf_pipe,
            "POST /file/upload HTTP/1.1\r\n"
            "X-mode: console-mode\r\n"
            "Content-length: %zd\r\n"
            "\r\n",
            bytes_read
        );
        int request_len = strlen(cli.buf_pipe);
        memcpy(cli.buf_pipe + request_len, tmp_buf, bytes_read);

        size_t total_len = request_len + bytes_read;
        if (SSL_write(cli.ssl, cli.buf_pipe, total_len) <= 0) {
            ERR_EXIT("[ERROR] SSL_write failed.\n");
        }

        if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
            ERR_EXIT("[ERROR] SSL_read failed.\n");
        }
        memset(tmp_buf, 0, PIPE_SIZE - 128);
    }
    memset(cli.buf_pipe, 0, PIPE_SIZE);
    sprintf(cli.buf_pipe,
        "POST /file/upload HTTP/1.1\r\n"
        "X-mode: console-mode\r\n"
        "Content-length: 0\r\n"
        "\r\n"
    );
    if (SSL_write(cli.ssl, cli.buf_pipe, strlen(cli.buf_pipe)) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }
    if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_read failed.\n");
    }
    close(upload_fd);
    free(tmp_buf);
    printf("%s[LOG] File upload success.%s\n", GREEN, RESET);
    // free(cli.buf_pipe);
    // cli.buf_pipe = (char *)malloc(PIPE_SIZE);
    return;
}

void download_request(int argc, char **argv) {
    memset(cli.buf_pipe, 0, PIPE_SIZE);
    sprintf(cli.buf_pipe,
        "GET /list_file HTTP/1.1\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
    );
    if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }
    if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_read failed.\n");
    }
    http_response res;
    parse_http_response(&res, cli.buf_pipe);
    if (res.status_code == 200) {
        board_request(2);
        printf("%sPlease enter the filename you want to download: ", BOTTOM);
        fflush(stdout);
        scanf("%s", cli.buf);
        printf("%s[LOG] Downloading file, please wait.%s\n", GREEN, RESET);

        int found = 0;
        char num[5], file[FILENAME_LEN];
        char *token = strtok(cli.buf_pipe, "\n");
        while (token != NULL) {
            sscanf(token, "%s %s", num, file);
            if (!strcmp(file, cli.buf)) {
                found = 1;
                break;
            }
            token = strtok(NULL, "\n");
        }
        // free(token);
        if (found) {
            memset(cli.buf_pipe, 0, PIPE_SIZE);
            sprintf(cli.buf_pipe, 
                "POST /file/download HTTP/1.1\r\n"
                "X-mode: console-mode\r\n"
                "\r\n"
                "%s",

                file
            );
            if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                ERR_EXIT("[ERROR] SSL_write failed.\n");
            }
            
            if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                ERR_EXIT("[ERROR] SSL_read failed.\n");
            }
            parse_http_response(&res, cli.buf_pipe);
            if (res.status_code == 200) {
                sleep(1);
                gtk_init(&argc, &argv);
                GtkWidget *dialog = gtk_file_chooser_dialog_new("Select a Folder", NULL,
                                                                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,  // 改為選擇目錄
                                                                "_Cancel", GTK_RESPONSE_CANCEL,
                                                                "_Open", GTK_RESPONSE_ACCEPT,
                                                                NULL);
                g_signal_connect(dialog, "response", G_CALLBACK(gtk_folder_selection), NULL);
                gtk_widget_show(dialog);
                gtk_main();
                
                strcat(cli.filename, "/");
                strcat(cli.filename, file);
                
                int download_fd = open(cli.filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (download_fd < 0) {
                    ERR_EXIT("[ERROR] Failed to open destination file.\n");
                }
                
                memset(cli.buf_pipe, 0, PIPE_SIZE);
                sprintf(cli.buf_pipe,
                    "GET /file/downling HTTP/1.1\r\n"
                    "X-mode: console-mode\r\n"
                    "\r\n"
                );
                if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                    ERR_EXIT("[ERROR] SSL_write failed.\n");
                }
                http_response res;
                size_t bytes_read;
                while ((bytes_read = SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE)) > 0) {
                    int content_length = parse_http_response_v1(&res, cli.buf_pipe);
                    if (content_length == 0) {
                        break;
                    }
                    if (write(download_fd, cli.buf_pipe, content_length) < 0) {
                        ERR_EXIT("[ERROR] Failed to write to destination file.\n");
                    }

                    memset(cli.buf_pipe, 0, PIPE_SIZE);
                    sprintf(cli.buf_pipe,
                        "GET /file/downling HTTP/1.1\r\n"
                        "X-mode: console-mode\r\n"
                        "\r\n"
                    );
                    if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                        ERR_EXIT("[ERROR] SSL_write failed.\n");
                    }
                }
                close(download_fd);
                printf("%s[LOG] File download success.%s\n", GREEN, RESET);
                fflush(stdout);
            } else {
                printf("%s[ERROR] File download failed.%s\n", RED, RESET);
                fflush(stdout);
            }
        } else {
            printf("%s[ERROR] Filename not valid.%s\n", RED, RESET);
            fflush(stdout);
        }
    } else {
        printf("%s[ERROR] Download section failed.%s\n", RED, RESET);
        fflush(stdout);
        return;
    }
    return;
}

// ======================================================================================================

void stream_request () {
    memset(cli.buf_pipe, 0, PIPE_SIZE);
    sprintf(cli.buf_pipe,
        "GET /list_video HTTP/1.1\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
    );
    if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }
    if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_read failed.\n");
    }
    http_response res;
    parse_http_response(&res, cli.buf_pipe);
    if (res.status_code == 200) {
        board_request(3);
        printf("%sPlease enter the video name you want to stream: ", BOTTOM);
        fflush(stdout);
        scanf("%s", cli.buf);
        printf("%s[LOG] Streaming video, please wait.%s\n", GREEN, RESET);

        int found = 0;
        char num[5], video[FILENAME_LEN], frame[10];
        char *token = strtok(cli.buf_pipe, "\n");
        while (token != NULL) {
            sscanf(token, "%s %s %s", num, video, frame);
            if (!strcmp(video, cli.buf)) {
                found = 1;
                break;
            }
            token = strtok(NULL, "\n");
        }
        // free(token);
        if (found) {
            memset(cli.buf_pipe, 0, PIPE_SIZE);
            sprintf(cli.buf_pipe, 
                "POST /video/stream HTTP/1.1\r\n"
                "X-mode: console-mode\r\n"
                "\r\n"
                "%s",

                video
            );
            if (SSL_write(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                ERR_EXIT("[ERROR] SSL_write failed.\n");
            }
            
            if (SSL_read(cli.ssl, cli.buf_pipe, PIPE_SIZE) <= 0) {
                ERR_EXIT("[ERROR] SSL_read failed.\n");
            }
            parse_http_response(&res, cli.buf_pipe);
            if (res.status_code == 200) {
                pthread_cond_signal(&player.pcond);
                // debug


                


                printf("%s[LOG] Video streaming finished.%s\n", GREEN, RESET);
                fflush(stdout);
            } else {
                printf("%s[ERROR] Video streaming failed.%s\n", RED, RESET);
                fflush(stdout);
            }
        } else {
            printf("%s[ERROR] Video not valid.%s\n", RED, RESET);
            fflush(stdout);
        }
    } else {
        printf("%s[ERROR] Video streaming section failed.%s\n", RED, RESET);
        fflush(stdout);
        return;
    }
    return;
}

void init_player () {
    if (pthread_mutex_init(&player.slock, NULL) != 0) {
        ERR_EXIT("[ERROR] Failed to initialize player mutex.\n");
    }
    if (pthread_cond_init(&player.pcond, NULL) != 0) {
        ERR_EXIT("[ERROR] Failed to initialize player condition variable.\n");
    }
    if (pthread_cond_init(&player.mcond, NULL) != 0) {
        ERR_EXIT("[ERROR] Failed to initialize player condition variable.\n");
    }
}

void reset_player(VideoPlayer *player, int total_frames) {
    player->total_frames = total_frames;
    player->is_paused = FALSE;
    for (int i = 1; i <= total_frames + 1; i++) {
        player->frame[i] = (char *)malloc(FILENAME_LEN);
    }
    for (int i = 1; i <= total_frames; i++) {
        sprintf(player->frame[i], "frames/frame_%04d.raw", i);
    }
    player->frame[total_frames] = NULL;
    return;
}

void destroy_player(VideoPlayer *player, int total_frames) {
    for (int i = 0; i <= total_frames + 1; i++) {
        free(player->frame[i]);
    }
}

void display_frame(const char *image_path) {
    GdkPixbuf *pixbuf, *scaled_pixbuf;

    pixbuf = gdk_pixbuf_new_from_file(image_path, NULL);
    if (pixbuf == NULL) {
        player.is_paused = !player.is_paused;
        gtk_button_set_label(GTK_BUTTON(player.pause_button), player.is_paused ? "Resume" : "Pause");
        g_print("Failed to load frame: %s\n", image_path);
        return;
    }

    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);

    int target_width = 1280;
    int target_height = 720;

    if (width > target_width || height > target_height) {
        scaled_pixbuf = gdk_pixbuf_scale_simple(pixbuf, target_width, target_height, GDK_INTERP_BILINEAR);
    } else {
        scaled_pixbuf = pixbuf;
    }

    gtk_image_set_from_pixbuf(GTK_IMAGE(player.image), scaled_pixbuf);
    g_object_unref(pixbuf);
    if (scaled_pixbuf != pixbuf) {
        g_object_unref(scaled_pixbuf);
    }
    gtk_widget_show_all(player.image);
}

void update_progress_bar(int current_frame) {
    float progress = (float)current_frame / player.total_frames;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(player.progress_bar), progress);
}

void update_scale_position(int current_frame) {
    gtk_range_set_value(GTK_RANGE(player.scale), current_frame);
}

gboolean play_video(gpointer data) {
    static int frame = 0;
    if (player.is_paused) {
        return TRUE;
    }

    frame = (int)gtk_range_get_value(GTK_RANGE(player.scale));
    if (player.frame[frame] != NULL) {
        display_frame(player.frame[frame]);
        update_progress_bar(frame + 1);
        update_scale_position(frame + 1);
    }
    frame = (frame + 1) % player.total_frames;
    return TRUE;
}

void on_scale_value_changed(GtkRange *range, gpointer data) {
    int current_frame = (int)gtk_range_get_value(range);
    update_progress_bar(current_frame);
    display_frame(player.frame[current_frame]);
}

void on_pause_button_clicked(GtkButton *button, gpointer data) {
    player.is_paused = !player.is_paused;
    gtk_button_set_label(GTK_BUTTON(player.pause_button), player.is_paused ? "Resume" : "Pause");
}

void apply_player(int argc, char **argv, int total_frames) {
    reset_player(&player, 4);
    gtk_init(&argc, &argv);

    GtkWidget *window;
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Frame-based Video");
    gtk_window_set_default_size(GTK_WINDOW(window), 320, 240);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    player.image = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(vbox), player.image, TRUE, TRUE, 0);
    gtk_widget_set_size_request(player.image, 300, 200);

    player.progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(player.progress_bar), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(player.progress_bar), "Loading...");
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(player.progress_bar), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), player.progress_bar, FALSE, FALSE, 0);

    player.scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, player.total_frames - 1, 1);
    gtk_range_set_value(GTK_RANGE(player.scale), 0);
    gtk_box_pack_start(GTK_BOX(vbox), player.scale, FALSE, FALSE, 0);

    player.pause_button = gtk_button_new_with_label("Pause");
    g_signal_connect(player.pause_button, "clicked", G_CALLBACK(on_pause_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), player.pause_button, FALSE, FALSE, 0);

    g_signal_connect(player.scale, "value-changed", G_CALLBACK(on_scale_value_changed), NULL);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    g_timeout_add(200, play_video, NULL);
    gtk_widget_show_all(window);
    gtk_main();
    destroy_player(&player, 4);

    return 0;
}

// ======================================================================================================

void signal_handler(int sig) {
    if (sig == SIGPIPE) {
        fprintf(stderr, "%s[ERROR] Connection broken or terminated unexpectedly.%s\n", RED, RESET);
        if (conn) {
            conn = 0;
        } else {
            exit(1);
        }
    }
    return;
}

void initialize_openssl() {
    SSL_library_init();
    OpenSSL_add_ssl_algorithms();
    SSL_load_error_strings();
}

void cleanup_openssl() {
    EVP_cleanup();
}

SSL_CTX *create_context() {
    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        ERR_EXIT("[ERROR] Unable to create SSL context\n");
    }
    return ctx;
}

static void init_client(char** argv) {
    if (signal(SIGPIPE, signal_handler) == SIG_ERR) {
        ERR_EXIT("[ERROR] Unable to set signal handler\n");
    }
    cli.ip = argv[1];
    cli.port = (unsigned short)atoi(argv[2]);
    if (cli.port == 0 || cli.port > 65536) {
        ERR_EXIT("[ERROR] Invalid port\n");
    }
    if ((cli.conn_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ERR_EXIT("[ERROR] Socket creation failed\n");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(cli.port);
    
    if (inet_pton(AF_INET, cli.ip, &server_addr.sin_addr) <= 0) {
        ERR_EXIT("[ERROR] Invalid IP\n");
    }
    if (connect(cli.conn_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ERR_EXIT("[ERROR] Connection failed\n");
    }

    initialize_openssl();
    cli.ctx = create_context();
    cli.ssl = SSL_new(cli.ctx);
    SSL_set_fd(cli.ssl, cli.conn_fd);

    if (SSL_connect(cli.ssl) <= 0) {
        ERR_EXIT("[ERROR] SSL connection failed\n");
    }
    conn = 1;
    return;
}

void shutdown_client() {
    pthread_mutex_lock(&poll.slock);
    pthread_cond_signal(&poll.pcond);
    pthread_mutex_unlock(&poll.slock);
    pthread_mutex_destroy(&poll.slock);
    pthread_cond_destroy(&poll.pcond);
    pthread_cond_destroy(&poll.mcond);
    free(poll.polling_thread);
    destroy_chatbox();
    free(cli.buf_pipe);
    SSL_shutdown(cli.ssl);
    SSL_free(cli.ssl);
    close(cli.conn_fd);
    SSL_CTX_free(cli.ctx);
    cleanup_openssl();
    return;
}