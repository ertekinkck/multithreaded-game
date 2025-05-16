#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdio.h>
#include <poll.h>
#include "thread.h"
#include "semaphore.h"

#define PORT 5000
#define MAX_PLAYERS 4

// Priority definitions (matching those in thread.c)
#define PRIORITY_HIGH 0
#define PRIORITY_LOW  1

// Structure to pass priority along with arguments (matching the one in thread.c)
typedef struct {
    int desired_priority;
    void* actual_arg;
} ThreadCreationArgs;

int		players[MAX_PLAYERS];
char	names[MAX_PLAYERS][64];
// player_count is now primarily managed and read by handle_player threads under mutex.
// Main will use its own counter for accepting connections.
volatile int player_count = 0;
// Track if player sockets have been closed by players
volatile int player_disconnected[MAX_PLAYERS] = {0};
Mutex	lock;

void	*handle_player(void* arg)
{
	int client_sock = *(int*)arg; // This is client_sock_ptr
	char name_buf[64] = {0}; // Local buffer for player name

	// Receive player name
	int received_bytes = 0;
	// It's better to ensure null termination and handle potential partial reads or errors.
	// For simplicity, assuming read gets the full name or enough.
	// A more robust solution would loop or use a helper for network reads.
	received_bytes = read(client_sock, name_buf, sizeof(name_buf) - 1);
	if (received_bytes > 0) {
		name_buf[received_bytes] = '\0'; // Ensure null termination
	} else {
		// Handle error or short read, perhaps close socket and exit thread
		dprintf(2, "Matchmaker: Failed to read name for socket %d\n", client_sock);
		close(client_sock);
		free(arg); // Free client_sock_ptr
		return NULL;
	}

	mutex_lock(&lock);
	int current_player_idx = player_count;
	if (current_player_idx < MAX_PLAYERS) {
		players[current_player_idx] = client_sock;
		strncpy(names[current_player_idx], name_buf, sizeof(names[current_player_idx]) - 1);
		names[current_player_idx][sizeof(names[current_player_idx]) - 1] = '\0'; // Ensure null termination
		player_count++;
		dprintf(1, "Matchmaker: Player %s (index %d) registered. Total players: %d\n", name_buf, current_player_idx, player_count);
	} else {
		// Should not happen if main loop correctly limits to MAX_PLAYERS
		mutex_unlock(&lock);
		dprintf(2, "Matchmaker: Error, player_count exceeded MAX_PLAYERS for %s\n", name_buf);
		close(client_sock);
		free(arg); // Free client_sock_ptr
		return NULL;
	}
	mutex_unlock(&lock);

	// Wait for all players to connect
	// This check needs to be safe. player_count is volatile.
	while (1) {
		mutex_lock(&lock);
		int current_total_players = player_count;
		mutex_unlock(&lock);
		if (current_total_players >= MAX_PLAYERS) {
			break;
		}
		thread_yield(); // Yield to other threads
	}

	// Perform matchmaking logic
	// This logic assumes players are matched in pairs based on their connection order (index 0 with 1, 2 with 3)
	// The original logic was `if (index % 2 == 0)`.
	// current_player_idx is the index of THIS player.
	mutex_lock(&lock);
	if (current_player_idx % 2 == 0 && current_player_idx + 1 < MAX_PLAYERS) {
		// Ensure the paired player's name is available.
		// Names are filled when players connect. By this point, all should be connected.
		char match_info_buf[128];
		snprintf(match_info_buf, sizeof(match_info_buf), "%s,%s", names[current_player_idx], names[current_player_idx + 1]);
		
		// Send match info to both players involved in this match
		if (players[current_player_idx] != -1) { // Check if socket is still valid
			write(players[current_player_idx], match_info_buf, strlen(match_info_buf));
		}
		if (players[current_player_idx + 1] != -1) { // Check if socket is still valid
			write(players[current_player_idx + 1], match_info_buf, strlen(match_info_buf));
		}
		dprintf(1, "Matchmaker: Match found and notified: %s vs %s\n", names[current_player_idx], names[current_player_idx + 1]);
	}
	mutex_unlock(&lock);

	// The socket ownership is now passed to the main thread
	// which will monitor the socket until it's closed by the player
	free(arg); // Free the int* (client_sock_ptr)
	return (NULL);
}

