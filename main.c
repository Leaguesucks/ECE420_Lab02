/*
    * main.c:
    *   The main server
    * 
    * Run as: ./main
    *         ./main <array len> <serverIP> <server port>                       -- MAIN REQUIREMENT: Focus on this for now
    *         ./main <array len> <serverIP> <server port> <strlen>              -- OPTIONAL
    *         ./main <array len> <serverIP> <server port> <strlen> <client num> -- OPTIONAL
    * TO DO:
    *         * The program compiles successfully without any warnings or errors for now, but needs verification
    *         * Provide error checking for mutex/cond lock/unlock in request_handler                             -- OPTIONAL, but may come in handdy for debug, also slow down accesing array
    *         * Improve the program to take more args                                                            -- OPTIONAL, but might be neat
    *         * If possible, verify memset() is doing its job. If not, just replace initialization with for loop
    * 
    * *** Provide more description here if needed ***
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>

#include "timer.h"
#include "common.h"

#define IPADDRESS "127.0.0.1" // Default IP Address
#define INIT_VAL  '\0'        // Initial value char in the main array. Init string should be filled with this char
#define PORT 3000             // Default port number
#define NUMSTR 1024           // Default number of string in the main array
#define STRLEN 1000           // Default length of each string in the main array
#define REQUESTLEN 13         // Length of the string request sent from client (XXX-Y-SSSSSS\0)

char            **theArray;               // The main array: for read and write
double          *timeArray;               // An array to hold the time it takes to process each request
int             *clientdesc;              // An array that hold the client descriptor
int             timeLength;               // Length of the time array, also = the number of request that has been issued
char            ipaddr[20];               // The server IP address
short int       portnum;                  // Port number is represented by a 16 bits integer
int             numstr, lenstr, numthr;   // Number of string, length of each string in the main array, and number of thread created (= num of request for now)
int             numcli;                   // Number of client

pthread_mutex_t  mutex_rwlock;            // Read/Write lock mutex
pthread_mutex_t *mutex_recvlock;          // A mutex array for receiving data from the client
pthread_mutex_t *mutex_translock;         // A mutex array for transmiting the data back to the client
pthread_cond_t   cond_rlock;              // Read lock
pthread_cond_t   cond_wlock;              // Write lock
int              readers;                 // Number of current readers
int              writers;                 // Number of current writer. THERE SHOULD ONLY BE 1 WRITER AND 0 READERS AT ANY GIVEN MOMENT
int              pending_writers;         // Number of writers waiting to write



/* The thread function
 * NOTE:
 *      * The reasons why we didnt do a struct for mutexes and conditional var like in lectures is for optimizing accesing the array
 *      * (might be changed later tho)
 * 
 *      *** More description here if needed ***
 */
