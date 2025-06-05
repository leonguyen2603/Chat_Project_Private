#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sqlite3.h>
#include <signal.h>

#define PORT 8888
#define MAX_CLIENTS 100
#define MAX_USERS 100

typedef struct {
    int fd;
    char room_id[64];
    int ready; // Thêm trạng thái ready
} client_info;

client_info clients[MAX_CLIENTS];
char online_users[MAX_USERS][64];
int online_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
sqlite3 *db;

// Prototype
void save_message(const char *room_id, const char *username, const char *content);
void send_last_messages(int client_fd, const char *room_id);
int group_exists(const char *room_id, char *password_out);
int create_group(const char *room_id, const char *password);

// User management
int user_exists(const char *username) {
    FILE *f = fopen("users.txt", "r");
    if (!f) return 0;
    char u[64], p[64];
    while (fscanf(f, "%63[^|]|%63[^\n]\n", u, p) == 2) {
        if (strcmp(u, username) == 0) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

int check_login(const char *username, const char *password) {
    FILE *f = fopen("users.txt", "r");
    if (!f) return 0;
    char u[64], p[64];
    while (fscanf(f, "%63[^|]|%63[^\n]\n", u, p) == 2) {
        if (strcmp(u, username) == 0 && strcmp(p, password) == 0) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

int register_user(const char *username, const char *password) {
    if (user_exists(username)) return 0;
    FILE *f = fopen("users.txt", "a");
    if (!f) return 0;
    fprintf(f, "%s|%s\n", username, password);
    fclose(f);
    return 1;
}

int is_online(const char *username) {
    for (int i = 0; i < online_count; ++i) {
        if (strcmp(online_users[i], username) == 0) return 1;
    }
    return 0;
}

void add_online(const char *username) {
    if (!is_online(username) && online_count < MAX_USERS) {
        strncpy(online_users[online_count++], username, 63);
    }
}

void remove_online(const char *username) {
    for (int i = 0; i < online_count; ++i) {
        if (strcmp(online_users[i], username) == 0) {
            for (int j = i; j < online_count - 1; ++j) {
                strcpy(online_users[j], online_users[j + 1]);
            }
            --online_count;
            break;
        }
    }
}

// Broadcast cho tất cả client trong cùng phòng
void broadcast_room(const char *msg, const char *room_id, int sender_fd) {
    int fds[MAX_CLIENTS];
    int count = 0;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd != 0 && clients[i].fd != sender_fd &&
            strcmp(clients[i].room_id, room_id) == 0 && clients[i].ready == 1) {
            fds[count++] = clients[i].fd;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    for (int i = 0; i < count; ++i) {
        send(fds[i], msg, strlen(msg), 0);
    }
}

// Lưu tin nhắn vào SQLite
void save_message(const char *room_id, const char *username, const char *content) {
    const char *sql = "INSERT INTO messages (room_id, username, content) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, room_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, content, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// Gửi 50 tin nhắn gần nhất của phòng cho client
void send_last_messages(int client_fd, const char *room_id) {
    const char *sql = "SELECT username, content, timestamp FROM messages WHERE room_id = ? ORDER BY id DESC LIMIT 50;";
    sqlite3_stmt *stmt;
    int sent_any = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, room_id, -1, SQLITE_STATIC);
        char msg[1200];
        char *history[50];
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && count < 50) {
            const char *username = (const char *)sqlite3_column_text(stmt, 0);
            const char *content = (const char *)sqlite3_column_text(stmt, 1);
            const char *timestamp = (const char *)sqlite3_column_text(stmt, 2);
            snprintf(msg, sizeof(msg), "[%s][%s]: %s", timestamp, username, content);
            size_t len = strlen(msg);
            if (len == 0 || msg[len-1] != '\n') {
                strncat(msg, "\n", sizeof(msg) - len - 1);
            }
            history[count] = strdup(msg);
            count++;
        }
        for (int i = count - 1; i >= 0; --i) {
            send(client_fd, history[i], strlen(history[i]), 0);
            free(history[i]);
            sent_any = 1;
        }
        sqlite3_finalize(stmt);
    }
    if (!sent_any) {
        const char *empty = "(Khong co lich su tin nhan)\n";
        send(client_fd, empty, strlen(empty), 0);
    }
    // Gửi kết thúc lịch sử
    const char *end = "END_HISTORY\n";
    send(client_fd, end, strlen(end), 0);
}

// Kiểm tra group tồn tại và lấy password
int group_exists(const char *room_id, char *password_out) {
    FILE *f = fopen("groups.txt", "r");
    if (!f) return 0;
    char id[64], pass[64];
    while (fscanf(f, "%63[^|]|%63[^\n]\n", id, pass) == 2) {
        if (strcmp(id, room_id) == 0) {
            if (password_out) strcpy(password_out, pass);
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

// Tạo group mới
int create_group(const char *room_id, const char *password) {
    if (group_exists(room_id, NULL)) return 0;
    FILE *f = fopen("groups.txt", "a");
    if (!f) return 0;
    fprintf(f, "%s|%s\n", room_id, password);
    fclose(f);
    return 1;
}

// Thêm client vào mảng clients[]
void add_client(int client_fd, const char *room_id) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd == 0) {
            clients[i].fd = client_fd;
            strncpy(clients[i].room_id, room_id, 63);
            clients[i].ready = 0; // Chưa sẵn sàng khi mới vào
            printf("Added client_fd: %d to clients[] at index %d\n", client_fd, i);
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void set_client_ready(int client_fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd == client_fd) {
            clients[i].ready = 1;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Xóa client khỏi mảng clients[]
void remove_client(int client_fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd == client_fd) {
            printf("Removing client_fd: %d from clients[] at index %d\n", client_fd, i); // Log
            clients[i].fd = 0;
            clients[i].room_id[0] = '\0';
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Gửi dữ liệu an toàn với kiểm tra lỗi
int safe_send(int client_fd, const char *msg, size_t len) {
    int bytes_sent = send(client_fd, msg, len, 0);
    if (bytes_sent < 0) {
        perror("send");
        return -1;
    }
    return bytes_sent;
}

// Xác thực user
int handle_auth(int client_fd, char *username) {
    char buffer[1024];
    int authenticated = 0;
    while (!authenticated) {
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) return 0;
        buffer[bytes] = '\0';

        int choice;
        char user[64], pass[64];
        if (sscanf(buffer, "%d|%63[^|]|%63[^\n]", &choice, user, pass) != 3) {
            char *msg = "Sai dinh dang du lieu!\n";
            send(client_fd, msg, strlen(msg), 0);
            continue;
        }

        if (choice == 1) { // Đăng nhập
            if (check_login(user, pass)) {
                if (is_online(user)) {
                    char *msg = "Tai khoan nay dang dang nhap o noi khac!\n";
                    send(client_fd, msg, strlen(msg), 0);
                } else {
                    char *msg = "Dang nhap thanh cong!\n";
                    send(client_fd, msg, strlen(msg), 0);
                    authenticated = 1;
                    strncpy(username, user, 63);
                    add_online(username);
                }
            } else {
                char *msg = "Sai username hoac password!\n";
                send(client_fd, msg, strlen(msg), 0);
            }
        } else if (choice == 2) { // Đăng ký
            if (register_user(user, pass)) {
                char *msg = "Dang ky thanh cong!\n";
                send(client_fd, msg, strlen(msg), 0);
                authenticated = 1;
                strncpy(username, user, 63);
            } else {
                char *msg = "Username da ton tai!\n";
                send(client_fd, msg, strlen(msg), 0);
            }
        } else {
            char *msg = "Lua chon khong hop le!\n";
            send(client_fd, msg, strlen(msg), 0);
        }
    }
    return 1;
}

// Chọn phòng chat
void handle_room_selection(int client_fd, char *room_id) {
    char buffer[1024];
    int bytes;
    while (1) {
        char menu[] = "=== MENU PHONG CHAT ===\n1. Tham gia phong chat tong\n2. Tao group chat moi\n3. Hien cac group co san\n0. Quay lai\nChon: ";
        printf("Sending menu to client_fd: %d\n", client_fd);
        if (send(client_fd, menu, strlen(menu), 0) < 0) {
            perror("send menu");
            close(client_fd);
            return;
        }
        printf("Waiting for client_fd: %d to send choice...\n", client_fd);

        // Thêm timeout cho recv()
        struct timeval tv = {30, 0}; // Timeout 30 giây
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client_fd, &readfds);

        int activity = select(client_fd + 1, &readfds, NULL, NULL, &tv);
        if (activity == 0) {
            printf("Timeout: Client %d did not send any data. Nhac nho client nhap lua chon.\n", client_fd); // Log timeout
            char reminder[] = "Vui long nhap lua chon!\n";
            if (send(client_fd, reminder, strlen(reminder), 0) < 0) {
                perror("send reminder");
                close(client_fd);
                return;
            }
            continue; // Tiếp tục vòng lặp để chờ phản hồi
        }
        if (activity < 0) {
            perror("select");
            close(client_fd);
            return;
        }

        bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            if (bytes == 0) {
                printf("Client %d closed connection (room selection).\n", client_fd);
            } else {
                perror("recv");
            }
            close(client_fd);
            return;
        }
        buffer[bytes] = '\0';
        // Loại bỏ mọi ký tự trắng, xuống dòng, carriage return ở cuối
        int len = strlen(buffer);
        while (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r' || buffer[len-1] == ' ')) {
            buffer[--len] = '\0';
        }
        printf("Received choice from client_fd %d: %s\n", client_fd, buffer); // Log

        int room_choice = atoi(buffer);
        if (room_choice == 1) {
            strcpy(room_id, "main_room");
            add_client(client_fd, room_id);
            send_last_messages(client_fd, room_id);
            // Gửi thông báo vào phòng sau END_HISTORY
            char welcome_msg[256];
            snprintf(welcome_msg, sizeof(welcome_msg), "=== Ban da vao phong chat: %s ===\n", room_id);
            printf("Sending welcome message to client_fd %d: %s", client_fd, welcome_msg);
            if (send(client_fd, welcome_msg, strlen(welcome_msg), 0) < 0) {
                perror("send welcome_msg");
                close(client_fd);
                return;
            }
            set_client_ready(client_fd);
            break;
        } else if (room_choice == 2) {
            // Tạo group mới
            if (send(client_fd, "Nhap room_id: ", 14, 0) < 0) {
                perror("send room_id prompt");
                return;
            }
            bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) {
                if (bytes == 0) {
                    printf("Client %d closed connection (room_id).\n", client_fd);
                } else {
                    perror("recv room_id");
                }
                return;
            }
            buffer[bytes] = '\0';
            buffer[strcspn(buffer, "\r\n")] = 0;
            char new_room_id[64];
            strncpy(new_room_id, buffer, 63);
            new_room_id[63] = '\0';

            if (send(client_fd, "Nhap password: ", 15, 0) < 0) {
                perror("send password prompt");
                return;
            }
            bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) {
                if (bytes == 0) {
                    printf("Client %d closed connection (password).\n", client_fd);
                } else {
                    perror("recv password");
                }
                return;
            }
            buffer[bytes] = '\0';
            buffer[strcspn(buffer, "\r\n")] = 0;
            char new_pass[64];
            strncpy(new_pass, buffer, 63);
            new_pass[63] = '\0';

            if (create_group(new_room_id, new_pass)) {
                strcpy(room_id, new_room_id);
                if (send(client_fd, "Tao group thanh cong!\n", 22, 0) < 0) {
                    perror("send group success");
                    return;
                }
                break;
            } else {
                if (send(client_fd, "Room da ton tai!\n", 17, 0) < 0) {
                    perror("send group exists");
                    return;
                }
            }
        } else if (room_choice == 3) {
            // Hiện các group có sẵn
            FILE *f = fopen("groups.txt", "r");
            if (f) {
                char line[128];
                if (send(client_fd, "Danh sach group:\n", 17, 0) < 0) {
                    perror("send group list header");
                    fclose(f);
                    return;
                }
                while (fgets(line, sizeof(line), f)) {
                    char id[64], pass[64];
                    if (sscanf(line, "%63[^|]|%63[^\n]", id, pass) == 2) {
                        strcat(id, "\n");
                        if (send(client_fd, id, strlen(id), 0) < 0) {
                            perror("send group id");
                            fclose(f);
                            return;
                        }
                    }
                }
                fclose(f);
            }
            if (send(client_fd, "Nhap room_id de join hoac 0 de quay lai: ", 41, 0) < 0) {
                perror("send join prompt");
                return;
            }
        } else if (room_choice == 0) {
            continue;
        } else {
            printf("Invalid choice from client_fd %d: %s\n", client_fd, buffer); // Log
            if (send(client_fd, "Lua chon khong hop le!\n", 23, 0) < 0) {
                perror("send invalid choice");
                close(client_fd);
                return;
            }
        }
    }
    //add_client(client_fd, room_id); //Chuyển lên trên
}

// Chat trong phòng
void handle_chat(int client_fd, const char *room_id, const char *username) {
    char buffer[1024];
    // Không gửi lại lịch sử hoặc thông báo vào phòng ở đây nữa

    // Broadcast cho các client khác (không gửi cho chính client vừa vào)
    snprintf(buffer, sizeof(buffer), "[%s da tham gia phong chat]\n", username);
    broadcast_room(buffer, room_id, client_fd);

    while (1) {
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes == 0) {
            // Client đóng kết nối, thoát khỏi vòng lặp
            printf("Client %d closed connection.\n", client_fd);
            break;
        }
        if (bytes < 0) {
            perror("recv");
            break;
        }
        buffer[bytes] = '\0';

        // Bỏ qua nếu client gửi "1" (hoặc chỉ là số, có thể kiểm tra thêm nếu muốn)
        char *trim = buffer;
        while (*trim == ' ' || *trim == '\n') trim++;
        if (strcmp(trim, "1") == 0 || strlen(trim) == 0) continue;

        char msg[1100];
        snprintf(msg, sizeof(msg), "[%s]: %s", username, buffer);

        save_message(room_id, username, buffer);
        broadcast_room(msg, room_id, client_fd);
    }
}

// Xử lý client
void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    printf("Thread started for client_fd: %d\n", client_fd);
    free(arg);
    char username[64] = {0};
    char room_id[64] = "";

    if (!handle_auth(client_fd, username)) goto cleanup;

    handle_room_selection(client_fd, room_id);
	//add_client(client_fd, room_id); //Chuyển lên trên

    handle_chat(client_fd, room_id, username);

cleanup:
    if (username[0]) {
        remove_online(username);
        char msg[128];
        snprintf(msg, sizeof(msg), "[%s da roi phong chat]\n", username);
        broadcast_room(msg, room_id, client_fd);
    }
    close(client_fd);
    remove_client(client_fd); // Đảm bảo xóa client khỏi danh sách
    return NULL;
}

// Khởi tạo database
int init_db() {
    int rc = sqlite3_open("chat.db", &db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    const char *sql = "CREATE TABLE IF NOT EXISTS messages ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "room_id TEXT,"
                      "username TEXT,"
                      "content TEXT,"
                      "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
                      ");";
    char *errmsg = NULL;
    rc = sqlite3_exec(db, sql, 0, 0, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return 0;
    }
    return 1;
}

int main() {
    signal(SIGPIPE, SIG_IGN); // Bỏ qua tín hiệu SIGPIPE
    if (!init_db()) {
        fprintf(stderr, "Failed to initialize database.\n");
        return 1;
    }
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    int opt = 1;
    // Cho phép reuse address để tránh lỗi "Address already in use"
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Server started on port %d\n", PORT);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        printf("Accepted client_fd: %d\n", client_fd);
        int *pclient = malloc(sizeof(int));
        if (!pclient) {
            perror("malloc");
            close(client_fd);
            continue;
        }
        *pclient = client_fd;
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, pclient);
        pthread_detach(tid);
    }
    close(server_fd);
    sqlite3_close(db);
    return 0;
}