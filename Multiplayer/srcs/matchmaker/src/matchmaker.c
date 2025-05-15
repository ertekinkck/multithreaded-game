#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>

#define PORT 5000
#define MAX_PLAYERS 4

int players[MAX_PLAYERS];
char names[MAX_PLAYERS][64];
int player_count = 0;
pthread_mutex_t lock;

void* handle_player(void* arg) {
    int client_sock = *(int*)arg;
    char name[64] = {0};
    recv(client_sock, name, sizeof(name), 0);
    pthread_mutex_lock(&lock);
    int index = player_count;
    players[index] = client_sock;
    strcpy(names[index], name);
    player_count++;
    pthread_mutex_unlock(&lock);

    while (player_count < MAX_PLAYERS) sleep(1);

    pthread_mutex_lock(&lock);
    if (index % 2 == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s,%s", names[index], names[index+1]);
        send(players[index], buf, strlen(buf), 0);
        send(players[index+1], buf, strlen(buf), 0);
        printf("Matchmaker: %s vs %s\n", names[index], names[index+1]);
    }
    pthread_mutex_unlock(&lock);

    return NULL;
}

int main() {
    setbuf(stdout, NULL);
    srand(42);
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_sock, 10);
    printf("Matchmaker listening on port %d\n", PORT);

    pthread_mutex_init(&lock, NULL);

    while (player_count < MAX_PLAYERS) {
        int* client_sock = malloc(sizeof(int));
        *client_sock = accept(server_sock, NULL, NULL);
        pthread_t tid;
        pthread_create(&tid, NULL, handle_player, client_sock);
        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}