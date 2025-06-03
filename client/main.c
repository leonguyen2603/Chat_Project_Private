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
        printf("%s", buffer);
        fflush(stdout);
    }
    return NULL;
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

    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, &sockfd);

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), stdin)) {
        send(sockfd, buffer, strlen(buffer), 0);
    }

    close(sockfd);
    return 0;
}