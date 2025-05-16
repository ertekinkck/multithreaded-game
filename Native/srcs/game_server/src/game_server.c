#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <time.h>

#define PORT 6000
#define MAX_FIGHTS 2

struct Player {
    int socket;
    char name[64];
    int health;
};

struct Fight {
    struct Player p1;
    struct Player p2;
};

struct Fight fights[MAX_FIGHTS];
int fight_count = 0;
pthread_mutex_t fight_lock;

void* fight_thread(void* arg) {
    struct Fight* f = (struct Fight*)arg;
    char buf[256];
    int turn = 0;

    srand(42);

    while (f->p1.health > 0 && f->p2.health > 0) {
        struct Player* attacker = (turn % 2 == 0) ? &f->p1 : &f->p2;
        struct Player* defender = (turn % 2 == 0) ? &f->p2 : &f->p1;

        int damage = (rand() % 26) + 5; // 5 to 30
        defender->health -= damage;
        if (defender->health < 0) {
            defender->health = 0;
        }

        snprintf(buf, sizeof(buf), "You damaged %s", defender->name);
        send(attacker->socket, buf, strlen(buf), 0);

        snprintf(buf, sizeof(buf), "You are hurt by %s", attacker->name);
        send(defender->socket, buf, strlen(buf), 0);

        snprintf(buf, sizeof(buf), "%s attacks %s by %d%s", attacker->name, defender->name, damage, damage > 20 ? " (heavy attack)" : "");
        printf("%s\n", buf);

        snprintf(buf, sizeof(buf), "%s health is %d", defender->name, defender->health);
        printf("%s\n", buf);

        float sleep_time = 0.5f + ((float)rand() / RAND_MAX) * 1.0f; // 0.5 to 1.5
        usleep((int)(sleep_time * 1000000));

        turn++;
    }

    if (f->p1.health <= 0) {
        send(f->p1.socket, "you are dead, killed by opponent", 34, 0);
        send(f->p2.socket, "you defeated the player", 24, 0);
    } else {
        send(f->p2.socket, "you are dead, killed by opponent", 34, 0);
        send(f->p1.socket, "you defeated the player", 24, 0);
    }

    close(f->p1.socket);
    close(f->p2.socket);
    return NULL;
}

void* handle_fight(void* arg) {
    int* sockets = (int*)arg;
    pthread_mutex_lock(&fight_lock);
    struct Fight* f = &fights[fight_count++];
    f->p1.socket = sockets[0];
    f->p2.socket = sockets[1];
    f->p1.health = f->p2.health = 100;

    recv(sockets[0], f->p1.name, 64, 0);
    recv(sockets[1], f->p2.name, 64, 0);
    pthread_mutex_unlock(&fight_lock);

    pthread_t tid;
    pthread_create(&tid, NULL, fight_thread, f);
    pthread_detach(tid);

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
    printf("Game server listening on port %d\n", PORT);

    pthread_mutex_init(&fight_lock, NULL);

    while (1) {
        int* sockets = malloc(2 * sizeof(int));
        sockets[0] = accept(server_sock, NULL, NULL);
        sockets[1] = accept(server_sock, NULL, NULL);
        pthread_t tid;
        pthread_create(&tid, NULL, handle_fight, sockets);
        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}