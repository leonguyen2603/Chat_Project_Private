#include <stdio.h> // printf, fprintf
#include <stdlib.h> // malloc
#include <string.h> // strcmp, strncpy
#include <unistd.h> // close, read, write
#include <pthread.h> // pthread_create, pthread_mutex_lock
#include <netinet/in.h> // socket, bind, listen, accept
#include <sqlite3.h> // SQLite
#include <sys/stat.h> // mkdir
#include <errno.h>

#define PORT 8888
#define MAX_CLIENTS 100
#define MAX_USERS 100

int clients[MAX_CLIENTS];
char online_users[MAX_USERS][64];
int online_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
sqlite3 *db;

void save_message(const char *username, const char *content);

// Check if user exists
// Returns 1 if user exists, 0 otherwise
int user_exists(const char *username) 
{
    FILE *f = fopen("users.txt", "r"); // Open users.txt for reading 
    if (!f) return 0; // If file doesn't exist, return 0
    char u[64], p[64]; 
    // Read each line in the format "username|password"
    while (fscanf(f, "%63[^|]|%63[^\n]\n", u, p) == 2) // Read username and password 
    {
        // If the username matches, return 1
        if (strcmp(u, username) == 0) 
        {
            fclose(f); // Close the file
            return 1;
        }
    }
    fclose(f);
    return 0;
}

