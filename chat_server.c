#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h>

#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>


// Constants
#define MAX_CLIENTS 100
#define MAX_LINE_LENGTH 20000 // 20k bytes including the \n
#define DEFAULT_PORT 1234

// For the pre-threading
#define SBUFSIZE 16
#define NTHREADS 4		// Number of worker threads


// Typedefs

// Client struct
typedef struct
{
    int clientfd;   // File descriptor for the client's connection
    int identifier; // Client identifier
    int joined;     // Boolean that says whether a client has joined a room 1 if yes 0 if no
    // TODO: Add attribute which says which chat room the client is a part of
} client_struct;


// Buffer for pre-threading
typedef struct 
{
	// int *buf;		/* Buffer array */
    client_struct **buf;
	int n;			/* Maximum number of slots */
	int front;		/* buf[(front+1)%n] is the first item */
	int rear;		/* buf[rear%n] is the last item */
	int slots;		/* Counts avalible slots */
	pthread_mutex_t mutex;	/* Protects access to buf */
	pthread_cond_t not_empty;	
	pthread_cond_t not_full;
} sbuf_t;




// Function definitions
static void           doit(client_struct *from_client);
static void*          thread(void *vargp);
void	          sbuf_init(sbuf_t *sp, int n);
void	          sbuf_insert(sbuf_t *sp, client_struct *item);
client_struct* sbuf_remove(sbuf_t *sp);


// Globals
sbuf_t sbuf; // Shared buffer of client_struct pointers
client_struct *client_list[MAX_CLIENTS] = {0}; // Array to hold all the currently connected clients (NULL initalized)
pthread_mutex_t client_list_mutex;
atomic_uint num_clients = 0;
int next_identifier = 1; // Number we use to get a unique identifier for an incoming new client


/*
 * Requires:
 *   Client struct pointer.
 *
 * Effects:
 *   Adds a client the the client_list.
 */
void client_add(client_struct *client)
{
    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (client_list[i] == NULL) // Looking for a NULL slot
        {
            client_list[i] = client;
            printf("[Server] Client connected with identifier: %d and fd: %d\n", client->identifier, client->clientfd);
            num_clients++;
            break;
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}

/*
 * Requires:
 *   Client struct pointer.
 *
 * Effects:
 *   Removes a client the the client_list.
 */
// void client_remove(client_struct *client)
// {

// }


/*
 * Requires:
 *   Client struct pointer.
 *
 * Effects:
 *   Adds a client the the client_list.
 */
void client_list_print(void)
{
    printf("[Server] Total number of clients: %u\n", num_clients);
    // pthread_mutex_lock(&client_list_mutex);
    // for (int i = 0; i < MAX_CLIENTS; ++i)
    // {
    //     if (!client_list[i]) // Looking for a non-NULL slot
    //     {
    //         // printf("Client found: %u\n", client_list[i]->identifier);
    //         printf("Client found:\n");
    //     }
    // }
    // pthread_mutex_unlock(&client_list_mutex);
}


/*
 * Requires:
 *   Desired size of the bounded buffer and a pointer to the sbuf_t pointer.
 *
 * Effects:
 *   Initializes a bounded buffer for holding connection requests.
 */
void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = malloc(n * sizeof(client_struct *));
    for (int i = 0; i < n; ++i)
    {
        (sp->buf)[i] = NULL;
    }
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
void sbuf_insert(sbuf_t *sp, client_struct *item)
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
client_struct* sbuf_remove(sbuf_t *sp)
{
	client_struct *item;
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

    // Initalize mutex for adding clients to the client_list
    pthread_mutex_init(&client_list_mutex, NULL);



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
        // Create a connected descriptor that can be used to communicate with the client
        socklen_t client_addr_len = sizeof(client_address);
        if ((connfd = accept(serverfd, (struct sockaddr *)&client_address, &client_addr_len)) < 0) 
        { 
            printf("accept error\n");
            exit(EXIT_FAILURE); 
        }

        if (num_clients == MAX_CLIENTS) // Reject connection if the client list is full
        {
            printf("[Server] Max clients reached, connection rejected\n");
            close(connfd);
            continue;
        }

        // Initialize a client struct + attributes, and add it to the list of clients
        client_struct *client = malloc(sizeof(client_struct));
        client->clientfd = connfd;
        client->identifier = next_identifier++;
        client->joined = 0;
        client_add(client);

        // Handle the client by inserting the connfd into the bounded buffer (Done by the main thread)
        sbuf_insert(&sbuf, client);


        // REMEMBER TO FREE UR MEMORY
    }

    return(0);
}

/* 
 * Thread routine
 * 
 * Requires:
 *   Nothing
 *
 * Effects:
 *   Services client request with worker thread.
 */
