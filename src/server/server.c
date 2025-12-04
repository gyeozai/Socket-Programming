#include "server.h"

server svr;
volatile sig_atomic_t shut_down = 0;
tpool *pool;
tinfo *info;
pthread_mutex_t ilock[MAX_CLIENTS];

MYSQL *db;
char query[QUERY_SIZE];
pthread_mutex_t dblock;

int thread_count = 0;
static void *worker_thread() {
    if (pthread_detach(pthread_self()) != 0) {
        ERR_EXIT("[ERROR] pthread_detach error.\n");
    }
    int check = 0; // first time to wake up
    int thread_idx = ++thread_count;
    
    pthread_mutex_lock(&pool->plock);
    while (!shut_down) {
        if (check) {
            pthread_mutex_lock(&pool->plock);
        } else {
            pthread_cond_signal(&pool->mcond);
            check++;
        }
        while (pool->pending == 0 && !shut_down) {
            // STEP1: wait for connection here
            printf("[LOG] Worker thread %d is waiting for connection.\n", thread_idx);
            pthread_cond_wait(&pool->wcond, &pool->plock);
        }
        if (shut_down) {
            break;
        }
        
        // STEP2: get connection from queue
        printf("[LOG] Worker thread %d is handling connection.\n", thread_idx);
        int conn_fd = pool->conn_queue->next->conn_fd;
        deleteNode();
        pool->pending--;
        pthread_mutex_unlock(&pool->plock);

        SSL *ssl = SSL_new(svr.ctx);
        SSL_set_fd(ssl, conn_fd);
        if (SSL_accept(ssl) <= 0) {
            ERR_EXIT("[ERROR] Unable to accept SSL connection.\n");
        }

        // STEP3: handle requests from clients
        int tinfo_idx = get_tinfo_idx();
        int pair_idx;
        int stage = 0;
        char *buf_pipe = (char *)malloc(PIPE_SIZE);
        http_request req;
        
        while (!shut_down) {
            memset(buf_pipe, 0, PIPE_SIZE);
            if (SSL_read(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                printf("[LOG] Client disconnected.\n");
                break;
            }
            parse_http_request(&req, buf_pipe);

            
            if (stage == 0) {
                if (!strcmp(req.path, "/")) {
                    // implement the main page
                } else if (!strcmp(req.path, "/login")) {
                    pthread_mutex_lock(&dblock);
                    login_request(buf_pipe, tinfo_idx);
                    pthread_mutex_unlock(&dblock);
                    if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                        printf("[LOG] Client disconnected.\n");
                        break;
                    }
                } else if (!strcmp(req.path, "/register")) {
                    pthread_mutex_lock(&dblock);
                    register_request(buf_pipe);
                    pthread_mutex_unlock(&dblock);
                    if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                        printf("[LOG] Client disconnected.\n");
                        break;
                    }
                } else if (!strcmp(req.path, "/logout")) {
                    pthread_mutex_lock(&ilock[tinfo_idx]);
                    reset_info(tinfo_idx);
                    info[tinfo_idx].serve = 1;
                    pthread_mutex_unlock(&ilock[tinfo_idx]);
                    memset(buf_pipe, 0, PIPE_SIZE);
                    sprintf(buf_pipe, 
                        "HTTP/1.1 200 OK\r\n"
                        "X-mode: console-mode\r\n"
                        "\r\n"
                    );
                    if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                        printf("[LOG] Client disconnected.\n");
                        break;
                    }
                } else if (!strcmp(req.path, "/list_online")) {
                    list_request(buf_pipe, tinfo_idx);
                    if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                        printf("[LOG] Client disconnected.\n");
                        break;
                    }
                } else if (!strcmp(req.path, "/chat/invite")) {
                    int target_tinfo_idx;
                    sscanf(buf_pipe, "target_tinfo_idx=%d", &target_tinfo_idx);

                    if (invite_request(buf_pipe, tinfo_idx, target_tinfo_idx)) {
                        stage = 1;
                        pair_idx = target_tinfo_idx;
                        memset(buf_pipe, 0, PIPE_SIZE);
                        sprintf(buf_pipe, 
                            "HTTP/1.1 200 OK\r\n"
                            "X-mode: console-mode\r\n"
                            "\r\n"
                        );
                        if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                            printf("[LOG] Client disconnected.\n");
                            break;
                        }
                    } else {
                        memset(buf_pipe, 0, PIPE_SIZE);
                        sprintf(buf_pipe, 
                            "HTTP/1.1 401 fail\r\n"
                            "X-mode: console-mode\r\n"
                            "\r\n"
                        );
                        if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                            printf("[LOG] Client disconnected.\n");
                            break;
                        }
                    }
                } else if (!strcmp(req.path, "/check_mail")) {
                    if (strlen(info[tinfo_idx].mail) == 0) {
                        memset(buf_pipe, 0, PIPE_SIZE);
                        sprintf(buf_pipe, 
                            "HTTP/1.1 401 empty\r\n"
                            "X-mode: console-mode\r\n"
                            "\r\n"
                        );
                        if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                            printf("[LOG] Client disconnected.\n");
                            break;
                        }
                    } else {
                        sscanf(info[tinfo_idx].mail, "src_tinfo_idx=%d", &pair_idx);
                        pthread_mutex_lock(&ilock[pair_idx]);
                        memset(buf_pipe, 0, PIPE_SIZE);
                        sprintf(buf_pipe, 
                            "HTTP/1.1 200 OK\r\n"
                            "X-mode: console-mode\r\n"
                            "Content-Type: text/plain\r\n"
                            "\r\n"
                            "src_username=%s",
                            info[pair_idx].username
                        );
                        pthread_mutex_unlock(&ilock[pair_idx]);
                        if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                            printf("[LOG] Client disconnected.\n");
                            break;
                        }
                    }
                } else if (!strcmp(req.path, "/chat/accept")) {
                    pthread_mutex_lock(&ilock[tinfo_idx]);
                    if (strlen(info[tinfo_idx].mail) == 0) {
                        pthread_mutex_unlock(&ilock[tinfo_idx]);
                        memset(buf_pipe, 0, PIPE_SIZE);
                        sprintf(buf_pipe, 
                            "HTTP/1.1 401 fail\r\n"
                            "X-mode: console-mode\r\n"
                            "\r\n"
                        );
                        if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                            printf("[LOG] Client disconnected.\n");
                            break;
                        }
                    } else {
                        info[tinfo_idx].stage = 1;
                        memset(info[tinfo_idx].mail, 0, BUFFER_SIZE);
                        pthread_mutex_unlock(&ilock[tinfo_idx]);
                        pthread_mutex_lock(&ilock[pair_idx]);
                        info[pair_idx].stage = 1;
                        memset(info[pair_idx].mail, 0, BUFFER_SIZE);
                        pthread_mutex_unlock(&ilock[pair_idx]);
                        stage = 1;
                        memset(buf_pipe, 0, PIPE_SIZE);
                        sprintf(buf_pipe, 
                            "HTTP/1.1 200 OK\r\n"
                            "X-mode: console-mode\r\n"
                            "\r\n"
                        );
                        if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                            printf("[LOG] Client disconnected.\n");
                            break;
                        }
                    }
                } else if (!strcmp(req.path, "/chat/reject")) {
                    pthread_mutex_lock(&ilock[tinfo_idx]);
                    memset(info[tinfo_idx].mail, 0, BUFFER_SIZE);
                    pthread_mutex_unlock(&ilock[tinfo_idx]);
                    memset(buf_pipe, 0, PIPE_SIZE);
                    sprintf(buf_pipe, 
                        "HTTP/1.1 200 OK\r\n"
                        "X-mode: console-mode\r\n"
                        "\r\n"
                    );
                    if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                        printf("[LOG] Client disconnected.\n");
                        break;
                    }
                } else if (!strcmp(req.path, "/file/upload")) {
                    pthread_mutex_lock(&ilock[tinfo_idx]);
                    info[tinfo_idx].stage = 2;
                    pthread_mutex_unlock(&ilock[tinfo_idx]);
                    upload_request(ssl, buf_pipe);
                    pthread_mutex_lock(&ilock[tinfo_idx]);
                    info[tinfo_idx].stage = 0;
                    pthread_mutex_unlock(&ilock[tinfo_idx]);
                } else if (!strcmp(req.path, "/file/download")) {
                    pthread_mutex_lock(&ilock[tinfo_idx]);
                    info[tinfo_idx].stage = 2;
                    pthread_mutex_unlock(&ilock[tinfo_idx]);
                    download_request(ssl, buf_pipe);
                    pthread_mutex_lock(&ilock[tinfo_idx]);
                    info[tinfo_idx].stage = 0;
                    pthread_mutex_unlock(&ilock[tinfo_idx]);
                } else if (!strcmp(req.path, "/list_file")) {
                    list_file_request(buf_pipe);
                    if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                        printf("[LOG] Client disconnected.\n");
                        break;
                    }
                }
            } else if (stage == 1) {
                if (!strcmp(req.path, "/chat/get")) {
                    pthread_mutex_lock(&ilock[tinfo_idx]);
                    if (!info[tinfo_idx].stage) {
                        stage = 0;
                        pthread_mutex_unlock(&ilock[tinfo_idx]);
                        memset(buf_pipe, 0, PIPE_SIZE);
                        sprintf(buf_pipe, 
                            "HTTP/1.1 401 leave\r\n"
                            "X-mode: console-mode\r\n"
                            "\r\n"
                        );
                        if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                            printf("[LOG] Client disconnected.\n");
                            break;
                        }
                    } else {
                        if (strlen(info[tinfo_idx].mail) == 0) {
                            pthread_mutex_unlock(&ilock[tinfo_idx]);
                            memset(buf_pipe, 0, PIPE_SIZE);
                            sprintf(buf_pipe, 
                                "HTTP/1.1 401 empty\r\n"
                                "X-mode: console-mode\r\n"
                                "\r\n"
                            );
                            if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                                printf("[LOG] Client disconnected.\n");
                                break;
                            }
                        } else {
                            pthread_mutex_unlock(&ilock[tinfo_idx]);
                            memset(buf_pipe, 0, PIPE_SIZE);
                            sprintf(buf_pipe, 
                                "HTTP/1.1 200 OK\r\n"
                                "X-mode: console-mode\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "%s",
                                info[tinfo_idx].mail
                            );
                            if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                                printf("[LOG] Client disconnected.\n");
                                break;
                            }
                            memset(info[tinfo_idx].mail, 0, BUFFER_SIZE);
                            pthread_mutex_unlock(&ilock[tinfo_idx]);
                        }
                    }
                } else if (!strcmp(req.path, "/chat/send")) {
                    pthread_mutex_lock(&ilock[pair_idx]);
                    if (!info[pair_idx].stage) {
                        stage = 0;
                        pthread_mutex_unlock(&ilock[pair_idx]);
                        memset(buf_pipe, 0, PIPE_SIZE);
                        sprintf(buf_pipe, 
                            "HTTP/1.1 401 leave\r\n"
                            "X-mode: console-mode\r\n"
                            "\r\n"
                        );
                        if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                            printf("[LOG] Client disconnected.\n");
                            break;
                        }
                    } else {
                        strcat(info[pair_idx].mail, buf_pipe);
                        pthread_mutex_unlock(&ilock[pair_idx]);
                        memset(buf_pipe, 0, PIPE_SIZE);
                        sprintf(buf_pipe, 
                            "HTTP/1.1 200 OK\r\n"
                            "X-mode: console-mode\r\n"
                            "\r\n"
                        );
                        if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                            printf("[LOG] Client disconnected.\n");
                            break;
                        }
                    }
                } else if (!strcmp(req.path, "/chat/leave")) {
                    pthread_mutex_lock(&ilock[tinfo_idx]);
                    info[tinfo_idx].stage = 0;
                    memset(info[tinfo_idx].mail, 0, BUFFER_SIZE);
                    pthread_mutex_unlock(&ilock[tinfo_idx]);
                    pthread_mutex_lock(&ilock[pair_idx]);
                    info[pair_idx].stage = 0;
                    memset(info[pair_idx].mail, 0, BUFFER_SIZE);
                    pthread_mutex_unlock(&ilock[pair_idx]);
                    stage = 0;
                    memset(buf_pipe, 0, PIPE_SIZE);
                    sprintf(buf_pipe, 
                        "HTTP/1.1 200 OK\r\n"
                        "X-mode: console-mode\r\n"
                        "\r\n"
                    );
                    if (SSL_write(ssl, buf_pipe, PIPE_SIZE) <= 0) {
                        printf("[LOG] Client disconnected.\n");
                        break;
                    }
                }
            }
        }

        // STEP4: clean up connection
        pthread_mutex_lock(&pool->plock);
        pool->curr_cli--;
        pthread_mutex_unlock(&pool->plock);
        pthread_mutex_lock(&ilock[tinfo_idx]);
        reset_info(tinfo_idx);
        pthread_mutex_unlock(&ilock[tinfo_idx]);
        free(buf_pipe);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(conn_fd);
    }

    // STEP5: worker thread leaves
    pool->num_threads--;
    printf("[LOG] Worker thread %d left.\n", thread_idx);
    if (pool->num_threads == 0) {
        pthread_cond_signal(&pool->mcond);
    }
    pthread_mutex_unlock(&pool->plock);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        ERR_EXIT("[ERROR] Usage: <port>.\n");
    }
    init_db();
    init_server(argv);
    fprintf(stderr, "[LOG] starting on %.80s, port %d, fd %d.\n", svr.hostname, svr.port, svr.listen_fd);
    fprintf(stderr, "%s[reminder] You can press Ctrl + C to gracefully shut down the server,%s\n", YELLOW, RESET);
    fprintf(stderr, "%s[reminder] or you can press Ctrl + \\ to force the server to shut down.%s\n", YELLOW, RESET);
    
    struct sockaddr_in client_addr;
    int client_len = sizeof(client_addr);
    int conn_fd;

    tpool_init(MAX_CLIENTS);
    tinfo_init(MAX_CLIENTS);
    
    // TODO: Add multithreading
    while (!shut_down) {
        conn_fd = accept(svr.listen_fd, (struct sockaddr *)&client_addr, (socklen_t*)&client_len);
        if (conn_fd < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            if (errno == ENFILE) {
                fprintf(stderr, "[WARN] out of file descriptor table ... (maxconn %d).\n", getdtablesize());
                continue;
            }
            if (shut_down) {
                break;
            }
            ERR_EXIT("[ERROR] Unable to accept connection.\n");
        }

        pthread_mutex_lock(&pool->plock);
        if (pool->curr_cli >= MAX_CLIENTS) {
            fprintf(stderr, "[WARN] Max clients reached. Rejecting connection.\n");
            close(conn_fd);
            pthread_mutex_unlock(&pool->plock);
            continue;
        }
        insertNode(conn_fd);
        pool->pending++;
        pool->curr_cli++;
        pthread_mutex_unlock(&pool->plock);
        pthread_cond_signal(&pool->wcond); // wake up single worker_thread to handle the connection
    }

    shutdown_server();
    fprintf(stderr, "\n[LOG] The server has been forced to shut down.\n");
    return 0;
}

