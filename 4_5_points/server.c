#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include "enums.h"

#define MAX_PROGRAM_COUNT 10
#define MAX_CLIENT_CONNECTIONS 3

struct ThreadArgs {
    int client_sock;
    int tasks_count;
};

struct program programs[MAX_PROGRAM_COUNT];

int programs_count, completed_count = 0;

sem_t sem;
sem_t print;

// Инициализация массива программ
void initPulls() {
    for (int i = 0; i < programs_count; ++i) {
        struct program program = {.id = i, .executor_id = -1, .checker_id = -1, .status = -1};
        programs[i] = program;
    }
}

void handleTCPClient(int client_socket);

void *threadMain(void *threadArgs) {
    int client_socket;
    int count;

    pthread_detach(pthread_self());

    client_socket = ((struct ThreadArgs *) threadArgs)->client_sock;
    count = ((struct ThreadArgs *) threadArgs)->tasks_count;
    free(threadArgs);
    handleTCPClient(client_socket);

    return (NULL);
}

// Создание серверного сокета TCP
int createTCPServerSocket(unsigned short port) {
    int sock;
    struct sockaddr_in echoServAddr;

    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket failed");
        exit(1);
    }

    memset(&echoServAddr, 0, sizeof(echoServAddr));
    echoServAddr.sin_family = AF_INET;
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    echoServAddr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0) {
        perror("bind failed");
        exit(1);
    }

    if (listen(sock, MAX_CLIENT_CONNECTIONS) < 0) {
        perror("listen failed");
        exit(1);
    }

    return sock;
}

// Принятие подключения клиента
int acceptTCPConnection(int servSock) {
    int client_sock;
    struct sockaddr_in echoClntAddr;
    unsigned int client_len;

    client_len = sizeof(echoClntAddr);

    if ((client_sock = accept(servSock, (struct sockaddr *) &echoClntAddr, &client_len)) < 0) {
        perror("Accept error");
        exit(1);
    }
    printf("Клиент подключен\n");

    printf("Работа с клиентом %s\n", inet_ntoa(echoClntAddr.sin_addr));

    return client_sock;
}

// Вывод информации о задачах
void printTasksInfo() {
    sem_wait(&print);
    for (int j = 0; j < programs_count; ++j) {
        printf("Программа %d со статусом %d, выполнено программистом №%d, проверено программистом %d\n",
               programs[j].id,
               programs[j].status,
               programs[j].executor_id,
               programs[j].checker_id);
    }
    sem_post(&print);
}

// Получение задачи программистом
void getTask(struct response *response, int programmer_id) {
    for (int i = 0; i < programs_count; ++i) {
        if (programs[i].status == NEW) {
            printf("Программист №%d выполняет программу №%d\n", programmer_id, programs[i].id);
            response->response_code = NEW_PROGRAM;
            programs[i].executor_id = programmer_id;
            programs[i].status = EXECUTING;
            response->program = programs[i];
            return;
        } else if (programs[i].status == FIX && programs[i].executor_id == programmer_id) {
            printf("Программист №%d чинит программу %d\n", programmer_id, programs[i].id);
            response->response_code = NEW_PROGRAM;
            programs[i].status = EXECUTING;
            response->program = programs[i];
            return;
        } else if (programs[i].status == EXECUTED && programs[i].executor_id != programmer_id) {
            printf("Программист №%d проверяет программу %d\n", programmer_id, programs[i].id);
            response->response_code = CHECK_PROGRAM;
            programs[i].checker_id = programmer_id;
            programs[i].status = CHECKING;
            response->program = programs[i];
            return;
        } else if (programs[i].executor_id == programmer_id && programs[i].status == WRONG) {
            printf("Программист №%d ошибся в программе %d\n", programmer_id, programs[i].id);
            response->response_code = FIX_PROGRAM;
            programs[i].status = FIX;
            response->program = programs[i];
            return;
        } else {
            printTasksInfo();
        }
    }

    if (completed_count == programs_count) {
        response->response_code = FINISH;
    }
}

// Обработка запроса от клиента
int handleClientRequest(int client_socket, struct request *request) {
    struct program null_task = {-1, -1, -1, -1};
    struct response response = {-1, null_task};

    sem_wait(&sem);

    if (completed_count == programs_count) {
        response.response_code = FINISH;
        printf("\n\nFINISH\n\n");
        printTasksInfo();
    } else {
        int programmer_id = request->programmer_id;
        struct program task = request->program;
        switch (request->request_code) {
            case 0:
                getTask(&response, programmer_id);
                break;
            case 1:
                task.status = EXECUTED;
                programs[task.id] = task;
                getTask(&response, programmer_id);
                break;
            case 2:
                programs[task.id] = task;
                if (task.status == RIGHT) {
                    ++completed_count;
                    printf("\nВыполненных программ %d\n", completed_count);
                }
                getTask(&response, programmer_id);
                break;
            default:break;
        }
    }
    sem_post(&sem);
    send(client_socket, &response, sizeof(response), 0);
    return response.response_code;
}

// Обработка соединения с клиентом
void handleTCPClient(int client_socket) {
    while (1) {
        struct request request = {-1, -1, -1};
        if (recv(client_socket, &request, sizeof(request), 0) < 0) {
            perror("receive bad");
            exit(1);
        }
        if (handleClientRequest(client_socket, &request) == FINISH) {
            break;
        }
    }

    close(client_socket);
}

int main(int argc, char *argv[]) {
    int server_sock;
    int client_sock;
    unsigned short echoServPort;
    pthread_t threadID;
    struct ThreadArgs *threadArgs;

    sem_init(&sem, 0, 1);
    sem_init(&print, 0, 1);

    echoServPort = atoi(argv[1]);
    programs_count = atoi(argv[2]);

    initPulls();

    server_sock = createTCPServerSocket(echoServPort);
    while (completed_count < programs_count) {
        client_sock = acceptTCPConnection(server_sock);
        if ((threadArgs = (struct ThreadArgs *) malloc(sizeof(struct ThreadArgs))) == NULL) {
            perror("malloc() failed");
            exit(1);
        }
        threadArgs->client_sock = client_sock;
        if (pthread_create(&threadID, NULL, threadMain, (void *) threadArgs) != 0) {
            perror("pthread_create()failed");
            exit(1);
        }
        printf("with thread %ld\n", (long int) threadID);
    }
    printf("\nFINISH\n");
    printTasksInfo();

    sem_destroy(&sem);
    sem_destroy(&print);
    close(server_sock);
}