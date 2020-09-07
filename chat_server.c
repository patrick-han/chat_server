#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
#include <signal.h>
#include <pthread.h>


// Constants
#define MAX_CLIENTS 100
#define MAX_LINE_LENGTH 20000 // 20k bytes including the \n
#define DEFAULT_PORT 1234

// For the pre-threading
#define SBUFSIZE 16
#define NTHREADS 4		// Number of worker threads



// Function definitions
void doit(int connfd);
void *thread(void *vargp);



// sbuf_t sbuf; // Shared buffer of connected descriptors

int main(int argc, char **argv)
{
    // Process argument
    if (argc > 2)
    {
        printf("error: server requires a single argument for the desired port number\n");
        printf("usage: ./chat_server [port]\n"); 
        exit(1);
    }
    unsigned int port;
    if (argc == 1) // If no port number is specified
    {
        port = 1234;
        printf("Started server on default port: %u\n", port);
    }
    else 
    {
        port = atoi(argv[1]);
        printf("Started server on port: %u\n", port);
    }

    // Ignore SIGPIPE
	signal(SIGPIPE, SIG_IGN);

    /* Setup server */

    // Socket setup
    int serverfd = 0;
    struct sockaddr_in server_address;
    int opt = 1;

    // Create socket file descriptor for the server
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) == 0) // IPv4, TCP, protocol value 0
    {
        printf("socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) 
    { 
        printf("setsockopt error\n");
        exit(EXIT_FAILURE); 
    } 
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);

    // Attach socket to the desired port, need to cast sockaddr_in to generic struture sockaddr
    if (bind(serverfd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    }

    // Wait for the clients to make a connection
    if (listen(serverfd, MAX_CLIENTS) < 0)
    {
        printf("listen error\n");
        exit(EXIT_FAILURE);
    }

     int connfd = 0;
     struct sockaddr_in client_address;


    // Handle clients
    while(1)
    {
        // Accept a connection
        // if ((connfd = accept(serverfd, (struct sockaddr *)&server_address, (socklen_t*)&addrlen))<0) 
        // { 
        //     printf("accept error\n");
        //     exit(EXIT_FAILURE); 
        // }


        // Create a conneted descriptor that can be used to communicate with the client
        socklen_t client_addr_len = sizeof(client_address);
        if ((connfd = accept(serverfd, (struct sockaddr *)&client_address, &client_addr_len)) < 0) 
        { 
            printf("accept error\n");
            exit(EXIT_FAILURE); 
        }

        // Handle the client
        doit(connfd);
    }

    return(0);
}


// doit() is for handling a single client, should move all server stuff out of it

void doit(int connfd)
{
    int valread;
    char buffer[MAX_LINE_LENGTH] = {0}; 
    char *hello = "[Server] Welcome to the chatroom, client. Please join a chatroom using the command: JOIN {ROOMNAME} {USERNAME}\n"; 
    send(connfd , hello , strlen(hello) , 0 ); // Send a welcome message to the client


    // Continously read messages from the client
    while ((valread = read(connfd , buffer, MAX_LINE_LENGTH)) > 0)
    {
        // printf("[Server] valread value: %d\n", valread);
        printf("[Client] %s\n", buffer); // Print the client message on the server side
    }
    

    // Close the connection
    close(connfd);
}