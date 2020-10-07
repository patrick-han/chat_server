#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>


// Constants
#define MAX_CLIENTS 100
#define MAX_LINE_LENGTH 20000 // 20k bytes including the \n
#define DEFAULT_PORT 1234
#define MAX_NAME_LENGTH 64
#define MAX_ROOMNAME_LENGTH 64

// For the pre-threading
#define SBUFSIZE 16
#define NTHREADS 4		// Number of worker threads by default


// Typedefs

// Client struct
typedef struct
{
    int clientfd;   // File descriptor for the client's connection
    int identifier; // Client identifier
    int joined;     // Boolean that says whether a client has joined a room 1 if yes 0 if no
    char roomname[MAX_ROOMNAME_LENGTH]; // The chat room a client is part of
    char username[MAX_NAME_LENGTH]; // String name of the user
} client_struct;


// Buffer for pre-threading
typedef struct 
{
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
void client_add(client_struct *client);
void client_remove(client_struct *client);

void sbuf_init(sbuf_t *sp, int n);
void sbuf_insert(sbuf_t *sp, client_struct *item);
client_struct* sbuf_remove(sbuf_t *sp);

char* concat(const char *s1, const char *s2);
void strip_CR_NL(char *str);

static void* thread(void *vargp);

void send_msg_to( char *msg, int connfd);
void send_msg_all(char *msg, client_struct *from_client);

static void doit(client_struct *from_client);




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
void client_remove(client_struct *client)
{
    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (client_list[i] != NULL)
        {
            if(client_list[i]->identifier == client->identifier)
            {
                free(client);
                client_list[i] = NULL;
                num_clients--;
            }
        }
        
    }
    pthread_mutex_unlock(&client_list_mutex);
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

/* 
 * Requires:
 *   2 strings.
 *
 * Effects:
 *   Concatenates two strings and returns the result.
 */
char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1) + strlen(s2) + 1); // +1 for the null-terminator
    // in real code you would check for errors in malloc here
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}


/* 
 * Requires:
 *   Input string.
 *
 * Effects:
 *   Strips newlines and return carriages from an input string by replacing them with nullbytes.
 */
void strip_CR_NL(char *str)
{
    while (*str != '\0') {
        if (*str == '\r' || *str == '\n') {
            *str = '\0';
        }
        str++;
    }
}


/* 
 * Requires:
 *   Port number CLI argument.
 *
 * Effects:
 *   Starts the chat server and serves clients.
 */
