#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define PORT 8888
#define SERVER_IP "127.0.0.1"

char my_username[64] = "";

// Prototype
void save_chat_history(const char *username, const char *msg);
void save_private_history(const char *user1, const char *user2, const char *msg);
void ensure_private_folder();
void get_private_filename(const char *user1, const char *user2, char *filename, size_t size);

void *recv_thread(void *arg) 
{
    int sockfd = *(int *)arg;
    char buffer[2048];
    while (1) 
    {
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0'; // Null-terminate the received string

        printf("\r\33[2K"); // Clear the current line

        // Hiển thị tin nhắn private
        if (strncmp(buffer, "[PRIVATE][", 10) == 0) 
        {
            // In nguyên nội dung tin nhắn private từ server, không thêm bất kỳ tiền tố nào nữa
            printf("%s", buffer);
            save_private_history(my_username, "", buffer);
            printf("[You]: ");
            fflush(stdout);
            continue;
        }

        // Nếu là thông báo chuyển chế độ
        if (strstr(buffer, "====PRIVATE====") != NULL) {
            printf("%s", buffer);
            printf("[You]: ");
            fflush(stdout);
            continue;
        }
        if (strstr(buffer, "====ALL====") != NULL) {
            printf("%s", buffer);
            printf("[You]: ");
            fflush(stdout);
            continue;
        }

        // Mặc định: tin nhắn all
        printf("%s", buffer);
        save_chat_history(my_username, buffer);
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

void ensure_private_folder() {
    struct stat st = {0};
    if (stat("private", &st) == -1) {
        mkdir("private", 0700);
    }
}

void get_private_filename(const char *user1, const char *user2, char *filename, size_t size) {
    char u1[64], u2[64];
    strncpy(u1, user1, 63); u1[63] = 0;
    strncpy(u2, user2, 63); u2[63] = 0;
    if (strcmp(u1, u2) > 0) { char tmp[64]; strcpy(tmp, u1); strcpy(u1, u2); strcpy(u2, tmp); }
    snprintf(filename, size, "private/%s_%s.txt", u1, u2);
}

// Save private chat history to a file
// The file is named "<user1>_<user2>.txt" in the "private" folder
void save_private_history(const char *user1, const char *user2, const char *msg) {
    ensure_private_folder();
    char filename[256];
    get_private_filename(user1, user2, filename, sizeof(filename));
    FILE *f = fopen(filename, "a");
    if (f) {
        fprintf(f, "%s", msg);
        fclose(f);
    }
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

    // Thêm kiểm tra kết nối lại nếu bị từ chối
    int retry = 5;
    while (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0 && retry-- > 0) 
    {
        perror("connect");
        printf("Dang thu ket noi lai sau 1 giay...\n");
        sleep(1);
    }
    if (retry < 0) 
    {
        printf("Khong the ket noi den server. Hay dam bao server dang chay!\n");
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
        // Gửi private nếu bắt đầu bằng @target@<username> 
        if (strncmp(buffer, "@target@", 8) == 0) {
            // Định dạng: @target@<username> <message>
            char target[64], msg[1024];
            if (sscanf(buffer + 8, "%63s %[^\n]", target, msg) == 2) {
                if (strcmp(target, my_username) == 0) {
                    printf("Khong the chat rieng voi chinh ban!\n[You]: ");
                    fflush(stdout);
                    continue;
                }
                // Gửi nguyên chuỗi lên server
                send(sockfd, buffer, strlen(buffer), 0);
                save_private_history(my_username, target, msg);
                printf("[You]: ");
                fflush(stdout);
                continue;
            } else {
                printf("Sai cu phap! Dung: @target@<username> <message>\n[You]: ");
                fflush(stdout);
                continue;
            }
        }
        // Lệnh xem danh sách private
        if (strcmp(buffer, "@private_list@\n") == 0 || strcmp(buffer, "@private_list@") == 0) {
            send(sockfd, buffer, strlen(buffer), 0);
            continue;
        }
        // Lệnh xem lịch sử private
        if (strncmp(buffer, "@private_history@ ", 18) == 0) {
            send(sockfd, buffer, strlen(buffer), 0);
            continue;
        }
        // Lệnh online
        if (strcmp(buffer, "@online@\n") == 0 || strcmp(buffer, "@online@") == 0) {
            send(sockfd, buffer, strlen(buffer), 0);
            continue;
        }
        // Thoát
        if (strncmp(buffer, "@exit@", 6) == 0) 
        {
            printf("Dang thoat khoi cuoc tro chuyen...\n");
            fflush(stdout);
            break;
        }

        // Gửi tin nhắn chung
        send(sockfd, buffer, strlen(buffer), 0);
        char line[1100];
        snprintf(line, sizeof(line), "[You]: %s", buffer);
        save_chat_history(my_username, line);
        printf("[You]: ");
        fflush(stdout);
    }

    close(sockfd);
    return 0;
}