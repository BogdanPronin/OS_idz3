#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "enums.h"

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in echoServAddr;
    unsigned short echoServPort;
    char *servIP;

    // Получаем идентификатор программиста, IP-адрес сервера и номер порта сервера из аргументов командной строки
    int programmer_id = atoi(argv[1]);
    servIP = argv[2];
    echoServPort = atoi(argv[3]);

    // Создаем TCP-сокет
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket failed");
        exit(1);
    }

    // Настраиваем структуру адреса сервера
    memset(&echoServAddr, 0, sizeof(echoServAddr));
    echoServAddr.sin_family = AF_INET;
    echoServAddr.sin_addr.s_addr = inet_addr(servIP);
    echoServAddr.sin_port = htons(echoServPort);

    // Устанавливаем соединение с сервером
    if (connect(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0) {
        perror("connect() failed");
        exit(1);
    }

    struct program task = {-1, -1, -1, -1};
    struct request request = {GET_TASK, programmer_id, task};

    while (1) {

        if (send(sock, &request, sizeof(request), 0) < 0) {
            perror("send() bad");
            exit(1);
        }
        printf("Программист №%d отправил запрос %d на сервер\n", programmer_id, request.request_code);

        struct response response = {-1, -1, -1, -1};
        if (recv(sock, &response, sizeof(response), 0) < 0) {
            perror("recv() bad");
            exit(1);
        }
        printf("Программист №%d получил запрос %d с сервера\n", programmer_id, response.response_code);

        if (response.response_code == FINISH) {
            break;
        }

        switch (response.response_code) {
            case UB:
                break;
            case NEW_PROGRAM:
                sleep(1);
                request.program = response.program;
                request.request_code = SEND_PROGRAM;
                break;
            case CHECK_PROGRAM:
                sleep(1);
                int8_t result = rand() % 2;
                printf("Результат проверки программы %d - %d", response.program.id, result);
                response.program.status = result == 0 ? WRONG : RIGHT;
                request.program = response.program;
                request.request_code = SEND_CHECK;
                break;
            case FIX_PROGRAM:
                sleep(1);
                request.program = response.program;
                request.request_code = SEND_PROGRAM;
                break;
            default:
                request.request_code = GET_TASK;
                break;
        }
        sleep(5);
    }

    close(sock);
    exit(0);
}
