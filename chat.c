#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>

#define MAX_MESSAGES 10    // 最多存儲的歷史消息數量
#define BUFFER_SIZE 256    // 單條消息最大長度
#define TERMINAL_HEIGHT 15 // 模擬終端高度

pthread_mutex_t lock;  // 鎖保護共享資源
char messages[MAX_MESSAGES][BUFFER_SIZE]; // 存儲歷史消息
int message_count = 0;  // 當前消息數量
int start_index = 0;    // 循環隊列的起始索引

char current_input[BUFFER_SIZE] = "";  // 暫存用戶當前輸入

// 非阻塞輸入設置
struct termios oldt, newt;

// 切換為非阻塞輸入模式
void enable_non_blocking_input() {
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); // 關閉行緩衝和輸出回顯
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

// 恢復默認輸入模式
void disable_non_blocking_input() {
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

// 在終端中刷新顯示
void refresh_terminal() {
    // 清空屏幕
    printf("\033[2J");

    // 鎖住消息隊列
    pthread_mutex_lock(&lock);

    // 顯示歷史消息
    int display_count = message_count > MAX_MESSAGES ? MAX_MESSAGES : message_count;
    int offset = message_count > TERMINAL_HEIGHT - 1
                     ? message_count - (TERMINAL_HEIGHT - 1)
                     : 0;

    printf("\033[%d;0H%s\n", 1, "=-=-=-=-=");
    printf("\033[%d;0H%s\n", 2, "=-=-=-=-=");
    printf("\033[%d;0H%s\n", 3, "=-=-=-=-=");
    for (int i = offset; i < offset + display_count; i++) {
        int index = (start_index + i) % MAX_MESSAGES;
        printf("\033[%d;0H%s\n", i - offset + 4, messages[index]);
    }

    // 顯示提示行和當前輸入
    printf("\033[%d;0HPlease enter a message: %s", TERMINAL_HEIGHT, current_input);
    fflush(stdout);

    pthread_mutex_unlock(&lock);
}

// 定時任務線程：每3秒生成一條 "haha: i am boy"
void *haha_thread(void *arg) {
    while (1) {
        sleep(1);

        pthread_mutex_lock(&lock);

        sprintf(messages[(start_index + message_count) % MAX_MESSAGES], "jjj: i am nbnot");
        // snprintf(messages[(start_index + message_count) % MAX_MESSAGES], BUFFER_SIZE, "haha: i am boy");
        if (message_count < MAX_MESSAGES) {
            message_count++;
        } else {
            // 隊列已滿，移動起始索引
            start_index = (start_index + 1) % MAX_MESSAGES;
        }

        pthread_mutex_unlock(&lock);

        refresh_terminal();
    }
    return NULL;
}

// 輸入線程：處理用戶輸入
void *input_thread(void *arg) {
    char ch;
    int input_length = 0;

    while (1) {
        // 讀取單個字符
        ch = getchar();

        pthread_mutex_lock(&lock);

        if (ch == '\n') {
            // 回車鍵：將當前輸入保存為消息
            if (input_length > 0) {
                sprintf(messages[(start_index + message_count) % MAX_MESSAGES], "You entered: %s", current_input);
                snprintf(messages[(start_index + message_count) % MAX_MESSAGES], BUFFER_SIZE, "You entered: %s", current_input);
                if (message_count < MAX_MESSAGES) {
                    message_count++;
                } else {
                    // 隊列已滿，移動起始索引
                    start_index = (start_index + 1) % MAX_MESSAGES;
                }
                input_length = 0;
                current_input[0] = '\0'; // 清空當前輸入
            }
        } else if (ch == 127) { // 處理退格鍵
            if (input_length > 0) {
                input_length--;
                current_input[input_length] = '\0';
            }
        } else if (input_length < BUFFER_SIZE - 1) {
            // 普通字符輸入
            current_input[input_length++] = ch;
            current_input[input_length] = '\0';
        }

        pthread_mutex_unlock(&lock);

        refresh_terminal();
    }
    return NULL;
}

int main() {
    pthread_t tid1, tid2;

    // 初始化鎖
    pthread_mutex_init(&lock, NULL);

    // 啟用非阻塞輸入
    enable_non_blocking_input();

    // 創建線程
    pthread_create(&tid1, NULL, haha_thread, NULL);
    pthread_create(&tid2, NULL, input_thread, NULL);

    // 等待線程完成
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    // 恢復默認輸入模式
    disable_non_blocking_input();

    // 銷毀鎖
    pthread_mutex_destroy(&lock);

    return 0;
}
