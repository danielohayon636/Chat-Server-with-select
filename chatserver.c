#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <netdb.h>
#include <signal.h>
void error(char *msg) //return error msg
{
    perror(msg);
    printf("\nerror:: %s \n", msg);
    exit(1);
}
struct client_struct //Store 1 client
{
    int id;
    int target;
    char *msg; // text messege from user
    struct client_struct *next;
} typedef client_struct;

struct Queue // a queue to keep all clients
{
    struct client_struct *front, *rear;
    int size; //queue length
};
struct client_struct *newNode(int id, int target, char *msg) //called by enqueue
{
    struct client_struct *new_client = (struct client_struct *)malloc(sizeof(struct client_struct));
    new_client->id = id;
    new_client->target = target;
    new_client->msg = msg;
    new_client->next = NULL;
    return new_client;
}
struct Queue *createQueue() // create an empty queue
{
    struct Queue *q = (struct Queue *)malloc(sizeof(struct Queue));
    q->front = q->rear = NULL;
    q->size = 0;
    return q;
}
void enQueue(struct Queue *q, int id, int target, char *msg) //add client to queue
{
    q->size++;
    struct client_struct *new_client = newNode(id, target, msg);
    if (q->rear == NULL)
    {
        q->front = q->rear = new_client;
        return;
    }
    q->rear->next = new_client; //Add new node at the end of queue and change rear
    q->rear = new_client;
}

client_struct *deQueue(struct Queue *q) //get client out of the queue
{
    if (q->front == NULL) //If queue empty return null.
        return NULL;
    struct client_struct *new_client = q->front;
    q->front = q->front->next;
    if (q->front == NULL) // If front becomes NULL, then change rear  to NULL
        q->rear = NULL;
    q->size--;
    return new_client;
}
int *clients; //srotes all clients on chat
int main_socket;
int size_of_clients; //num of clients
struct Queue *q;
struct Queue *olds; //queue to resend messeges which failed to send

void handler(int num) //handle CTRL-C
{
    client_struct *temp;
    while (q->size > 0)
    {
        temp = deQueue(q);
        free(temp);
        q->size--;
    }
    while (olds->size > 0)
    {
        temp = deQueue(olds);
        free(temp);
        olds->size--;
    }
    free(q);
    free(olds);
    for (int i = 0; i < size_of_clients; i++)
        if (clients[i] == 1)
            close(i);
    free(clients);
    close(main_socket);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, handler); //handle CTRL-C
    if (argc != 2)           //validation
    {
        printf("Command line usage: server <port> <pool-size> <max-number-of-request>\n");
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]); //port number
    q = createQueue();
    olds = createQueue();
    size_of_clients = 128;
    clients = (int *)calloc(sizeof(int), size_of_clients); //clients fd's array
    for (int i = 0; i < size_of_clients; i++)
    {
        clients[i] = -1; //init all fd numbers
    }
    struct sockaddr_in server_addr; //init
    struct sockaddr client_address; //init
    socklen_t client_len = sizeof(struct sockaddr_in);
    main_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (main_socket < 0) //main socket init validation
    {
        free(clients);
        free(q);
        free(olds);
        error("main socket init failed\n");
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    /*============ BIND ==============*/
    if (bind(main_socket, (__CONST_SOCKADDR_ARG)&server_addr, sizeof(server_addr)) < 0) //bind check
    {
        free(clients);
        free(q);
        free(olds);
        error("bind failed\n");
    }
    /*============ LISTEN ==============*/
    if (listen(main_socket, 5) < 0) //listen check
    {
        free(clients);
        free(q);
        free(olds);
        error("listen failed\n");
    }
    fd_set all_fds;
    fd_set to_read_from;
    fd_set to_write_to;
    FD_ZERO(&all_fds);
    FD_SET(main_socket, &all_fds);
    int rc = 0;
    int max_socket = main_socket + 1;
    char msg[4096];
    while (1) //untill CTRL C received
    {
        to_read_from = all_fds;                                           // fd numbers to read from
        to_write_to = all_fds;                                            // fd numbers to write to
        rc = select(max_socket, &to_read_from, &to_write_to, NULL, NULL); //wait for "action" on chat
        bzero(msg, 4096);
        if (FD_ISSET(main_socket, &to_read_from)) //check fd numbers of clients to read from
        {
            int serving_socket = accept(main_socket, &client_address, &client_len); //new fd number for new  client
            if (serving_socket >= max_socket)                                       // fd is not on set so we need to put it on the set
            {
                max_socket = serving_socket + 1;
            }
            if (serving_socket >= 0)
            {
                clients[serving_socket] = 1;
                FD_SET(serving_socket, &all_fds);
            }
        }
        for (int fd = main_socket + 1; fd < max_socket; fd++)
        {
            if (FD_ISSET(fd, &to_read_from))
            {
                rc = read(fd, msg, 4096); //read messege from client
                if (rc == 0)
                {
                    //delete from array
                    clients[fd] = -1;
                    FD_CLR(fd, &all_fds);
                    close(fd);
                }
                else if (rc > 0) //server is ready to read from client socket
                {
                    printf("\nserver is ready to read from socket %d\n", fd);
                    for (int j = main_socket + 1; j < max_socket; j++)
                    {
                        if (j != fd)
                        {
                            enQueue(q, fd, j, msg); //put all the other clients bact to the queue
                        }
                    }
                }
                else
                {
                    error("read failed");
                }
            }
        }
        while (q->size > 0)
        {
            client_struct *current = deQueue(q);
            int tar = current->target;

            if (!FD_ISSET(tar, &to_write_to)) //client is not ready to written into
            {
                enQueue(olds, current->id, current->target, current->msg);
                free(current);
            }
            else //client is ready
            {
                printf("server is ready to write to socket %d\n", current->target);
                char guest[12] = "\0";
                sprintf(guest, "guest%d: ", current->id);
                if ((rc = write(tar, guest, strlen(guest) + 1)) < 0) //write user name
                    error("write failed");
                if ((rc = write(tar, current->msg, strlen(current->msg))) < 0) //write user messege
                    error("write failed");
                free(current);
            }
        }
        while (q->size > 0)
        {
            client_struct *current = deQueue(olds);
            enQueue(q, current->id, current->target, current->msg);
            free(current);
        }
    }
}