int main(int argc, char **argv)
{
    // Process argument
    if (argc > 2)
    {
        printf("error: server requires a single argument for the desired port number\n");
        printf("usage: ./chat_server [port]\n"); 
        exit(EXIT_FAILURE);
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
        if (port <= 1023 || port > 65535) {
            printf("error: specify port number greater than 1023\n");
            exit(EXIT_FAILURE);
        }
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
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) // IPv4, TCP, protocol value 0
    {
        printf("socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) 
    { 
        printf("setsockopt error\n");
        exit(EXIT_FAILURE); 
    } 
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) 
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
        client_struct *client = calloc(1, sizeof(client_struct));
        client->clientfd = connfd;
        client->identifier = next_identifier++;
        client->joined = 0;
        client_add(client);

        if (num_clients > NTHREADS) // If the number of clients is exceeding the default amount of worker threads
        {
            pthread_create(&tid, NULL, thread, NULL);
        }
        // Handle the client by inserting the connfd into the bounded buffer (Done by the main thread)
        sbuf_insert(&sbuf, client);
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
	while(1) 
    {
        // Remove next-to-be-serviced client from bounded buffer
        client_struct *from_client = sbuf_remove(&sbuf);
        int connfd = from_client->clientfd;

		// Service client
        doit(from_client);
        char *leave_msg = concat(from_client->username, " has left");
        send_msg_all(leave_msg, from_client);
        client_remove(from_client);
		close(connfd);
	}
}


/* 
 * Requires:
 *   String message and connection file descriptor.
 *
 * Effects:
 *   Sends a string message to a single client via client's connection file descriptor.
 */
void send_msg_to( char *msg, int connfd)
{
    if (write(connfd, msg, strlen(msg)) < 0) {
        printf("Write message to failed\n");
        exit(EXIT_FAILURE);
    }
}




/* 
 * Requires:
 *   String message and client_struct of who that message is from.
 *
 * Effects:
 *   Sends a string message to all other clients in the same room as from_client.
 */
void send_msg_all(char *msg, client_struct *from_client)
{
    int connfd; // Holds the client fd's for each client iterated throug in the client_list;
    int from_id = from_client->identifier;
    char *from_roomname = from_client->roomname;
    char *end_seq = "\r\n";

    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if ((client_list[i] != NULL) 
            && (client_list[i]->identifier != from_id)
            && (!strcmp(client_list[i]->roomname, from_roomname))) // Looking for a non-NULL slots, don't send to the client that sent it, and only send to clients from the same room
        {
            connfd = client_list[i]->clientfd;

            if (write(connfd, msg, strlen(msg)) < 0) 
            {
                printf("Write message to all failed (msg)\n");
                exit(EXIT_FAILURE);
            }
            // Write \r\n
            if (write(connfd, end_seq, strlen(end_seq)) < 0) 
            {
                printf("Write message to all failed (end_seq)\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}


/* 
 * Requires:
 *   client_struct of the client sending a message.
 *
 * Effects:
 *   Services the client's requests.
 */
void doit(client_struct *from_client)
{
    int valread;
    int from_connfd = from_client->clientfd;
    int from_id = from_client->identifier;
    int from_joined = from_client->joined;
    char msg_buffer[MAX_LINE_LENGTH] = {0}; // Holds a client message

    printf("[Server] Client \"%d\" joined the server\n", from_id);
    // Continously read messages from the client
    while ((valread = read(from_connfd, msg_buffer, MAX_LINE_LENGTH)) > 0)
    {
        strip_CR_NL(msg_buffer); // Strip return carriage and new line
        
        if (!from_joined)
        {
            // strcpy the message buffer since strtok() modifies the msg_buffer
            char msg_buffer_cpy[MAX_LINE_LENGTH] = {0};
            strcpy(msg_buffer_cpy, msg_buffer);

            // Delimit the input string use strtok() to look for JOIN {ROOMNAME} {USERNAME}<NL>
            int i = 1;
            char *p = strtok(msg_buffer_cpy, " ");

            if (p == NULL) // Ignore blank input, or else a segfault will occur in the strcmp() below
            {
                continue;
            }

            for (int k = 0; k < strlen(p); ++k) // Case insensitivity for keyword JOIN
            {
                p[k] = toupper(p[k]);
            }

            if (!strcmp(p, "JOIN")) // We only care to parse the line if its a JOIN command at this point
            {
                while (p) {
                    p = strtok(NULL, " ");
                    i = i + 1;

                    if (i == 2) // ROOMNAME
                    {
                        strcpy(from_client->roomname, p);
                    }
                    else if (i == 3) // USERNAME
                    {
                        strcpy(from_client->username, p);
                    }
                }
                // If after parsing the JOIN command, there are not 3 tokens (including JOIN) something has gone wrong..
                if (i != 4) // +1 from how the while() loop above works
                {
                    send_msg_to("ERROR\n", from_connfd);
                    break;
                }
                else
                {
                    //TODO: validate roomname and username are not spaces or something wacky...
                    printf("[Server] Client identified by: \"%d\" and named: \"%s\" has joined the room called: \"%s\"\n", from_id, from_client->username, from_client->roomname);
                    char* joined_message = concat(from_client->username, " has joined");
                    send_msg_all(joined_message, from_client);
                    send_msg_to(joined_message, from_client->clientfd);
                    send_msg_to("\r\n", from_client->clientfd);

                    free(joined_message);
                    from_joined = 1;
                }
                
            }
            else // if the command isnt a valid JOIN, send ERROR to client and close the connection
            {
                send_msg_to("ERROR\n", from_connfd);
                break;
            }
            
        }
        else // Once the client has joined a chatroom they will be able to send messages
        {
            printf("[Server] In room: \"%s\", client \"%d\" said: \"%s\"\n", from_client->roomname, from_id, msg_buffer); // Print the client message on the server side

            // Append username and prompt to the message
            char* username = from_client->username;
            char* prompt = concat(username, ": ");
            char* complete_msg = concat(prompt, msg_buffer);

            // Send prompted message back to self and to all other clients in the same chat room
            send_msg_all(complete_msg, from_client);
            send_msg_to(complete_msg, from_client->clientfd);
            send_msg_to("\r\n", from_client->clientfd);

            free(prompt);
            free(complete_msg);
        }
        memset(msg_buffer, 0, sizeof(msg_buffer)); // Clear msg_buffer so previous messages don't leak into the next
    }
    
}