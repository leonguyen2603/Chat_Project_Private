#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8888
#define SERVER_IP "127.0.0.1"

void *recv_thread(void *arg) {
    int sockfd = *(int *)arg;
    char buffer[1024];
    while (1) {
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        printf("\r%s", buffer);
        printf("[You]: ");
        fflush(stdout);
    }
    return NULL;
}

void show_menu() {
    printf("==== MENU ====\n");
    printf("1. Dang nhap\n");
    printf("2. Dang ky\n");
    printf("3. Thoat\n");
    printf("Chon: ");
    fflush(stdout);
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    int authenticated = 0;
    char buffer[1024];
    while (!authenticated) {
        show_menu();
        int choice;
        if (scanf("%d", &choice) != 1) {
            // Xử lý nhập sai kiểu dữ liệu
            printf("Vui long nhap so (1-3)!\n");
            while (getchar() != '\n'); // Xóa bộ đệm
            continue;
        }
        getchar(); // bỏ ký tự '\n'

        if (choice == 3) {
            close(sockfd);
            return 0;
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

        // Gửi lựa chọn, username, password lên server
        snprintf(buffer, sizeof(buffer), "%d|%s|%s\n", choice, username, password);
        send(sockfd, buffer, strlen(buffer), 0);

        // Nhận phản hồi từ server
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            printf("Mat ket noi voi server!\n");
            close(sockfd);
            return 1;
        }
        buffer[bytes] = '\0';
        printf("%s", buffer);

        if (strstr(buffer, "thanh cong") != NULL) {
            authenticated = 1;
        }
    }

    // Đã xác thực, vào giao diện chat
    printf("=== Ban da vao phong chat ===\n");
    printf("[You]: ");
    fflush(stdout);

    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, &sockfd);

    while (fgets(buffer, sizeof(buffer), stdin)) {
        send(sockfd, buffer, strlen(buffer), 0);
        printf("[You]: ");
        fflush(stdout);
    }

    close(sockfd);
    return 0;
}