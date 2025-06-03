#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>

#define PORT 8888
#define MAX_CLIENTS 100

int clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    char buffer[1024];
    while (1) {
        int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        broadcast(buffer, client_fd);
    }
    close(client_fd);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] == client_fd) {
            clients[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    free(arg);
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