void *request_handler(void* arg) {
    long          rank;                                  // This thread rank
    int           cfd;                                   // This thread client descriptor
    ClientRequest creqst;                                // To store the processed client requests
    char          request[COM_BUFF_SIZE];                // Request sent from client
    char          response[lenstr + COM_BUFF_SIZE + 10]; // Response to send back to the client (10 spare bytes added)
    char          msg[lenstr];                           // String that stored content from the array
    double        start, end;                            // For measuring the array accesing time
    int           rev;

    rank = (long) arg; // Assign this thread its rank
    cfd = clientdesc[rank % numcli]; // Each client is handled by numthr/numcli thread

    /* Each thread only handle 1 request */
    pthread_mutex_lock(&mutex_recvlock[rank % numcli]); // Only 1 thread should receive data from the same client at the same time

    if ((rev = recv(cfd, request, REQUESTLEN * sizeof(char), 0)) < 0) {
        fprintf(stderr, "Cannot read request from client %d\n", cfd);
        exit(EXIT_FAILURE);
    }
    else if (rev == 0) { // Client has shut its communication
        pthread_exit(NULL);
        return NULL; // To make very damn sure that this thread terminate
    }

    pthread_mutex_unlock(&mutex_recvlock[rank % numcli]);

    // Process the client requests
    if (ParseMsg(request, &creqst) != 0) {
        fprintf(stderr, "Cannot process client %d request: \'%s\'\n", cfd, request);
        exit(EXIT_FAILURE);
    }

    GET_TIME(start); // Start the timer

    if (creqst.is_read) { // It is a read operation
        pthread_mutex_lock(&mutex_rwlock);
        
        while (writers > 0 || pending_writers > 0) { // Cannot read when there are writers
            pthread_cond_wait(&cond_rlock, &mutex_rwlock); // Wait for read lock to be released
        }

        /* Granted read lock, proceed to do some read operations */
        readers++;
        pthread_cond_broadcast(&cond_rlock); // Wake up all readers
        getContent(msg, creqst.pos, theArray); // Get the content from the array

        /* Finish reading, now decrement the read count */
        if (readers > 0) readers--;

        pthread_mutex_unlock(&mutex_rwlock);
    }
    else { // It is a write operation
        pthread_mutex_lock(&mutex_rwlock);

        while (readers > 0 || writers > 0) { // Cannot write when there are readers or writers
            pending_writers++; // Notify that there is 1 more writers waiting
            pthread_cond_wait(&cond_wlock, &mutex_rwlock);
            pending_writers--; // This pending writer can now write
        }

        /* Writer starts write operation */
        writers++;
        setContent(creqst.msg, creqst.pos, theArray); // Write to the array

        if (writers > 0) writers = 0; // Finish writing, now there should be 0 current writer
        if (readers == 0 && pending_writers > 0) { // If there are pending writers, wake one of them up
            pthread_cond_signal(&cond_wlock);
        }

        pthread_mutex_unlock(&mutex_rwlock);
    }

    GET_TIME(end); // Finish the timer

    timeArray[rank] = end - start;

    if (creqst.is_read) {
        sprintf(response, "From server thread %ld to client %d: [R] theArray[%d] = \'%s\'", rank, cfd, creqst.pos, msg);
    }
    else {
        sprintf(response, "From server thread %ld to client %d: [W] theArray[%d] = \'%s\'", rank, cfd, creqst.pos, creqst.msg);
    }

    pthread_mutex_lock(&mutex_translock[rank % numcli]); // Only 1 thread should transmit the data back to the same client at the same time

    if (write(cfd, response, sizeof(response)) < 0) {
        fprintf(stderr, "Thread %ld fail to response back to client %d\n", rank, cfd);
        exit(EXIT_FAILURE);
    }

    pthread_mutex_unlock(&mutex_translock[rank % numcli]);

    pthread_exit(NULL);
    return NULL; // To make very damn sure that this thread exit
}

