#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdio.h>
#include "thread.h"
#include "semaphore.h"

#define PORT 5000
#define MAX_PLAYERS 4

int		players[MAX_PLAYERS];
char	names[MAX_PLAYERS][64];
int		player_count = 0;
Mutex	lock;

void	*handle_player(void* arg)
{
	int client_sock = *(int*)arg;
	char name[64] = {0};

	// Receive player name (non-blocking approach)
	int received = 0;
	while (received <= 0)
	{
		received = read(client_sock, name, sizeof(name));
		if (received <= 0)
		{
			thread_yield();
		}
	}

	mutex_lock(&lock);

	int index = player_count;
	players[index] = client_sock;
	strcpy(names[index], name);
	player_count++;

	mutex_unlock(&lock);

	// Wait for all players (non-blocking approach)
	while (player_count < MAX_PLAYERS)
	{
		thread_yield();
	}

	mutex_lock(&lock);
	if (index % 2 == 0)
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "%s,%s", names[index], names[index+1]);
		dprintf(players[index], buf);
		dprintf(players[index+1], buf);
		dprintf(1, "Matchmaker: %s vs %s\n", names[index], names[index+1]);
	}
	mutex_unlock(&lock);

	return (NULL);
}

int	main(void)
{

	srand(42);

	// Initialize thread system
	thread_init();
	mutex_init(&lock);

	// Set up server socket
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(PORT);

	bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
	listen(server_sock, 10);
	dprintf(1, "Matchmaker listening on port %d\n", PORT);

	while (player_count < MAX_PLAYERS)
	{
		int client_sock = accept(server_sock, NULL, NULL);
		
		// Non-blocking accept
		if (client_sock < 0)
		{
			thread_yield();
			continue;
		}

		dprintf(1, "New player connected: %d\n", client_sock);
		
		Thread	*player_thread;
		int err = thread_create(&player_thread, handle_player, &client_sock);
		if (err != 0)
		{
			dprintf(2, "Failed to create thread for player\n");
		}
	}

	// Let threads finish their work
	while (player_count < MAX_PLAYERS)
	{
		thread_yield();
	}

	close(server_sock);
	mutex_destroy(&lock);
	return (0);
}
