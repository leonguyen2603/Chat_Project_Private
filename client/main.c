#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

#define PORT 8888
#define SERVER_IP "127.0.0.1"

char my_username[64] = "";

void save_chat_history(const char *username, const char *msg) {
    char filename[128];
    snprintf(filename, sizeof(filename), "%s_history.txt", username);
    FILE *f = fopen(filename, "a");
    if (f) {
        fprintf(f, "%s", msg);
        fclose(f);
    }
}

void receive_history(int sockfd) {
    char buffer[2048];
    int got_end = 0;
    while (1) {
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';

        if (!got_end) {
            if (strcmp(buffer, "END_HISTORY\n") == 0) {
                printf("Received response from server: END_HISTORY\n");
                got_end = 1;
                continue;
            }
            printf("Received response from server: %s", buffer);
            save_chat_history(my_username, buffer);
            if (strstr(buffer, "=== Ban da vao phong chat:") != NULL) {
                printf("=== Ban da vao phong chat ===\n");
                printf("[You]: ");
                fflush(stdout);
                return;
            }
        } else {
            if (strstr(buffer, "=== Ban da vao phong chat:") != NULL) {
                printf("Received response from server: %s", buffer);
                printf("=== Ban da vao phong chat ===\n");
                printf("[You]: ");
                fflush(stdout);
                return;
            }
        }
    }
}

void *recv_thread(void *arg) {
    int sockfd = *(int *)arg;
    char buffer[2048];
    while (1) {
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        // Luôn in prefix cho mọi tin nhắn từ server (kể cả thông báo hệ thống)
        printf("Received response from server: %s", buffer);
        // In prompt sau mọi tin nhắn chat hoặc thông báo (trừ khi là END_HISTORY hoặc welcome)
        if (
            strcmp(buffer, "END_HISTORY\n") != 0 &&
            strstr(buffer, "=== Ban da vao phong chat:") == NULL &&
            strstr(buffer, "MENU PHONG CHAT") == NULL &&
            strstr(buffer, "Vui long nhap lua chon!") == NULL &&
            strstr(buffer, "Lua chon khong hop le!") == NULL
        ) {
            printf("[You]: ");
            fflush(stdout);
        }
    }
    return NULL;
}

void chat_loop(int sockfd) {
    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, &sockfd);

    char buffer[2048];
    // In prompt ngay khi vao chat_loop
    printf("[You]: ");
    fflush(stdout);

    while (fgets(buffer, sizeof(buffer), stdin)) {
        buffer[strcspn(buffer, "\n")] = 0;
        if (strncmp(buffer, "@exit@", 6) == 0) {
            printf("Dang thoat khoi cuoc tro chuyen...\n");
            break;
        }
        if (strlen(buffer) == 0) {
            printf("[You]: ");
            fflush(stdout);
            continue;
        }
        size_t len = strlen(buffer);
        if (len > sizeof(buffer) - 2) buffer[sizeof(buffer) - 2] = '\0';
        char sendbuf[2049];
        snprintf(sendbuf, sizeof(sendbuf), "%s\n", buffer);
        send(sockfd, sendbuf, strlen(sendbuf), 0);
        char line[1100];
        snprintf(line, sizeof(line), "[You]: %.1020s\n", buffer);
        save_chat_history(my_username, line);
        printf("[You]: ");
        fflush(stdout);
    }
}

void show_menu() {
    printf("==== MENU ====\n");
    printf("1. Dang nhap\n");
    printf("2. Dang ky\n");
    printf("3. Thoat\n");
    printf("Chon: ");
    fflush(stdout);
}

int handle_auth(int sockfd) {
    int authenticated = 0;
    char buffer[2048];
    while (!authenticated) {
        show_menu();
        int choice;
        if (scanf("%d", &choice) != 1) {
            printf("Vui long nhap so (1-3)!\n");
            while (getchar() != '\n');
            continue;
        }
        getchar();

        if (choice == 3) {
            close(sockfd);
            exit(0);
        }
        if (choice != 1 && choice != 2) {
            printf("Lua chon khong hop le!\n");
            continue;
        }

        char username[64], password[64];
        printf("Username: ");
        fgets(username, sizeof(username), stdin);
        username[strcspn(username, "\n")] = 0;
        printf("Password: ");
        fgets(password, sizeof(password), stdin);
        password[strcspn(password, "\n")] = 0;

        snprintf(buffer, sizeof(buffer), "%d|%s|%s\n", choice, username, password);
        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            perror("send");
            return 0;
        }

        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            printf("Mat ket noi voi server!\n");
            close(sockfd);
            exit(1);
        }
        buffer[bytes] = '\0';
        printf("%s", buffer);

        if (strstr(buffer, "thanh cong") != NULL) {
            authenticated = 1;
            strcpy(my_username, username);
        }
    }
    return 1;
}

