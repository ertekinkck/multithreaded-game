#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <time.h>
#include <signal.h>
#include <sys/select.h>
#include "thread.h"
#include "semaphore.h"

#define PORT 6000
#define MAX_FIGHTS 10  // Increased to handle more fights over time

// Priority definitions (matching those in thread.c)
#define PRIORITY_HIGH 0
#define PRIORITY_LOW  1

// Structure to pass priority along with arguments (matching the one in thread.c)
typedef struct {
    int desired_priority;
    void* actual_arg;
} ThreadCreationArgs;

struct Player {
	int socket;
	char name[64];
	int health;
};

struct Fight {
	struct Player p1;
	struct Player p2;
	volatile int completed; // Flag to indicate if fight is completed
};

struct Fight fights[MAX_FIGHTS];
int fight_count = 0;
Mutex fight_lock;

// Array to keep track of all fight threads
Thread* fight_threads[MAX_FIGHTS];
volatile int running = 1; // Flag to control server shutdown

// Handle termination signals
void handle_signal(int sig) {
    dprintf(1, "Game server: Received signal %d, initiating graceful shutdown...\n", sig);
    running = 0;
}

void* fight_thread(void* arg) {
	struct Fight* f = (struct Fight*)arg;
	int turn = 0;
	srand(42);

	// Log that the fight started
	dprintf(1, "Fight started between %s and %s!\n", f->p1.name, f->p2.name);

	while (f->p1.health > 0 && f->p2.health > 0 && running) {
		struct Player* attacker = (turn % 2 == 0) ? &f->p1 : &f->p2;
		struct Player* defender = (turn % 2 == 0) ? &f->p2 : &f->p1;

		int damage = (rand() % 26) + 5;
		defender->health -= damage;
		if (defender->health < 0) defender->health = 0;

		dprintf(attacker->socket, "%s: You damaged %s\n", attacker->name, defender->name);
		dprintf(defender->socket, "%s: You are hurt by %s\n", defender->name, attacker->name);
		dprintf(1, "%s damaged %s by %d %s\n", attacker->name, defender->name, damage, damage > 20 ? "(heavy attack)" : "");
		dprintf(1, "%s health is %d\n", defender->name, defender->health);

		float sleep_time = 0.5f + ((float)rand() / RAND_MAX) * 1.0f;
		usleep((int)(sleep_time * 1000000));
		turn++;
	}

	// If shutdown was triggered, end fight early with a message
	if (!running) {
		dprintf(1, "Fight between %s and %s interrupted due to server shutdown\n", f->p1.name, f->p2.name);
		dprintf(f->p1.socket, "Game server shutting down. Fight ended early.\n");
		dprintf(f->p2.socket, "Game server shutting down. Fight ended early.\n");
	} else if (f->p1.health <= 0) {
		dprintf(1, "Fight ended: %s defeated %s\n", f->p2.name, f->p1.name);
		dprintf(f->p1.socket, "%s: you are dead, killed by opponent\n", f->p1.name);
		dprintf(f->p2.socket, "%s: you defeated the player\n", f->p2.name);
	} else {
		dprintf(1, "Fight ended: %s defeated %s\n", f->p1.name, f->p2.name);
		dprintf(f->p2.socket, "%s: you are dead, killed by opponent\n", f->p2.name);
		dprintf(f->p1.socket, "%s: you defeated the player\n", f->p1.name);
	}

	// Close player sockets
	close(f->p1.socket);
	close(f->p2.socket);
	
	// Mark fight as completed
	mutex_lock(&fight_lock);
	f->completed = 1;
	mutex_unlock(&fight_lock);
	
	dprintf(1, "Fight thread for %s vs %s completed and exiting.\n", f->p1.name, f->p2.name);
	return NULL;
}

void *handle_fight(void *arg)
{
	int* sockets = (int *)arg;

	mutex_lock(&fight_lock);
	struct Fight* f = &fights[fight_count];
	
	// Initialize fight structure
	f->p1.socket = sockets[0];
	f->p2.socket = sockets[1];
	f->p1.health = 100;
	f->p2.health = 100;
	f->completed = 0;  // Initialize completion status
	
	// Read player names
	read(sockets[0], f->p1.name, sizeof(f->p1.name) - 1);
	f->p1.name[sizeof(f->p1.name) - 1] = '\0';  // Ensure null termination
	read(sockets[1], f->p2.name, sizeof(f->p2.name) - 1);
	f->p2.name[sizeof(f->p2.name) - 1] = '\0';  // Ensure null termination
	
	// Increment fight counter
	int current_fight_index = fight_count++;
	mutex_unlock(&fight_lock);

	// Create thread creation arguments with HIGH priority for fight threads
	ThreadCreationArgs *tc_args = malloc(sizeof(ThreadCreationArgs));
	if (tc_args == NULL) {
		perror("Failed to allocate memory for ThreadCreationArgs");
		free(sockets);
		return NULL;
	}
	
	tc_args->desired_priority = PRIORITY_HIGH;
	tc_args->actual_arg = f;

	// Create fight thread with high priority
	Thread* t;
	int err = thread_create(&t, fight_thread, tc_args);
	if (err != 0) {
		perror("Failed to create fight thread");
		free(tc_args);
		mutex_lock(&fight_lock);
		fight_count--; // Decrement fight counter on failure
		mutex_unlock(&fight_lock);
	} else {
		// Store thread in our tracking array
		mutex_lock(&fight_lock);
		fight_threads[current_fight_index] = t;
		mutex_unlock(&fight_lock);
		free(tc_args); // tc_args is not needed anymore
	}

	free(sockets);
	return (NULL);
}