// Check login credentials
// Returns 1 on success, 0 on failure
int check_login(const char *username, const char *password) 
{
    FILE *f = fopen("users.txt", "r");
    if (!f) return 0;
    char u[64], p[64];
    while (fscanf(f, "%63[^|]|%63[^\n]\n", u, p) == 2) 
    {
        if (strcmp(u, username) == 0 && strcmp(p, password) == 0) 
        {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

// Register a new user
// Returns 1 on success, 0 if user already exists
int register_user(const char *username, const char *password) 
{
    if (user_exists(username)) return 0;
    FILE *f = fopen("users.txt", "a");
    if (!f) return 0;
    fprintf(f, "%s|%s\n", username, password);
    fclose(f);
    return 1;
}

// Check if a user is online
// Returns 1 if online, 0 otherwise
int is_online(const char *username) 
{
    for (int i = 0; i < online_count; ++i) 
    {
        if (strcmp(online_users[i], username) == 0) return 1;
    }
    return 0;
}

// Add a user to the online list
// Does nothing if user is already online or max users reached
void add_online(const char *username) 
{
    if (!is_online(username) && online_count < MAX_USERS) 
    {
        strncpy(online_users[online_count++], username, 63);
    }
}

// Remove a user from the online list
// Does nothing if user is not online
void remove_online(const char *username) 
{
    for (int i = 0; i < online_count; ++i) 
    {
        if (strcmp(online_users[i], username) == 0) 
        {
            for (int j = i; j < online_count - 1; ++j) 
            {
                strcpy(online_users[j], online_users[j + 1]);
            }
            --online_count;
            break;
        }
    }
}

// Broadcast a message to all clients except the sender
// sender_fd is the file descriptor of the client that sent the message
void broadcast(char *msg, int sender_fd) 
{
    // Lock the mutex to protect shared resource
    // Loop through all clients and send the message
    // Skip the sender_fd to avoid sending the message back to the sender
    pthread_mutex_lock(&clients_mutex); 
    for (int i = 0; i < MAX_CLIENTS; ++i) 
    {
        if (clients[i] != 0 && clients[i] != sender_fd) 
        {
            send(clients[i], msg, strlen(msg), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

typedef struct {
    int fd;
    char username[64];
} client_state_t;

client_state_t client_states[MAX_CLIENTS];

// Tạo thư mục nếu chưa có
void ensure_private_folder() {
    struct stat st = {0};
    if (stat("private", &st) == -1) {
        mkdir("private", 0700);
    }
}

// Tạo tên file private cho 2 user (theo thứ tự alpha)
void get_private_filename(const char *user1, const char *user2, char *filename, size_t size) {
    char u1[64], u2[64];
    strncpy(u1, user1, 63); u1[63] = 0;
    strncpy(u2, user2, 63); u2[63] = 0;
    if (strcmp(u1, u2) > 0) { char tmp[64]; strcpy(tmp, u1); strcpy(u1, u2); strcpy(u2, tmp); }
    snprintf(filename, size, "private/%s_%s.txt", u1, u2);
}

// Lưu tin nhắn private vào file
void save_private_message(const char *from, const char *to, const char *msg) {
    ensure_private_folder();
    char filename[256];
    get_private_filename(from, to, filename, sizeof(filename));
    FILE *f = fopen(filename, "a");
    if (f) {
        fprintf(f, "[PRIVATE][%s->%s]: %s", from, to, msg);
        fclose(f);
    }
}

// Thêm user vào private_list file
void add_private_list(const char *user, const char *peer) {
    ensure_private_folder();
    char filename[256];
    snprintf(filename, sizeof(filename), "private/private_list_%s.txt", user);
    // Kiểm tra đã có chưa
    FILE *f = fopen(filename, "r");
    char line[64];
    int found = 0;
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (strcmp(line, peer) == 0) { found = 1; break; }
        }
        fclose(f);
    }
    if (!found) {
        f = fopen(filename, "a");
        if (f) { fprintf(f, "%s\n", peer); fclose(f); }
    }
}

// Gửi private_list cho client
void send_private_list(const char *user, int client_fd) {
    char filename[256];
    snprintf(filename, sizeof(filename), "private/private_list_%s.txt", user);
    FILE *f = fopen(filename, "r");
    if (!f) {
        char *msg = "Chua co lich su chat rieng voi ai!\n";
        send(client_fd, msg, strlen(msg), 0);
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        send(client_fd, line, strlen(line), 0);
    }
    fclose(f);
}

// Gửi lịch sử private cho client
void send_private_history(const char *user, const char *peer, int client_fd) {
    char filename[256];
    get_private_filename(user, peer, filename, sizeof(filename));
    FILE *f = fopen(filename, "r");
    if (!f) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Khong co lich su chat rieng voi %s\n", peer);
        send(client_fd, msg, strlen(msg), 0);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        send(client_fd, line, strlen(line), 0);
    }
    fclose(f);
}

// Tìm fd theo username
int find_fd_by_username(const char *username) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_states[i].fd != 0 && strcmp(client_states[i].username, username) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return client_states[i].fd;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return 0;
}

// Initialize the SQLite database
// Creates the messages table if it doesn't exist
// Returns 1 on success, 0 on failure
int init_db() 
{
    int rc = sqlite3_open("chat.db", &db); // Open the database file
                                           // If the database file doesn't exist, it will be created
    if (rc) 
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    const char *sql = "CREATE TABLE IF NOT EXISTS messages ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "username TEXT,"
                      "content TEXT,"
                      "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
                      ");";
    char *errmsg = NULL;
    rc = sqlite3_exec(db, sql, 0, 0, &errmsg);
    if (rc != SQLITE_OK) 
    {
        fprintf(stderr, "SQL error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return 0;
    }
    return 1;
}

// Save a message to the database
void save_message(const char *username, const char *content) 
{
    const char *sql = "INSERT INTO messages (username, content) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) 
    {
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, content, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void *handle_client(void *arg) 
{
    int client_fd = *(int *)arg;
    free(arg);
    char buffer[1024];
    int authenticated = 0;
    char username[64] = {0};
    int idx = -1;

    while (!authenticated) 
    {
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0); // Receive data from client
        if (bytes <= 0) goto cleanup;
        buffer[bytes] = '\0'; // Null-terminate the received string

        int choice;
        char user[64], pass[64];
        // Parse the input in the format "choice|username|password"
        // choice: 1 for login, 2 for register
        if (sscanf(buffer, "%d|%63[^|]|%63[^\n]", &choice, user, pass) != 3) 
        {
            char *msg = "Sai dinh dang du lieu!\n";
            send(client_fd, msg, strlen(msg), 0);
            continue;
        }

        if (choice == 1) 
        { 
            if (check_login(user, pass)) 
            {
                if (is_online(user)) 
                {
                    char *msg = "Tai khoan nay dang dang nhap o noi khac!\n";
                    send(client_fd, msg, strlen(msg), 0);
                } 
                else 
                {
                    char *msg = "Dang nhap thanh cong!\n";
                    send(client_fd, msg, strlen(msg), 0);
                    authenticated = 1;
                    strncpy(username, user, sizeof(username)-1);
                    add_online(username);
                }
            } 
            else 
            {
                char *msg = "Sai username hoac password!\n";
                send(client_fd, msg, strlen(msg), 0);
            }
        } 
        else if (choice == 2) 
        { 
            if (register_user(user, pass)) 
            {
                char *msg = "Dang ky thanh cong!\n";
                send(client_fd, msg, strlen(msg), 0);
                authenticated = 1;
                strncpy(username, user, sizeof(username)-1); 
            } 
            else 
            {
                char *msg = "Username da ton tai!\n";
                send(client_fd, msg, strlen(msg), 0);
            }
        } 
        // else if (choice == 3) 
        // { 
        //     char *msg = "Cam on da su dung dich vu!\n";
        //     send(client_fd, msg, strlen(msg), 0);
        //     close(client_fd);
        //     return NULL; // Exit the thread
        // }
        else 
        {
            char *msg = "Lua chon khong hop le!\n";
            send(client_fd, msg, strlen(msg), 0);
        }
    }

    // At this point, the user is authenticated
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) 
    {
        if (clients[i] == 0) 
        {
            clients[i] = client_fd;
            idx = i;
            break;
        }
    }
    if (idx >= 0) {
        client_states[idx].fd = client_fd;
        strncpy(client_states[idx].username, username, 63);
    }
    pthread_mutex_unlock(&clients_mutex);

    add_online(username); // Add user to online list

    // Inform other clients that this user has joined the chat room
    snprintf(buffer, sizeof(buffer), "[%s da tham gia phong chat]\n", username);
    broadcast(buffer, client_fd);

    while (1) 
    {
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';

        // Nếu client gửi "@online@", trả về danh sách user online
        if (strcmp(buffer, "@online@\n") == 0 || strcmp(buffer, "@online@") == 0) {
            char online_list[2048] = "Nguoi dang online:\n";
            pthread_mutex_lock(&clients_mutex);
            for (int i = 0; i < online_count; ++i) {
                strcat(online_list, "- ");
                strcat(online_list, online_users[i]);
                strcat(online_list, "\n");
            }
            pthread_mutex_unlock(&clients_mutex);
            send(client_fd, online_list, strlen(online_list), 0);
            continue;
        }

        // @target@ <username>
        if (strncmp(buffer, "@target@", 8) == 0) {
            char target[64], msg[1024];
            if (sscanf(buffer + 8, "%63s %[^\n]", target, msg) == 2) {
                if (!is_online(target)) {
                    char msgbuf[128];
                    snprintf(msgbuf, sizeof(msgbuf), "Nguoi dung %s khong online!\n", target);
                    send(client_fd, msgbuf, strlen(msgbuf), 0);
                    continue;
                }
                if (strcmp(target, username) == 0) {
                    char *msgbuf = "Khong the chat rieng voi chinh ban!\n";
                    send(client_fd, msgbuf, strlen(msgbuf), 0);
                    continue;
                }
                int target_fd = find_fd_by_username(target);
                if (target_fd == 0) {
                    char msgbuf[128];
                    snprintf(msgbuf, sizeof(msgbuf), "Khong gui duoc cho %s!\n", target);
                    send(client_fd, msgbuf, strlen(msgbuf), 0);
                    continue;
                }
                // Lưu lịch sử private cho cả 2 phía
                save_private_message(username, target, msg);
                add_private_list(username, target);
                add_private_list(target, username);

                char msgbuf[1100];
                int prefix_len = snprintf(NULL, 0, "[PRIVATE][%s]: ", username);
                int max_msg_len = sizeof(msgbuf) - prefix_len - 2; // 2 for '\n' and '\0'
                char *msg_to_send = msg;
                char tmp[1024];
                if ((int)strlen(msg) > max_msg_len) {
                    strncpy(tmp, msg, max_msg_len);
                    tmp[max_msg_len] = '\0';
                    msg_to_send = tmp;
                }
                snprintf(msgbuf, sizeof(msgbuf), "[PRIVATE][%s]: %s\n", username, msg_to_send);
                send(target_fd, msgbuf, strlen(msgbuf), 0);
                // KHÔNG gửi lại cho người gửi nữa!
                continue;
            } else {
                char *msgbuf = "Sai cu phap! Dung: @target@<username> <message>\n";
                send(client_fd, msgbuf, strlen(msgbuf), 0);
                continue;
            }
        }

        // Tin nhắn chung
        char msg[1100];
        snprintf(msg, sizeof(msg), "[%s]: %s", username, buffer);
        save_message(username, buffer);
        broadcast(msg, client_fd);
    }

    cleanup:
    if (username[0]) 
    {
        remove_online(username);
        char msg[128];
        snprintf(msg, sizeof(msg), "[%s da roi phong chat]\n", username);
        broadcast(msg, client_fd);
    }
    close(client_fd);
    pthread_mutex_lock(&clients_mutex);
    clients[idx] = 0;
    client_states[idx].fd = 0;
    client_states[idx].username[0] = 0;
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

int main() 
{
    // Initialize the clients array to 0
    if (!init_db()) 
    {
        fprintf(stderr, "Failed to initialize database.\n");
        return 1;
    }
    int server_fd = socket(AF_INET, SOCK_STREAM, 0); // Create a TCP socket
    struct sockaddr_in addr = {0}; // Initialize the address structure
    addr.sin_family = AF_INET; // Set the address family to IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // Bind to any available address
    addr.sin_port = htons(PORT); // Set the port number (convert to network byte order)

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)); // Bind the socket to the address and port
    listen(server_fd, 10); // Listen for incoming connections

    printf("Server started on port %d\n", PORT);

    while (1) 
    {
        int client_fd = accept(server_fd, NULL, NULL); // Accept a new client connection

        int *pclient = malloc(sizeof(int)); // Allocate memory for the client file descriptor
        *pclient = client_fd;
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, pclient);
        pthread_detach(tid);
    }
    close(server_fd);
    sqlite3_close(db); 
    return 0;
}