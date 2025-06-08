#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8888
#define SERVER_IP "127.0.0.1"

char my_username[64] = "";

// Prototype
void save_chat_history(const char *username, const char *msg);

void *recv_thread(void *arg) 
{
    int sockfd = *(int *)arg;
    char buffer[1024];
    while (1) 
    {
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';

        // Clear the current line in terminal
        printf("\r\33[2K"); // Clear line and move cursor to beginning

        printf("%s", buffer); // Print the received message

        extern char my_username[64];
        save_chat_history(my_username, buffer);

        // In lại prompt nhập
        printf("[You]: ");
        fflush(stdout);
    }
    return NULL;
}

// Menu 
void show_menu() 
{
    printf("==== MENU ====\n");
    printf("1. Dang nhap\n");
    printf("2. Dang ky\n");
    printf("3. Thoat\n");
    printf("Chon: ");
    fflush(stdout);
}

// Save chat history to a file
// The file is named "<username>_history.txt"
void save_chat_history(const char *username, const char *msg) 
{
    char filename[128];
    snprintf(filename, sizeof(filename), "%s_history.txt", username);
    FILE *f = fopen(filename, "a");
    if (f) 
    {
        fprintf(f, "%s", msg);
        fclose(f);
    }
}

int main() 
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); // TCP
    if (sockfd < 0) 
    {
        perror("socket");
        return 1;
    }
    struct sockaddr_in addr = {0}; // Server address structure
    addr.sin_family = AF_INET; // IPv4
    addr.sin_port = htons(PORT); // Port number
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr); // Convert IP address from text to binary

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) 
    {
        perror("connect");
        return 1;
    }

    int authenticated = 0;
    char buffer[1024];
    char my_username[64] = "";
    while (!authenticated) 
    {
        show_menu();
        int choice;
        // Check if scanf was invalid
        if (scanf("%d", &choice) != 1) 
        {
            printf("Vui long nhap so (1-3)!\n");
            while (getchar() != '\n'); // Clear invalid input
            continue;
        }
        getchar(); // Clear newline character after scanf

        if (choice == 3) 
        {
            close(sockfd);
            return 0;
        }

        if (choice != 1 && choice != 2 && choice != 3) 
        {
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

        // Send data to server
        snprintf(buffer, sizeof(buffer), "%d|%s|%s\n", choice, username, password);
        send(sockfd, buffer, strlen(buffer), 0);

        // Receive response from server
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) 
        {
            printf("Mat ket noi voi server!\n");
            close(sockfd);
            return 1;
        }
        buffer[bytes] = '\0';
        printf("%s", buffer);

        if (strstr(buffer, "thanh cong") != NULL) 
        {
            authenticated = 1;
            strcpy(my_username, username); // Lưu lại username
        }
    }

    // Đã xác thực, vào giao diện chat
    printf("=== Ban da vao phong chat ===\n");
    fflush(stdout);
    printf("[You]: ");
    fflush(stdout);

    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, &sockfd);

    while (fgets(buffer, sizeof(buffer), stdin)) 
    {
        if (strncmp(buffer, "@exit@", 6) == 0) 
        {
            printf("Dang thoat khoi cuoc tro chuyen...\n");
            fflush(stdout);
            break;
        }
        send(sockfd, buffer, strlen(buffer), 0);

        // Ghi lịch sử gửi
        char line[1100];
        snprintf(line, sizeof(line), "[You]: %s", buffer);
        save_chat_history(my_username, line);

        printf("[You]: ");
        fflush(stdout);
    }

    close(sockfd);
    return 0;
}