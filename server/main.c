#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>

#define PORT 8888
#define MAX_CLIENTS 100
#define MAX_USERS 100

int clients[MAX_CLIENTS];
char online_users[MAX_USERS][64];
int online_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Kiểm tra username đã tồn tại chưa
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

// Kiểm tra đăng nhập
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

// Đăng ký user mới
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

void broadcast(char *msg, int sender_fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] != 0 && clients[i] != sender_fd) {
            send(clients[i], msg, strlen(msg), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    char buffer[1024];
    int authenticated = 0;
    char username[64] = {0};

    // Xác thực
    while (!authenticated) {
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) goto cleanup;
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
                    strncpy(username, user, sizeof(username)-1);
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
                strncpy(username, user, sizeof(username)-1);
            } else {
                char *msg = "Username da ton tai!\n";
                send(client_fd, msg, strlen(msg), 0);
            }
        } else {
            char *msg = "Lua chon khong hop le!\n";
            send(client_fd, msg, strlen(msg), 0);
        }
    }

    add_online(username);

    // Đã xác thực, cho phép chat
    snprintf(buffer, sizeof(buffer), "[%s da tham gia phong chat]\n", username);
    broadcast(buffer, client_fd);

    while (1) {
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';

        char msg[1100];
        snprintf(msg, sizeof(msg), "[%s]: %s", username, buffer);
        broadcast(msg, client_fd);
    }

cleanup:
    if (username[0]) remove_online(username);
    close(client_fd);
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] == client_fd) {
            clients[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    printf("Server started on port %d\n", PORT);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);

        pthread_mutex_lock(&clients_mutex);
        int i;
        for (i = 0; i < MAX_CLIENTS; ++i) {
            if (clients[i] == 0) {
                clients[i] = client_fd;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (i == MAX_CLIENTS) {
            close(client_fd);
            continue;
        }

        int *pclient = malloc(sizeof(int));
        *pclient = client_fd;
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, pclient);
        pthread_detach(tid);
    }
    close(server_fd);
    return 0;
}