/*
    * Check the program arguments
    * Param : args num, args
    * Return: 0 if OK, negative number otherwise
*/
int CheckArgs(int argv, char* argc[]) {
    int OK = 0;
    errno = 0;

    switch(argv) {
        case 1:
            strncpy(ipaddr, IPADDRESS, sizeof(ipaddr) - 1);
            portnum = PORT;
            numstr = NUMSTR;
            lenstr = STRLEN;
            numthr = COM_NUM_REQUEST;
            numcli = COM_CLIENT_THREAD_COUNT;
            break;
        
        case 4:
            strncpy(ipaddr, argc[2], sizeof(ipaddr) - 1);
            
            char *endptr1, *endptr2;
            long result1, result2;

            result1 = strtol(argc[1], &endptr1, 10);
        
            // Check for overflow for arraylen
            if (errno == ERANGE) {
                fprintf(stderr, "Args 1 <array len> = \'%s\': OVERFLOW\n", argc[1]);
                OK--;
            }
            
            errno = 0; // Reset errno
            result2 = strtol(argc[3], &endptr2, 10);

            // Check for overflow for port num
            if (errno == ERANGE) {
                fprintf(stderr, "Args 3 <server port> = \'%s\': OVERFLOW\n", argc[3]);
                OK--;
            }

            // Check for valid input (array len and port num)
            if (*endptr1 != '\0') {
                fprintf(stderr, "Args 1 <array len> = \'%s\': INVALID INPUT! MUST BE AN INT\n", argc[1]);
                OK--;
            }
            if (*endptr2 != '\0') {
                fprintf(stderr, "Args 3 <server port> = \'%s\': INVALID INPUT! MUST BE AN INT\n", argc[3]);
                OK--;
            }

            // Check if the array len is valid
            if (result1 > INT_MAX) {
                fprintf(stderr, "Array length = %ld: OVERFLOW! MAX <array len> = %d\n", result1, INT_MAX);
                OK--;
            }
            if (result1 <= 0) {
                fprintf(stderr, "Array length cannot be negative or 0\n");
                OK--;
            }

            if (result2 > SHRT_MAX) {
                fprintf(stderr, "Port number = %ld to large! MAX PORT = %d\n", result2, SHRT_MAX);
                OK--;
            }
            if (result2 < 0) {
                fprintf(stderr, "Port number cannot be negative\n");
                OK--;
            }

            lenstr = STRLEN;
            numthr = COM_NUM_REQUEST;
            numcli = COM_CLIENT_THREAD_COUNT;

            break;

        default:
            fprintf(stderr, "Number of argument = %d is invalid! Must be 0 or 3\n", argv - 1);
            OK--;    
    }

    return OK;
}