// Check if all fights have been completed
int all_fights_completed() {
	mutex_lock(&fight_lock);
	for (int i = 0; i < fight_count; i++) {
		if (!fights[i].completed) {
			mutex_unlock(&fight_lock);
			return 0; // Found at least one incomplete fight
		}
	}
	mutex_unlock(&fight_lock);
	return 1; // All fights are completed
}

int	main(void)
{
	srand(42);

	// Set up signal handlers for graceful termination
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		perror("Game server: Socket creation failed");
		return 1;
	}

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(PORT);

	// Allow socket to be reused immediately after closing
	int yes = 1;
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		perror("Game server: setsockopt failed");
		close(server_sock);
		return 1;
	}

	if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("Game server: Bind failed");
		close(server_sock);
		return 1;
	}

	if (listen(server_sock, MAX_FIGHTS * 2) < 0) {
		perror("Game server: Listen failed");
		close(server_sock);
		return 1;
	}

	dprintf(1, "Game server listening on port %d\n", PORT);

	thread_init();
	mutex_init(&fight_lock);

	// Initialize fight threads array
	for (int i = 0; i < MAX_FIGHTS; i++) {
		fight_threads[i] = NULL;
	}
	
	// Keep accepting connections and creating handle_fight threads
	// until a termination signal is received or MAX_FIGHTS is reached
	while (running && fight_count < MAX_FIGHTS)
	{
		// Use select to enable checking for shutdown signal while waiting for connections
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(server_sock, &readfds);
		
		struct timeval timeout;
		timeout.tv_sec = 1;  // Check for shutdown signal every second
		timeout.tv_usec = 0;
		
		int ready = select(server_sock + 1, &readfds, NULL, NULL, &timeout);
		
		if (!running) break;  // Check if shutdown was requested
		
		if (ready <= 0) continue;  // Timeout or error, continue and check again
		
		// Ready to accept new connections
		int	*sockets = malloc(sizeof(int) * 2);
		if (sockets == NULL) {
			perror("Game server: Failed to allocate memory for sockets");
			continue;
		}
		
		// Accept player connections with a timeout
		sockets[0] = accept(server_sock, NULL, NULL);
		if (sockets[0] < 0) {
			perror("Game server: Accept failed for first player");
			free(sockets);
			continue;
		}
		
		dprintf(1, "Game server: First player connected (socket %d)\n", sockets[0]);
		
		// Wait for the second player with a timeout
		fd_set readfds2;
		FD_ZERO(&readfds2);
		FD_SET(server_sock, &readfds2);
		
		timeout.tv_sec = 10;  // Wait up to 10 seconds for second player
		timeout.tv_usec = 0;
		
		ready = select(server_sock + 1, &readfds2, NULL, NULL, &timeout);
		
		if (!running || ready <= 0) {
			// Shutdown requested or timeout waiting for second player
			dprintf(1, "Game server: Timeout waiting for second player or shutdown requested\n");
			dprintf(sockets[0], "Couldn't find a match for you. Please try again later.\n");
			close(sockets[0]);
			free(sockets);
			continue;
		}
		
		sockets[1] = accept(server_sock, NULL, NULL);
		if (sockets[1] < 0) {
			perror("Game server: Accept failed for second player");
			close(sockets[0]);
			free(sockets);
			continue;
		}
		
		dprintf(1, "Game server: Second player connected (socket %d)\n", sockets[1]);
		
		// Create thread creation arguments with LOW priority for handle_fight
		ThreadCreationArgs *tc_args = malloc(sizeof(ThreadCreationArgs));
		if (tc_args == NULL) {
			perror("Game server: Failed to allocate memory for ThreadCreationArgs");
			close(sockets[0]);
			close(sockets[1]);
			free(sockets);
			continue;
		}
		
		tc_args->desired_priority = PRIORITY_LOW;
		tc_args->actual_arg = sockets;

		Thread* t;
		int err = thread_create(&t, handle_fight, tc_args);
		if (err != 0) {
			perror("Game server: Failed to create handle_fight thread");
			free(sockets);
			free(tc_args);
		} else {
			free(tc_args);
			thread_join(t);  // Join immediately as handle_fight creates the actual fight thread
		}
	}

	// No longer accepting new connections
	dprintf(1, "Game server: No longer accepting new connections, waiting for existing fights to complete...\n");
	
	// Wait for all fights to complete
	while (!all_fights_completed() && running) {
		sleep(1);
		dprintf(1, "Game server: Waiting for fights to complete. Active fights: %d\n", fight_count);
	}
	
	// Join all fight threads
	dprintf(1, "Game server: All fights completed or shutdown requested. Cleaning up...\n");
	for (int i = 0; i < fight_count; i++) {
		if (fight_threads[i] != NULL) {
			thread_join(fight_threads[i]);
			dprintf(1, "Game server: Joined fight thread %d\n", i);
		}
	}

	dprintf(1, "Game server: All fight threads joined. Shutting down.\n");
	close(server_sock);
	mutex_destroy(&fight_lock);
	return 0;
}
