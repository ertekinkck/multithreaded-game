#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MATCHMAKER_PORT 5000
#define SERVER_PORT 6000

void	connect_and_send(int port, const char* host, const char* name, int* sock)
{
	struct sockaddr_in addr;
	struct hostent* server = gethostbyname(host);
	*sock = socket(AF_INET, SOCK_STREAM, 0);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr = *(struct in_addr*)server->h_addr_list[0];
	connect(*sock, (struct sockaddr*)&addr, sizeof(addr));
	dprintf(*sock, name);
}

int	main(void)
{
	srand(42);
	char	*name = getenv("PLAYER_NAME");

	dprintf(1, "Player name: %s\n", name);
	if (name == NULL)
	{
		dprintf(2, "Error: PLAYER_NAME environment variable not set.\n");
		return 1;
	}
	int match_sock;
	connect_and_send(MATCHMAKER_PORT, "matchmaker", name, &match_sock);

	char buf[128] = {0};
	read(match_sock, buf, sizeof(buf));
	close(match_sock);

	int game_sock;
	connect_and_send(SERVER_PORT, "game_server", name, &game_sock);

	while (1)
	{
		int n = read(game_sock, buf, sizeof(buf));
		if (n <= 0)
			break;
		buf[n] = '\0';
		dprintf(1, "%s: %s\n", name, buf);
	}

	close(game_sock);
	return 0;
}
