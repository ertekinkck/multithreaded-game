FROM    gcc:latest

WORKDIR /app

CMD gcc -o game_server ./src/game_server.c ./common/* -I /app/common && ./game_server

