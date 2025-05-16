#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MATCHMAKER_PORT 5000
#define SERVER_PORT 6000

void connect_and_send(int port, const char* host, const char* name, int* sock) {
    struct sockaddr_in addr;
    struct hostent* server = gethostbyname(host);
    *sock = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = *(struct in_addr*)server->h_addr_list[0];
    connect(*sock, (struct sockaddr*)&addr, sizeof(addr));
    send(*sock, name, strlen(name), 0);
}

int main() {
    setbuf(stdout, NULL);

    srand(42);
    char* name = getenv("PLAYER_NAME");
    printf("Player name: %s\n", name);
    if (name == NULL) {
        fprintf(stderr, "Error: PLAYER_NAME environment variable not set.\n");
        return 1;
    }
    int match_sock;
    connect_and_send(MATCHMAKER_PORT, "matchmaker", name, &match_sock);

    char buf[128] = {0};
    recv(match_sock, buf, sizeof(buf), 0);
    close(match_sock);

    int game_sock;
    connect_and_send(SERVER_PORT, "game_server", name, &game_sock);

    while (1) {
        int n = recv(game_sock, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        buf[n] = '\0';
        printf("%s: %s\n", name, buf);
    }

    close(game_sock);
    return 0;
}