int main(int argv, char* argc[]) {
    int                sockfd;     // Socket descriptor
    struct sockaddr_in sockvar;    // Contains IP address, port number
    
    pthread_t          *thrID;     // An array to store the threads ID
    long                thRank;    // To assign rank to a thread

    if (CheckArgs(argv, argc) < 0) exit(EXIT_FAILURE); // Process the arguments

    printf("Server: \'%s\', IPADDRESS = \'%s\', PORT = %d\n", argc[0], ipaddr, portnum);

    if ((thrID = (pthread_t*) malloc(numthr * sizeof(pthread_t))) == NULL) {
        fprintf(stderr, "Cannot allocate memory for thrID\n");
        exit(EXIT_FAILURE);
    }
    if ((timeArray = (double*) malloc(numthr * sizeof(double))) == NULL) {
        fprintf(stderr, "Cannot allocate memory for timeArray\n");
        exit(EXIT_FAILURE);
    }
    if ((clientdesc = (int*) malloc(numcli * sizeof(int))) == NULL) {
        fprintf(stderr, "Cannot allocate memory for clientdesc\n");
        exit(EXIT_FAILURE);
    }
    if ((mutex_recvlock = (pthread_mutex_t*) malloc(numcli * sizeof(pthread_mutex_t))) == NULL) {
        fprintf(stderr, "Cannot allocate memory for mutex_recvlock\n");
        exit(EXIT_FAILURE);
    }
    if ((mutex_translock = (pthread_mutex_t*) malloc(numcli * sizeof(pthread_mutex_t))) == NULL) {
        fprintf(stderr, "Cannot allocate memory for mutex_translock\n");
        exit(EXIT_FAILURE);
    }
    if ((theArray = (char**) malloc(numstr * sizeof(char*))) == NULL) {
        fprintf(stderr, "Cannot allocate memory for theArray\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < numstr; i++) {
        if ((theArray[i] = (char*) malloc(lenstr * sizeof(char))) == NULL) {
            fprintf(stderr, "Cannot allocate memory for theArray[%d]\n", i);
            exit(EXIT_FAILURE);
        }
        
        // Initialize the string by filling it with '\0'
        memset(theArray[i], INIT_VAL, lenstr * sizeof(char*));
    }

    if (pthread_mutex_init(&mutex_rwlock, NULL) != 0) {
        fprintf(stderr, "Cannot create mutex_rwlock\n");
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&cond_rlock, NULL) != 0) {
        fprintf(stderr, "Cannot create cond_rlock\n");
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&cond_wlock, NULL) != 0) {
        fprintf(stderr, "Cannot create cond_wlock\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < numcli; i++) {
        if (pthread_mutex_init(&mutex_recvlock[i], NULL) != 0) {
            fprintf(stderr, "Cannot create mutex_recvlock[%d\n", i);
            exit(EXIT_FAILURE);
        }
        if (pthread_mutex_init(&mutex_translock[i], NULL) != 0) {
            fprintf(stderr, "Cannot create mutex_translock[%d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    readers = writers = pending_writers = 0;

    /* Initialize the time array to all 0 */
    memset(timeArray, 0, numthr * sizeof(double));

    sockvar.sin_addr.s_addr = inet_addr(ipaddr);
    sockvar.sin_port = portnum;
    sockvar.sin_family = AF_INET;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Cannot create a server socket with IPADDRESS = \'%s\' and PORT = %d\n", ipaddr, portnum);
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr*) &sockvar, sizeof(sockvar)) < 0) {
        fprintf(stderr, "Cannot bind server socket with IPADDRESS = \'%s\', PORT = %d\n", ipaddr, portnum);
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, COM_NUM_REQUEST * 2) < 0) {
        fprintf(stderr, " Cannot start listening for %d requests\n", COM_NUM_REQUEST);
        exit(EXIT_FAILURE);
    }

    // while (1) { // Loop indefinitely: Not needed for now, kept here for potential improvement
        for (thRank = 0; thRank < numthr; thRank++) {
            if ((clientdesc[thRank] = accept(sockfd, NULL, NULL)) < 0) {
                fprintf(stderr, "Cannot establish connection to client number %ld\n", thRank);
                exit(EXIT_FAILURE);
            }

            if (pthread_create(&thrID[thRank], NULL, request_handler, (void*) thRank) != 0) {
                fprintf(stderr, "Cannot create thread %ld for client %d\n", thRank, clientdesc[thRank]);
                exit(EXIT_FAILURE);
            }

        }
    //}

    /* Wait for all thread to terminate */
    for (int i = 0; i < numthr; i++) {
        if (pthread_join(thrID[i], NULL) != 0) {
            fprintf(stderr, "Cannot wait for thread %d to terminate\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /* The time length is the number of nonzero element in the time array */
    for (timeLength = 0; timeLength < numthr; timeLength++) {
        if (timeArray[timeLength] <= 0) break;
    }

    saveTimes(timeArray, timeLength); // Save the average access time


    if (close(sockfd) != 0) {
        fprintf(stderr, "Could not close the connection to the server! Retrying...\n");
        if (shutdown(sockfd, SHUT_RDWR) < 0) { // Forced shutdown attempt
            fprintf(stderr, "Could not shut down the server\n");
            exit(EXIT_FAILURE);
        }
    }

    if (pthread_mutex_destroy(&mutex_rwlock) != 0) {
        fprintf(stderr, "Could not destroy mutex_rwlock\n");
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_destroy(&cond_rlock) != 0) {
        fprintf(stderr, "Could not destroy cond_rlock\n");
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_destroy(&cond_wlock) != 0) {
        fprintf(stderr, "Could not destroy cond_wlock\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < numcli; i++) {
        if (pthread_mutex_destroy(&mutex_recvlock[i]) != 0) {
            fprintf(stderr, "Could not destroy mutex_recvlock[%d]\n", i);
            exit(EXIT_FAILURE);
        }
        if (pthread_mutex_destroy(&mutex_translock[i]) != 0) {
            fprintf(stderr, "Could not destroy mutex_translock[%d]\n", i);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < numstr; i++) {
        free(theArray[i]);
    }

    free(thrID);
    free(theArray);
    free(timeArray);
    free(clientdesc);
    free(mutex_recvlock);
    free(mutex_translock);

    return 0;
}