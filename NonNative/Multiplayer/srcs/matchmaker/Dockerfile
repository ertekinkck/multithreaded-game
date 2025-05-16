FROM    gcc:latest

WORKDIR /app

CMD gcc -o matchmaker ./src/matchmaker.c ./common/*.c -I ./common && ./matchmaker