int handle_room_selection(int sockfd) {
    char buffer[4096];
    char input[128];
    int bytes;
    while (1) {
        printf("Waiting to receive menu from server...\n");
        // Nhận liên tục cho đến khi nhận được cả menu và prompt "Chon:"
        buffer[0] = '\0';
        int got_menu = 0, got_prompt = 0;
        while (!(got_menu && got_prompt)) {
            bytes = recv(sockfd, buffer + strlen(buffer), sizeof(buffer) - 1 - strlen(buffer), 0);
            if (bytes <= 0) {
                printf("Server closed connection or error occurred. Trying to reconnect...\n");
                close(sockfd);
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in addr = {0};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(PORT);
                inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

                if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    perror("connect");
                    return 0;
                }
                if (!handle_auth(sockfd)) {
                    close(sockfd);
                    return 0;
                }
                buffer[0] = '\0';
                continue;
            }
            buffer[strlen(buffer) + bytes] = '\0';
            if (strstr(buffer, "=== MENU PHONG CHAT ===")) got_menu = 1;
            if (strstr(buffer, "Chon:")) got_prompt = 1;
            if (strstr(buffer, "Vui long nhap lua chon!") != NULL) {
                printf("%s", buffer);
                buffer[0] = '\0';
                got_menu = 0;
                got_prompt = 0;
            }
        }
        printf("%s", buffer);

        // Yêu cầu nhập lại nếu không nhập số hợp lệ (chỉ cho phép 0,1,2,3)
        int valid_choice = 0;
        while (!valid_choice) {
            printf("Enter your choice: ");
            if (!fgets(input, sizeof(input), stdin)) return 0;
            input[strcspn(input, "\n")] = 0;
            if (strlen(input) != 1 || strchr("0123", input[0]) == NULL) {
                printf("Vui long nhap so hop le (0-3)!\n");
                continue;
            }
            valid_choice = 1;
        }
        snprintf(buffer, sizeof(buffer), "%s\n", input);
        printf("Sending choice to server: %s\n", input);
        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            perror("send choice");
            printf("Failed to send choice to server. Exiting...\n");
            return 0;
        }

        // Chờ phản hồi cho đến khi nhận được thông báo vào phòng hoặc menu lại
        while (1) {
            printf("Waiting for server response...\n");
            struct timeval tv = {30, 0};
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);

            int activity = select(sockfd + 1, &readfds, NULL, NULL, &tv);
            if (activity == 0) {
                printf("Timeout: Không nhận được phản hồi từ server. Trying to reconnect...\n");
                close(sockfd);
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in addr = {0};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(PORT);
                inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

                if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    perror("connect");
                    return 0;
                }
                if (!handle_auth(sockfd)) {
                    close(sockfd);
                    return 0;
                }
                break;
            }
            if (activity < 0) {
                perror("select");
                printf("Select error. Exiting...\n");
                return 0;
            }

            bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) {
                printf("Server closed connection or error occurred. Trying to reconnect...\n");
                close(sockfd);
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in addr = {0};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(PORT);
                inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

                if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    perror("connect");
                    return 0;
                }
                if (!handle_auth(sockfd)) {
                    close(sockfd);
                    return 0;
                }
                break;
            }
            buffer[bytes] = '\0';
            printf("Received response from server: %s", buffer);

            if (strstr(buffer, "=== Ban da vao phong chat:") != NULL) {
                return 1; // Thoát ngay khi vào phòng
            }
            if (strstr(buffer, "MENU PHONG CHAT")) {
                printf("Server sent menu again.\n");
                break;
            }
            if (strstr(buffer, "Vui long nhap lua chon!")) {
                printf("Server nhắc nhở nhập lựa chọn.\n");
                break;
            }
            if (strstr(buffer, "Lua chon khong hop le!")) {
                printf("Server báo lựa chọn không hợp lệ.\n");
                break;
            }
        }
    }
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    if (!handle_auth(sockfd)) {
        close(sockfd);
        return 1;
    }

    if (!handle_room_selection(sockfd)) {
        close(sockfd);
        return 1;
    }

    receive_history(sockfd); 
    chat_loop(sockfd);

    close(sockfd);
    return 0;
}