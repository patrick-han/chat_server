#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 


// Constants
#define MAX_CLIENTS 100
#define MAX_LINE_LENGTH 20000 // 20k bytes including the \n
#define DEFAULT_PORT 1234


void doit(unsigned int port);

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

    // Setup our server


    // Handle the clients

    doit(port);

    
    

    return(0);
}


// doit() is for handling a single client, should move all server stuff out of it

void doit(unsigned int port)
{
    // Socket setup
    int serverfd, new_socket, valread;
    struct sockaddr_in server_address;
    int opt = 1;
    int addrlen = sizeof(server_address); 
    char buffer[MAX_LINE_LENGTH] = {0}; 
    char *hello = "Hello from server\n"; 


    // Create socket file descriptor for the server
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) == 0) // IPv4, TCP, protocol value 0
    {
        printf("socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080 
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) 
    { 
        printf("setsockopt error\n");
        exit(EXIT_FAILURE); 
    } 
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);

    // Attach socket to the desired port
    if (bind(serverfd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    }

    // Wait for the client to make a connection
    if (listen(serverfd, MAX_CLIENTS) < 0)
    {
        printf("listen error\n");
        exit(EXIT_FAILURE);
    }

    // Accept a connection
    if ((new_socket = accept(serverfd, (struct sockaddr *)&server_address, (socklen_t*)&addrlen))<0) 
    { 
        printf("accept error\n");
        exit(EXIT_FAILURE); 
    }
    valread = read( new_socket , buffer, 1024); 
    printf("%s\n", buffer); // Print the client message on the server side
    send(new_socket , hello , strlen(hello) , 0 ); // Send a message to the client
}