// Thread function to monitor one player's connection
void* monitor_player_thread(void* arg)
{
	int player_idx = *(int*)arg;
	int socket_fd = players[player_idx];
	char buffer[8]; // Small buffer just to check if socket is still open
	
	dprintf(1, "Matchmaker: Starting to monitor player %s (index %d)\n", names[player_idx], player_idx);
	
	// Continue reading from socket until it's closed by the player
	// This happens when the player finishes the game or disconnects
	while (1) {
		// Use poll to check if socket is still open without blocking
		struct pollfd pfd;
		pfd.fd = socket_fd;
		pfd.events = POLLIN;
		
		int poll_result = poll(&pfd, 1, 1000); // 1 second timeout
		
		if (poll_result < 0) {
			// Error in polling
			dprintf(2, "Matchmaker: Poll error for player %s (index %d)\n", names[player_idx], player_idx);
			break;
		} else if (poll_result > 0) {
			// Socket has data available or has been closed
			int bytes_read = read(socket_fd, buffer, sizeof(buffer));
			if (bytes_read <= 0) {
				// Socket closed by peer or error
				dprintf(1, "Matchmaker: Player %s (index %d) disconnected\n", names[player_idx], player_idx);
				break;
			}
			// Otherwise, we received some unexpected data, just ignore it
		}
		// If poll times out, just continue looping
	}
	
	// Mark this player as disconnected and close the socket
	mutex_lock(&lock);
	player_disconnected[player_idx] = 1;
	close(socket_fd);
	players[player_idx] = -1; // Mark socket as invalid
	mutex_unlock(&lock);
	
	free(arg);
	return NULL;
}

// Check if all players have disconnected
int all_players_disconnected()
{
	mutex_lock(&lock);
	int result = 1; // Assume all disconnected
	for (int i = 0; i < player_count; i++) {
		if (!player_disconnected[i]) {
			result = 0; // At least one player still connected
			break;
		}
	}
	mutex_unlock(&lock);
	return result;
}

