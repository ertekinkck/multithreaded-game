#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <time.h>
#include "thread.h"
#include "semaphore.h"

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
Mutex fight_lock;

void* fight_thread(void* arg) {
	struct Fight* f = (struct Fight*)arg;
	int turn = 0;
	srand(42);

	while (f->p1.health > 0 && f->p2.health > 0) {
		struct Player* attacker = (turn % 2 == 0) ? &f->p1 : &f->p2;
		struct Player* defender = (turn % 2 == 0) ? &f->p2 : &f->p1;

		int damage = (rand() % 26) + 5;
		defender->health -= damage;
		if (defender->health < 0) defender->health = 0;

		dprintf(attacker->socket, "You damaged %s", defender->name);
		dprintf(defender->socket, "You are hurt by %s", attacker->name);
		dprintf(1, "%s damaged %s by %d %s\n", attacker->name, defender->name, damage, damage > 20 ? "(heavy attack)" : "");
		dprintf(1, "%s health is %d\n", defender->name, defender->health);

		float sleep_time = 0.5f + ((float)rand() / RAND_MAX) * 1.0f;
		usleep((int)(sleep_time * 1000000));
		turn++;
	}

	if (f->p1.health <= 0)
	{
		dprintf(f->p1.socket, "you are dead, killed by opponent");
		dprintf(f->p2.socket, "you defeated the player");
	}
	else
	{
		dprintf(f->p2.socket, "you are dead, killed by opponent");
		dprintf(f->p1.socket, "you defeated the player");
	}

	close(f->p1.socket);
	close(f->p2.socket);
	return NULL;
}

void	*handle_fight(void *arg)
{
	int* sockets = (int *)arg;

	mutex_lock(&fight_lock);
	struct Fight* f = &fights[fight_count++];
	f->p1.socket = sockets[0];
	f->p2.socket = sockets[1];
	f->p1.health = 100;
	f->p2.health = 100;
	read(sockets[0], f->p1.name, sizeof(f->p1.name));
	read(sockets[1], f->p2.name, sizeof(f->p2.name));
	mutex_unlock(&fight_lock);

	Thread* t;
	thread_create(&t, fight_thread, f);

	free(sockets);
	return (NULL);
}

int	main(void)
{
	srand(42);

	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(PORT);

	bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
	listen(server_sock, 10);

	dprintf(1, "Game server listening on port %d\n", PORT);

	thread_init();
	mutex_init(&fight_lock);

	while (1)
	{
		int	*sockets = malloc(sizeof(int) * 2);
		sockets[0] = accept(server_sock, NULL, NULL);
		sockets[1] = accept(server_sock, NULL, NULL);

		Thread* t;
		thread_create(&t, handle_fight, sockets);
	}

	close(server_sock);
	mutex_destroy(&fight_lock);
	return 0;
}