// ======================================================================================================

void register_request(char *request) {
    // printf("[LOG] into regi func\n");
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
    sscanf(request, "username=%[^&]&password=%s", username, password);
    
    snprintf(query, QUERY_SIZE, "INSERT INTO Users (Username, Password) VALUES ('%s', '%s')", username, password);
    memset(request, 0, PIPE_SIZE);
    if (mysql_query(db, query)) {
        sprintf(request,
            "HTTP/1.1 401 fail\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
        return;
    }
    sprintf(request,
        "HTTP/1.1 200 OK\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
    );
    return;
}

void login_request(char *request, int tinfo_idx) {
    // printf("[LOG] into logi func\n");
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
    sscanf(request, "username=%[^&]&password=%s", username, password);

    // check if duplicate login
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_mutex_lock(&ilock[i]);
        if (!strcmp(info[i].username, username)) {
            pthread_mutex_unlock(&ilock[i]);
            memset(request, 0, PIPE_SIZE);
            sprintf(request,
                "HTTP/1.1 401 fail\r\n"
                "X-mode: console-mode\r\n"
                "\r\n"
            );
            return;
        }
        pthread_mutex_unlock(&ilock[i]);
    }

    MYSQL_RES *result;
    MYSQL_ROW row;
    
    // check if user not found
    snprintf(query, QUERY_SIZE, "SELECT EXISTS (SELECT 1 FROM Users WHERE Username = '%s')", username);
    memset(request, 0, PIPE_SIZE);
    if (mysql_query(db, query)) {
        sprintf(request,
            "HTTP/1.1 401 fail\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
        return;
    }
    result = mysql_store_result(db);
    if (!result) {
        sprintf(request,
            "HTTP/1.1 401 fail\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
        return;
    }
    row = mysql_fetch_row(result);
    if (row == NULL || row[0][0] == '0') {
        sprintf(request,
            "HTTP/1.1 401 usrf\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
        mysql_free_result(result);
        return;
    }
    mysql_free_result(result);

    // check if wrong password
    snprintf(query, QUERY_SIZE, "SELECT EXISTS (SELECT 1 FROM Users WHERE Username = '%s' AND Password = '%s')", username, password);
    if (mysql_query(db, query)) {
        sprintf(request,
            "HTTP/1.1 401 fail\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
        return;
    }
    result = mysql_store_result(db);
    if (!result) {
        sprintf(request,
            "HTTP/1.1 401 fail\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
        return;
    }
    row = mysql_fetch_row(result);
    if (row == NULL || row[0][0] == '0') {
        sprintf(request,
            "HTTP/1.1 401 pwdf\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
        mysql_free_result(result);
        return;
    }
    mysql_free_result(result);

    // login success
    pthread_mutex_lock(&ilock[tinfo_idx]);
    strcpy(info[tinfo_idx].username, username);
    pthread_mutex_unlock(&ilock[tinfo_idx]);
    sprintf(request,
        "HTTP/1.1 200 OK\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
    );
    return;
}

static void init_db () {
    db = mysql_init(NULL);
    if (mysql_real_connect(db, SERVER, USER, PASSWORD, NULL, 0, NULL, 0) == NULL) {
        printf("mysql_real_connect() failed: %s\n", mysql_error(db));
        mysql_close(db);
        ERR_EXIT("mysql_real_connect() failed");
    }
    if (mysql_query(db, "CREATE DATABASE IF NOT EXISTS chatIN")) {
        mysql_close(db);
        ERR_EXIT("CREATE DATABASE failed");
    }
    if (mysql_select_db(db, "chatIN")) {
        mysql_close(db);
        ERR_EXIT("CREATE DATABASE failed");
    }
    if (mysql_query(db, "CREATE TABLE IF NOT EXISTS Users (Id INT AUTO_INCREMENT PRIMARY KEY, Username VARCHAR(50) NOT NULL UNIQUE, Password VARCHAR(50) NOT NULL)")) {
        mysql_close(db);
        ERR_EXIT("CREATE TABLE failed");
    }
    if (pthread_mutex_init(&dblock, NULL) != 0) {
        mysql_close(db);
        ERR_EXIT("pthread_mutex_init() failed");
    }
}

// ======================================================================================================

void list_request(char *request, int tinfo_idx) {
    char list[15 * USERNAME_LEN] = "";
    char entry[2 * USERNAME_LEN];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (i == tinfo_idx) {
            continue;
        }
        pthread_mutex_lock(&ilock[i]);
        if (strlen(info[i].username) > 0) {
            if (!info[i].stage) {
                sprintf(entry, "%d. %s\n", i, info[i].username);
            } else {
                sprintf(entry, "%d. %s (busy)\n", i, info[i].username);
            }
            strcat(list, entry);
        }
        pthread_mutex_unlock(&ilock[i]);
    }
    memset(request, 0, PIPE_SIZE);
    sprintf(request,
        "HTTP/1.1 200 OK\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
        "%s",       // online_user_list
        list
    );
    return;
}

int invite_request (char *request, int tinfo_idx, int target_tinfo_idx) {
    memset(request, 0, PIPE_SIZE);
    
    pthread_mutex_lock(&ilock[target_tinfo_idx]);
    if (info[target_tinfo_idx].serve == 0
        || info[target_tinfo_idx].username == NULL
        || info[target_tinfo_idx].stage > 0
        || strlen(info[target_tinfo_idx].mail) > 0) {
        pthread_mutex_unlock(&ilock[target_tinfo_idx]);
        sprintf(request,
            "HTTP/1.1 401 fail\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
    } else {
        char mail[BUFFER_SIZE]; // mail for chat invitation
        sprintf(mail,
            "src_tinfo_idx=%d",     
            tinfo_idx
        );
        strcpy(info[target_tinfo_idx].mail, mail);
        pthread_mutex_unlock(&ilock[target_tinfo_idx]);

        for (int timeout = 0; timeout < 5; timeout++) {
            pthread_mutex_lock(&ilock[tinfo_idx]);
            if (info[tinfo_idx].stage == 1) {
                pthread_mutex_unlock(&ilock[tinfo_idx]);
                sprintf(request,
                    "HTTP/1.1 200 OK\r\n"
                    "X-mode: console-mode\r\n"
                    "\r\n"
                );
                return 1;
            }
            if (timeout == 4) {
                memset(info[target_tinfo_idx].mail, 0, BUFFER_SIZE);
            }
            pthread_mutex_unlock(&ilock[tinfo_idx]);
            sleep(3);
        }
        sprintf(request,
            "HTTP/1.1 401 fail\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
    }
    return 0;
}

// ======================================================================================================

void parse_http_request(http_request *req, char *request) {
    // Parse the request line
    sscanf(request, "%s %s %s", req->method, req->path, req->http_version);

    // Parse the headers
    char *headers_start = strstr(request, "\r\n") + 2; // Skip past the first "\r\n"
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
    strcpy(request, body);
    return;
}

int parse_http_request_v1(http_request *req, char *request) {
    // Parse the request line
    sscanf(request, "%s %s %s", req->method, req->path, req->http_version);

    // Parse the headers
    int content_length;
    char *headers_start = strstr(request, "\r\n") + 2; // Skip past the first "\r\n"
    char *header_end = strstr(headers_start, "\r\n\r\n"); // Find the end of the headers
    // check the content-length
    char *content_length_start = strstr(headers_start, "Content-length: ");
    if (content_length_start) {
        sscanf(content_length_start, "Content-length: %d", &content_length);
    }

    // Parse the body
    char *body = header_end + 4; // Skip past the "\r\n\r\n"
    memcpy(request, body, content_length);
    return content_length;
}

void upload_request(SSL *ssl, char *request) {
    char *original_filename = strrchr(request, '/') + 1;
    char filename[FILENAME_LEN];
    sprintf(filename, "files/chatIN_%d_%s", svr.file_counter++, original_filename);

    int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        memset(request, 0, PIPE_SIZE);
        sprintf(request, 
            "HTTP/1.1 401 fail\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
        if (SSL_write(ssl, request, PIPE_SIZE) <= 0) {
            ERR_EXIT("[ERROR] SSL_write failed.\n");
        }
        return;
    }
    memset(request, 0, PIPE_SIZE);
    sprintf(request, 
        "HTTP/1.1 200 OK\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
    );
    if (SSL_write(ssl, request, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }
    
    // free(request);
    // request = (char *)malloc(CHUNK_SIZE);
    http_request req;
    size_t bytes_read;
    while ((bytes_read = SSL_read(ssl, request, PIPE_SIZE)) > 0) {
        int content_length = parse_http_request_v1(&req, request);
        if (content_length == 0) {
            break;
        }

        if (write(file_fd, request, content_length) < 0) {
            break;
        }
        
        memset(request, 0, PIPE_SIZE);
        sprintf(request, 
            "HTTP/1.1 200 OK\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
        if (SSL_write(ssl, request, strlen(request)) <= 0) {
            ERR_EXIT("[ERROR] SSL_write failed.\n");
        }
    }
    memset(request, 0, PIPE_SIZE);
    sprintf(request, 
        "HTTP/1.1 200 OK\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
    );
    if (SSL_write(ssl, request, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }
    close(file_fd);
    // free(request);
    // request = (char *)malloc(PIPE_SIZE);
    printf("[LOG] \"%s\" uploaded successfully.\n", filename);
    return;
}

void list_file_request (char *request) {
    struct dirent *entry;
    DIR *dp = opendir("files");
    if (dp == NULL) {
        ERR_EXIT("[ERROR] Unable to open directory.\n");
    }
    char list[15 * FILENAME_LEN] = "";
    char line[2 * FILENAME_LEN];

    int idx = 1;
    while ((entry = readdir(dp))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        sprintf(line, "%d. %s\n", idx++, entry->d_name);
        strcat(list, line);
    }
    closedir(dp);

    memset(request, 0, PIPE_SIZE);
    sprintf(request,
        "HTTP/1.1 200 OK\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
        "%s",       // file_list
        list
    );
    return;
}

void download_request (SSL*ssl, char *request) {
    char filename[FILENAME_LEN] = "files/";
    strcat(filename, request);

    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        memset(request, 0, PIPE_SIZE);
        sprintf(request, 
            "HTTP/1.1 401 fail\r\n"
            "X-mode: console-mode\r\n"
            "\r\n"
        );
        if (SSL_write(ssl, request, PIPE_SIZE) <= 0) {
            ERR_EXIT("[ERROR] SSL_write failed.\n");
        }
        return;
    }
    memset(request, 0, PIPE_SIZE);
    sprintf(request, 
        "HTTP/1.1 200 OK\r\n"
        "X-mode: console-mode\r\n"
        "\r\n"
    );
    if (SSL_write(ssl, request, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }
    
    char *tmp_buf = (char *)malloc(PIPE_SIZE - 128);
    memset(tmp_buf, 0, PIPE_SIZE - 128);

    size_t bytes_read;
    while ((bytes_read = read(file_fd, tmp_buf, PIPE_SIZE - 128)) > 0) {
        memset(request, 0, PIPE_SIZE);
        sprintf(request, 
            "HTTP/1.1 200 OK\r\n"
            "X-mode: console-mode\r\n"
            "Content-Type: text/plain\r\n"
            "Content-length: %zd\r\n"
            "\r\n",
            bytes_read
        );
        int response_len = strlen(request);
        memcpy(request + response_len, tmp_buf, bytes_read);
        
        size_t total_len = response_len + bytes_read;
        if (SSL_write(ssl, request, total_len) <= 0) {
            ERR_EXIT("[ERROR] SSL_write failed.\n");
        }
        if (SSL_read(ssl, request, PIPE_SIZE) <= 0) {
            ERR_EXIT("[ERROR] SSL_read failed.\n");
        }
        memset(tmp_buf, 0, PIPE_SIZE - 128);
    }
    memset(request, 0, PIPE_SIZE);
    sprintf(request, 
        "HTTP/1.1 200 OK\r\n"
        "X-mode: console-mode\r\n"
        "Content-length: 0\r\n"
        "\r\n"
    );
    if (SSL_write(ssl, request, PIPE_SIZE) <= 0) {
        ERR_EXIT("[ERROR] SSL_write failed.\n");
    }
    close(file_fd);
    free(tmp_buf);
    return;
}

// ======================================================================================================

void tpool_init(int n_threads) {
    pool = (tpool *)malloc(sizeof(tpool));
    if (pool == NULL) {
        ERR_EXIT("[ERROR] Unable to allocate memory for thread pool.\n");
    }
    if (pthread_mutex_init(&pool->plock, NULL) != 0) {
        ERR_EXIT("[ERROR] Unable to initialize mutex.\n");
    }
    if (pthread_cond_init(&pool->wcond, NULL) != 0) {
        ERR_EXIT("[ERROR] Unable to initialize condition variable.\n");
    }
    if (pthread_cond_init(&pool->mcond, NULL) != 0) {
        ERR_EXIT("[ERROR] Unable to initialize condition variable.\n");
    }
    pool->curr_cli = 0;
    pool->pending = 0;
    Node *head = genNode(0);
    pool->conn_queue = head;
    pool->conn_tail = NULL;

    pool->num_threads = n_threads;
    pool->worker_thread = (pthread_t *)malloc(n_threads * sizeof(pthread_t));
    if (pool->worker_thread == NULL) {
        ERR_EXIT("[ERROR] Unable to allocate memory for worker threads.\n");
    }
    pthread_mutex_lock(&pool->plock);
    for (int i = 0; i < n_threads; i++) {
        if (pthread_create(&pool->worker_thread[i], NULL, worker_thread, NULL) != 0) {
            ERR_EXIT("[ERROR] Unable to create worker thread.\n");
        }
        pthread_cond_wait(&pool->mcond, &pool->plock);
    }
    pthread_mutex_unlock(&pool->plock);
    return;
}

void reset_info(int i) {
    info[i].serve = 0;
    memset(info[i].username, 0, USERNAME_LEN);
    info[i].stage = 0;
    memset(info[i].mail, 0, BUFFER_SIZE);
    return;
}

void tinfo_init(int n_threads) {
    info = (tinfo *)malloc(sizeof(tinfo) * n_threads);
    if (info == NULL) {
        ERR_EXIT("[ERROR] Unable to allocate memory for thread info.\n");
    }
    for (int i = 0; i < n_threads; i++) {
        reset_info(i);
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (pthread_mutex_init(&ilock[i], NULL) != 0) {
            ERR_EXIT("[ERROR] Unable to initialize mutex.\n");
        }
    }
    return;
}

int get_tinfo_idx () {
    int found = 0;
    int idx = 0;
    for (; idx < MAX_CLIENTS; idx++) {
        pthread_mutex_lock(&ilock[idx]);
        if (!info[idx].serve) {
            info[idx].serve = 1;
            found = 1;
            pthread_mutex_unlock(&ilock[idx]);
            break;
        }
        pthread_mutex_unlock(&ilock[idx]);
        if (!found && idx == MAX_CLIENTS - 1) {
            idx = 0;
        }
    }
    return idx;
}

Node *genNode(int conn_fd) {
    Node *newNode = (Node *)malloc(sizeof(Node));
    if (newNode == NULL) {
        ERR_EXIT("[ERROR] Unable to allocate memory for new node.\n");
    }
    newNode->conn_fd = conn_fd;
    newNode->next = NULL;
    return newNode;
}

void insertNode(int conn_fd) {
    Node *current = genNode(conn_fd);
    if (pool->pending == 0){
        pool->conn_queue->next = current;
        pool->conn_tail = current;
    } else {
        pool->conn_tail->next = current;
        pool->conn_tail = current;
    }
    return;
}

void deleteNode(){
    Node *ptr = pool->conn_queue;
    if (pool->pending == 1){
        pool->conn_tail = NULL;
        ptr = ptr->next;
        pool->conn_queue->next = NULL;
        free(ptr);
    } else {
        ptr = ptr->next;
        pool->conn_queue->next = ptr->next;
        free(ptr);
    }
    return;
}

// ======================================================================================================

void signal_handler(int sig) {
    if (sig == SIGINT) {
        shut_down = 1;
        close(svr.listen_fd);
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
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        ERR_EXIT("[ERROR] Unable to create SSL context.\n");
    }
    return ctx;
}

void configure_context(SSL_CTX *ctx) {
    if (SSL_CTX_use_certificate_file(ctx, CERT_FILE, SSL_FILETYPE_PEM) <= 0) {
        ERR_EXIT("[ERROR] Unable to load certificate.\n");
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, KEY_FILE, SSL_FILETYPE_PEM) <= 0) {
        ERR_EXIT("[ERROR] Unable to load private key.\n");
    }
    if (!SSL_CTX_check_private_key(ctx)) {
        ERR_EXIT("[ERROR] Private key does not match the certificate public key.\n");
    }
}

static void init_server(char** argv) {
    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = (unsigned short)atoi(argv[1]);
    if (svr.port == 0 || svr.port > 65536) {
        ERR_EXIT("[ERROR] Invalid port.\n");
    }
    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) {
        ERR_EXIT("[ERROR] Unable to create socket.\n");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(svr.port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    int tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("[ERROR] Unable to set socket options.\n");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        ERR_EXIT("[ERROR] Unable to bind.\n");
    }
    if (listen(svr.listen_fd, 1) < 0) {
        ERR_EXIT("[ERROR] Unable to listen.\n");
    }

    initialize_openssl();
    svr.ctx = create_context();
    configure_context(svr.ctx);

    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        ERR_EXIT("[ERROR] Unable to set signal handler.\n");
    }
    return;
}

void shutdown_server() {
    pthread_mutex_lock(&pool->plock);
    pthread_cond_broadcast(&pool->wcond);
    while (pool->num_threads > 0) {
        pthread_cond_wait(&pool->mcond, &pool->plock);
    }
    pthread_mutex_unlock(&pool->plock);

    pthread_mutex_destroy(&pool->plock);
    pthread_cond_destroy(&pool->wcond);
    pthread_cond_destroy(&pool->mcond);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_mutex_destroy(&ilock[i]);
    }
    free(pool->worker_thread);
    free(pool);
    free(info);
    SSL_CTX_free(svr.ctx);
    cleanup_openssl();
    return;
}