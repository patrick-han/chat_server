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


// Typedefs
typedef struct {
	int *buf;		/* Buffer array */
	int n;			/* Maximum number of slots */
	int front;		/* buf[(front+1)%n] is the first tiem */
	int rear;		/* buf[rear%n] is the last item */
	int slots;		/* Counts avalible slots */
	pthread_mutex_t mutex;	/* Protects access to buf */
	pthread_cond_t not_empty;	
	pthread_cond_t not_full;
} sbuf_t;



// Function definitions
void doit(int connfd);
void *thread(void *vargp);


/*
 * Requires:
 *   Desired size of the bounded buffer and a pointer to the sbuf_t pointer.
 *
 * Effects:
 *   Initializes a bounded buffer for holding connection requests.
 */
static void 
sbuf_init(sbuf_t *sp, int n)
{
	// sp->buf = Calloc(n, sizeof(int));
    sp->buf = calloc(n, sizeof(int));
	sp->n = n;				/* Buffer holds n items */
	sp->front = sp->rear = 0;		/* Empty iff front == rear */
	pthread_mutex_init(&sp->mutex, NULL);	/* Default mutex for locking */
	sp->slots = 0;				/* All slots available */
	pthread_cond_init(&sp->not_full, NULL);	
	pthread_cond_init(&sp->not_empty, NULL);
}

/* 
 * Requires:
 *   Buffer pointer and item to be inserted.
 *
 * Effects:
 *   Inserts item into the rear of the buffer.
 */
static void 
sbuf_insert(sbuf_t *sp, int item)
{				
	pthread_mutex_lock(&sp->mutex);		/* Lock the buffer */
	while (sp->slots == sp->n) {		/* Wait for available slot */
		pthread_cond_wait(&sp->not_full, &sp->mutex);
	}
	sp->buf[(++sp->rear) % (sp->n)] = item;	/* Insert the item */
	sp->slots = sp->slots + 1;
	pthread_cond_signal(&sp->not_empty);
	pthread_mutex_unlock(&sp->mutex);	/* Unlock the buffer */
			
}

/* 
 * Requires:
 *   Buffer pointer.
 *
 * Effects:
 *   Removes first item from buffer.
 */
static int 
sbuf_remove(sbuf_t *sp)
{
	int item;
	pthread_mutex_lock(&sp->mutex);		/* Lock the buffer */
	while (sp->slots == 0) {		/* Wait for available item */
		pthread_cond_wait(&sp->not_empty, &sp->mutex);
	}			
	item = sp->buf[(++sp->front) % (sp->n)];/* Remove the item */
	sp->slots = sp->slots - 1;
	pthread_cond_signal(&sp->not_full);	/* Announce available slot */
	pthread_mutex_unlock(&sp->mutex);	/* Unlock the buffer */
	return item;
}


sbuf_t sbuf; // Shared buffer of connected descriptors

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







     
    /* Pre-threading setup */
    pthread_t tid;
    
	sbuf_init(&sbuf, SBUFSIZE); // Create bounded buffer for our worker threads
	for (int i = 0; i < NTHREADS; i++) // Spawn worker threads
    {
		pthread_create(&tid, NULL, thread, NULL);
    }





    /* Client settings */
    int connfd = 0;
    struct sockaddr_in client_address;

    /* Handle clients */
    while(1)
    {
        // Accept a connection
        // if ((connfd = accept(serverfd, (struct sockaddr *)&server_address, (socklen_t*)&addrlen))<0) 
        // { 
        //     printf("accept error\n");
        //     exit(EXIT_FAILURE); 
        // }


        // Create a connected descriptor that can be used to communicate with the client
        socklen_t client_addr_len = sizeof(client_address);
        if ((connfd = accept(serverfd, (struct sockaddr *)&client_address, &client_addr_len)) < 0) 
        { 
            printf("accept error\n");
            exit(EXIT_FAILURE); 
        }

        // Handle the client by inserting the connfd into the bounded buffer (Done by the main thread)
        sbuf_insert(&sbuf, connfd);
    }

    return(0);
}

/* 
 * Requires:
 *   Nothing
 *
 * Effects:
 *   Services client request with worker thread
 */
void *thread(void *vargp)
{
	(void) vargp; // Avoid message about unused arguments
    pthread_detach(pthread_self()); // No return values
	while(1) {
		    // Remove descriptor from bounded buffer
		    int connfd = sbuf_remove(&sbuf);
        	struct sockaddr_in addr;
        	socklen_t addr_size = sizeof(struct sockaddr_in);
        	int res = getpeername(connfd, (struct sockaddr *)&addr, &addr_size);
        	if (res < 0) {
           		// Error
            	// printf("Error with getting peer name from file descriptor %d, %s\n", res, strerror(errno));
                printf("Error with getting peer name from file descriptor %d\n", res);
	    		continue;
        	}
		// Service client and close
        doit(connfd);
		close(connfd);
	}
}






// doit() is for handling a single client

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
    
}