int	main(void)
{
	srand(42); // Use time for a different seed

	thread_init();
	mutex_init(&lock);

	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		perror("Matchmaker: Socket creation failed");
		return 1;
	}

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(PORT);

	if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("Matchmaker: Bind failed");
		close(server_sock);
		return 1;
	}

	if (listen(server_sock, MAX_PLAYERS) < 0) { // Listen backlog can be MAX_PLAYERS
		perror("Matchmaker: Listen failed");
		close(server_sock);
		return 1;
	}
	dprintf(1, "Matchmaker listening on port %d\n", PORT);

	Thread *player_threads[MAX_PLAYERS];
	for (int i = 0; i < MAX_PLAYERS; i++) {
		player_threads[i] = NULL; // Initialize to NULL
	}

	for (int i = 0; i < MAX_PLAYERS; i++) {
		dprintf(1, "Matchmaker: Waiting to accept player %d/%d...\n", i + 1, MAX_PLAYERS);
		int client_sock_val = accept(server_sock, NULL, NULL);
		if (client_sock_val < 0) {
			perror("Matchmaker: Accept failed");
			// Consider how to handle this; for now, we might not get all players
			// and the join logic below will handle NULL threads.
			// Or, could retry or exit. For simplicity, continue and try to get other players.
			// If we can't accept all players, the system won't work as intended.
			// Let's make it an error and break, then cleanup.
			dprintf(2, "Matchmaker: Critical accept error for player %d. Shutting down.\n", i + 1);
			// We need to join any threads created so far before exiting.
			for (int j = 0; j < i; j++) { // Join threads created before the error
				if (player_threads[j] != NULL) {
					thread_join(player_threads[j]);
				}
			}
			close(server_sock);
			mutex_destroy(&lock);
			return 1; // Exit if accept fails critically
		}

		dprintf(1, "Matchmaker: Accepted player %d (socket %d)\n", i + 1, client_sock_val);

		int *client_sock_ptr = malloc(sizeof(int));
		if (!client_sock_ptr) {
			perror("Matchmaker: Failed to malloc for client_sock_ptr");
			close(client_sock_val); // Close accepted socket
			// Join previously created threads and exit
			for (int j = 0; j < i; j++) { if (player_threads[j] != NULL) thread_join(player_threads[j]); }
			close(server_sock); mutex_destroy(&lock); return 1;
		}
		*client_sock_ptr = client_sock_val;

		ThreadCreationArgs *tc_args = malloc(sizeof(ThreadCreationArgs));
		if (!tc_args) {
			perror("Matchmaker: Failed to malloc for tc_args");
			free(client_sock_ptr);
			close(client_sock_val);
			for (int j = 0; j < i; j++) { if (player_threads[j] != NULL) thread_join(player_threads[j]); }
			close(server_sock); mutex_destroy(&lock); return 1;
		}
		tc_args->actual_arg = client_sock_ptr;

		// Assign priorities based on connection order (i)
		// Player4 (i=0) and Player2 (i=2) get high priority
		if (i == 0 || i == 2) {
			tc_args->desired_priority = PRIORITY_HIGH;
			dprintf(1, "Matchmaker: Assigning HIGH priority to thread for player index %d\n", i);
		} else {
			tc_args->desired_priority = PRIORITY_LOW;
			dprintf(1, "Matchmaker: Assigning LOW priority to thread for player index %d\n", i);
		}
		
		int err = thread_create(&player_threads[i], handle_player, tc_args);
		if (err != 0) {
			dprintf(2, "Matchmaker: Failed to create thread for player index %d\n", i);
			free(client_sock_ptr); // client_sock_ptr was for the thread
			free(tc_args);         // tc_args was for the thread
			close(client_sock_val);// Close the socket as the thread didn't take ownership
			// No thread to join for this iteration, player_threads[i] remains NULL
			// Potentially try to accept this player again or abort.
			// For robustness, let's try to fill all slots or fail gracefully.
			// If a thread fails to create, it's a significant issue.
			// For now, it means player_threads[i] will be NULL and not joined.
            // But the application may not function correctly.
		} else {
			// tc_args is consumed by thread_create (its contents are copied or used).
			// The memory for tc_args itself should be freed by the caller if thread_create doesn't take ownership of it.
			// Based on typical usage, tc_args is passed by pointer and its contents used.
			// The `arg` in thread_create is freed by `handle_player`.
			// The `tc_args` struct itself, if `thread_create` only *reads* from it, should be freed here.
			// Let's assume thread_create doesn't free `tc_args` but `handle_player` gets `tc_args->actual_arg`.
			// The previous `free(tc_args)` in main after `thread_create` call was correct.
			free(tc_args); 
		}
	}

	dprintf(1, "Matchmaker: All %d player connection attempts processed. Waiting for matchmaking threads to complete...\n", MAX_PLAYERS);
	for (int i = 0; i < MAX_PLAYERS; i++) {
		if (player_threads[i] != NULL) {
			thread_join(player_threads[i]);
			dprintf(1, "Matchmaker: Thread for player index %d joined.\n", i);
		} else {
			dprintf(1, "Matchmaker: No thread to join for player index %d (was not created or failed).\n", i);
		}
	}
	
	// All players are now connected and matched
	// Create monitoring threads to wait for each player to disconnect
	Thread *monitor_threads[MAX_PLAYERS];
	for (int i = 0; i < MAX_PLAYERS; i++) {
		int *idx_ptr = malloc(sizeof(int));
		if (!idx_ptr) {
			perror("Matchmaker: Failed to malloc for monitor thread arg");
			continue;
		}
		*idx_ptr = i;
		
		// Create a thread to monitor this player
		int err = thread_create(&monitor_threads[i], monitor_player_thread, idx_ptr);
		if (err != 0) {
			dprintf(2, "Matchmaker: Failed to create monitor thread for player %d\n", i);
			free(idx_ptr);
			monitor_threads[i] = NULL;
		}
	}
	
	// Wait until all players have disconnected
	dprintf(1, "Matchmaker: Waiting for all players to finish their games...\n");
	while (!all_players_disconnected()) {
		sleep(1); // Check every second
	}
	
	// Join all monitor threads
	for (int i = 0; i < MAX_PLAYERS; i++) {
		if (monitor_threads[i] != NULL) {
			thread_join(monitor_threads[i]);
			dprintf(1, "Matchmaker: Monitor thread for player %d joined.\n", i);
		}
	}

	dprintf(1, "Matchmaker: All players have finished their games. Closing server socket and exiting.\n");
	close(server_sock);
	mutex_destroy(&lock);
	return (0);
}