void *thread(void *vargp)
{
	(void) vargp; // Avoid message about unused arguments
    pthread_detach(pthread_self()); // No return values
	while(1) {
		    // Remove descriptor from bounded buffer
		    // int connfd = sbuf_remove(&sbuf);

            client_struct *from_client = sbuf_remove(&sbuf);
            int connfd = from_client->clientfd;
        	// struct sockaddr_in addr;
        	// socklen_t addr_size = sizeof(struct sockaddr_in);
        	// int res = getpeername(connfd, (struct sockaddr *)&addr, &addr_size);
        	// if (res < 0) {
           	// 	// Error
            //     printf("Error with getting peer name from file descriptor %d\n", res);
	    	// 	continue;
        	// }
		// Service client and close
        // doit(connfd);
        doit(from_client);
		close(connfd);
	}
}


void send_msg_to( char *msg, int connfd)
{
    if (write(connfd, msg, strlen(msg)) < 0) {
        printf("Write message to failed\n");
        exit(EXIT_FAILURE);
    }
}

char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1) + strlen(s2) + 1); // +1 for the null-terminator
    // in real code you would check for errors in malloc here
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}


void send_msg_all(char *msg, char *prompt, client_struct *from_client)
{
    int connfd; // Holds the client fd's for each client iterated throug in the client_list;
    int from_id = from_client->identifier;

    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if ((client_list[i] != NULL) && (client_list[i]->identifier != from_id)) // Looking for a non-NULL slots, don't send to the client that sent it, 
        {
            connfd = client_list[i]->clientfd;

            // Write indicator of which user sent the message {USERNAME}: 
            if (write(connfd, prompt, strlen(prompt)) < 0) 
            {
                printf("Write message to all failed\n");
                exit(EXIT_FAILURE);
            }
            // Write the message sent from the user from_client
            if (write(connfd, msg, strlen(msg)) < 0) 
            {
                printf("Write message to all failed\n");
                exit(EXIT_FAILURE);
            }
            // free(username);
            // free(prompt);
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}




// doit() is for handling a single client
void doit(client_struct *from_client)
{
    int valread;
    int from_connfd = from_client->clientfd;
    int from_id = from_client->identifier;
    int from_joined = from_client->joined;
    char msg_buffer[MAX_LINE_LENGTH] = {0}; // Holds a client message

    //char *welcome_message = "[Server] Welcome to the chatroom, client. Please join a chatroom using the command: JOIN {ROOMNAME} {USERNAME}\n"; 
    //send(connfd, welcome_message, strlen(welcome_message) , 0 ); // Send a welcome message to the client

    printf("Client \"%d\" joined the server\n", from_id);
    // Continously read messages from the client
    while ((valread = read(from_connfd, msg_buffer, MAX_LINE_LENGTH)) > 0)
    {
        if (!from_joined)
        {
            // strcpy the message buffer since strtok() modifies the msg_buffer
            char msg_buffer_cpy[MAX_LINE_LENGTH] = {0};
            strcpy(msg_buffer_cpy, msg_buffer);
            // Delimit the input string use strtok() to look for JOIN {ROOMNAME} {USERNAME}<NL>
            int i = 1;
            char *p = strtok(msg_buffer_cpy, " ");
            if (!strcmp(p, "JOIN")) // We only care to parse the line if its a JOIN command at this point
            {
                while (p) {
                    printf("%i. token = %s\n", i, p);
                    p = strtok(NULL, " ");
                    i = i + 1;
                }
                // If after parsing the JOIN command, there are not 3 tokens (including JOIN) something has gone wrong..
                if (i != 4) // +1 from how the while() loop above works
                {
                    send_msg_to("JOIN requires 2 arguments: JOIN {ROOMNAME} {USERNAME}\n", from_connfd);
                }
                else
                {
                    //TODO: validate roomname and username are not spaces or something wacky...

                    from_joined = 1;
                }
                
            }
        }
        else // Once the client has joined a chatroom they will be able to send messages
        {
            printf("[Server] Client \"%d\" said: %s\n", from_id, msg_buffer); // Print the client message on the server side


            // 1. Construct the USERNAME: prompt
            // This will need to be changed when we actually have string usernames instead of just using the identifier
            int username_length = snprintf(NULL, 0, "%d", from_id);
            char* username = malloc(username_length + 1);
            snprintf(username, username_length + 1, "%d", from_id);
            char* prompt = concat(username, ": ");

            // Send prompted message back to self and to all other clients in the same chat room
            // send_msg_to(msg_buffer, from_connfd);
            send_msg_all(msg_buffer, prompt, from_client);

            free(username);
            free(prompt);
        }
        memset(msg_buffer, 0, sizeof(msg_buffer)); // Clear msg_buffer so previous messages don't leak into the next
    }
    
}