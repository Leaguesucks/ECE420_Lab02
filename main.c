/*
    * main.c:
    *   The main server, process read and write requests from clients and measures and records the average time for each 1000 requests
    * 
    * Run as: ./main                                       -- Default run, save efforts and useful for debug: arraylen = 1000, serverIP = 127.0.0.1, port = 3000
    *         ./main <array len> <serverIP> <server port>  -- Arguments execution
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
#include <math.h>
#include <signal.h>

#include "timer.h"
#include "common.h"

/* For printing colored text (debugging or style purpose)
 *
 * USAGE: COLOR(color) "String" COLOR(RESET)              
 */
#define COLOR(code) "\033[" code "m"

#define BLACK   "30"
#define RED     "31"
#define GREEN   "32"
#define YELLOW  "33"
#define BLUE    "34"
#define MAGENTA "35"
#define CYAN    "36"
#define WHITE   "37"
#define RESET   "0"
/* ****************************************************** */

#define IPADDRESS "127.0.0.1"             // Default IP Address
#define PORT   3000                       // Default port number
#define NUMSTR 1000                       // Default number of string in the main array
#define STRLEN 1024                       // Default length of each string in the main array

char             **theArray;              // The main array: for read and write
double           *timeArray;              // An array to hold the time it takes to process each request
int              timeLength;              // Length of the time array, also = the number of request that has been issued
char             ipaddr[20];              // The server IP address
short int        portnum;                 // Port number is represented by a 16 bits integer
int              numstr, lenstr, numthr;  // Number of string, length of each string in the main array, and number of thread created (= num of request for now)
int              numcli;                  // Number of client
int              resqno;                  // Number of request that has been issued
int              sockfd;                  // Server socket descriptor

pthread_mutex_t  mutex_rwlock;            // Read/Write lock mutex
pthread_cond_t   cond_rlock;              // Read lock
pthread_cond_t   cond_wlock;              // Write lock
int              readers;                 // Number of current readers
int              writers;                 // Number of current writer. THERE SHOULD ONLY BE 1 WRITER AND 0 READERS AT ANY GIVEN MOMENT
int              pending_writers;         // Number of writers waiting to write

pthread_t        *thrID;                  // An array to store the threads ID

/* The thread function
 * 
 * Para: The request descriptor
 */
void *request_handler(void* arg) {    
    long          cfd;                                   // This thread request descriptor
    ClientRequest creqst;                                // To store the processed client requests
    char          request[lenstr + COM_BUFF_SIZE + 50];  // Request sent from client
    char          response[lenstr + COM_BUFF_SIZE + 50]; // Response to send back to the client (50 spare bytes added)
    char          msg[lenstr];                           // String that stored content from the array
    double        start, end;                            // For measuring the array accesing time
    int           rev;                                   // Handle receive error
    int           err;                                   // Handle errors when errno is set
    int           rank;                                  // This thread rank, = current number of request

    cfd = (long) arg;

    /* Initialize all char array */
    memset(request , '\0', (lenstr + COM_BUFF_SIZE + 50) * sizeof(char));
    memset(response, '\0', (lenstr + COM_BUFF_SIZE + 50) * sizeof(char));
    memset(msg     , '\0',  lenstr                       * sizeof(char));

    if ((rev = recv(cfd, request, COM_BUFF_SIZE * sizeof(char), 0)) < 0) {
        err = errno;
        fprintf(stderr, COLOR(RED)"Cannot read request %ld\n"COLOR(RESET), cfd);
        fprintf(stderr, COLOR(RED)"Errno %d:\'%s\'\n"COLOR(RESET), err, strerror(err));
        exit(EXIT_FAILURE);
    }
    else if (rev == 0) { // Client has shut its communication
        pthread_exit(NULL);
        return NULL; // To make very damn sure that this thread terminate
    }

    // Process the client requests
    if (ParseMsg(request, &creqst) != 0) {
        fprintf(stderr, COLOR(RED)"Thread cannot process client request %ld: \'%s\'\n"COLOR(RESET), cfd, request);
        exit(EXIT_FAILURE);
    }

    if (creqst.is_read) { // It is a read operation
        pthread_mutex_lock(&mutex_rwlock);

        if (resqno >= numthr) resqno = 0; // Extra protection
        rank = resqno;
        resqno++; // Take advantages of the mutex, increment number of request
        

        GET_TIME(start); // Start the timer

        while (writers > 0 || pending_writers > 0) { // Cannot read when there are writers
            pthread_cond_wait(&cond_rlock, &mutex_rwlock); // Wait for read lock to be released
        }

        /* Granted read lock, proceed to do some read operations */
        readers++;
        pthread_cond_broadcast(&cond_rlock); // Wake up all readers
        getContent(msg, creqst.pos, theArray); // Get the content from the array

        /* Finish reading, now decrement the read count */
        if (readers > 0) readers--;

        GET_TIME(end); // Finish the timer

        timeArray[rank] = end - start;

        if (write(cfd, theArray[creqst.pos], COM_BUFF_SIZE) < 0) {
            fprintf(stderr, COLOR(RED)"Thread fail transmit response %ld \n"COLOR(RESET), cfd);
            exit(EXIT_FAILURE);
        }

        pthread_mutex_unlock(&mutex_rwlock);
    }
    else { // It is a write operation
        pthread_mutex_lock(&mutex_rwlock);

        if (resqno >= numthr) resqno = 0; // Extra protection
        
        rank = resqno;
        resqno++; // Take advantages of the mutex, increment number of request

        GET_TIME(start); // Start the timer

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

        GET_TIME(end); // Finish the timer

        timeArray[rank] = end - start;

        if (write(cfd, theArray[creqst.pos], COM_BUFF_SIZE) < 0) {
            fprintf(stderr, COLOR(RED)"Thread fail transmit response %ld \n"COLOR(RESET), cfd);
            exit(EXIT_FAILURE);
        }

        pthread_mutex_unlock(&mutex_rwlock);
    }

    if (close(cfd) < 0) {
        fprintf(stderr, COLOR(RED)"Cannot close descriptor %ld, trying to shutdown...\n"COLOR(RESET), cfd);

        if (shutdown(cfd, SHUT_WR) < 0) {
            fprintf(stderr, COLOR(RED)"Cannot shut the descriptor %ld down\n"COLOR(RESET), cfd);
            exit(EXIT_FAILURE);
        }
    }

    pthread_exit(NULL);
    return NULL; // To make very damn sure that this thread exit
}

/*
 *  Check the program arguments
 *
 *  Param : args num, args
 *  Return: 0 if OK, negative number otherwise
 */
int CheckArgs(int argc, char* argv[]) {
    errno = 0;

    if (argc == 1) {  //default values when running main
        strncpy(ipaddr, IPADDRESS, sizeof(ipaddr) - 1);
        ipaddr[sizeof(ipaddr) - 1] = '\0';
        portnum = PORT;
        numstr = NUMSTR;
        lenstr = STRLEN;
        numthr = COM_NUM_REQUEST;
        numcli = COM_CLIENT_THREAD_COUNT;
        return 0;
    }

    else if (argc != 4) {  // checks the number of arguments
        fprintf(stderr, "Usage: %s <array len> <server IP> <server port>\n", argv[0]);
        return -1;
    }

    // Parse array length
    char *endptr;
    long result = strtol(argv[1], &endptr, 10);
    if (errno == ERANGE || result > INT_MAX || result <= 0 || *endptr != '\0') {
        fprintf(stderr, COLOR(RED)"Invalid array length '%s'. Must be a positive integer (max: %d)\n"COLOR(RESET), argv[1], INT_MAX);
        return -1;
    }
    numstr = (int)result;

    // Parse and validate IP address
    if (inet_pton(AF_INET, argv[2], &ipaddr) != 1) {
        fprintf(stderr, COLOR(RED)"Invalid IP address format: %s\n"COLOR(RESET), argv[2]);
        return -1;
    }
    strncpy(ipaddr, argv[2], sizeof(ipaddr) - 1);
    ipaddr[sizeof(ipaddr) - 1] = '\0';

    // Parse port number
    errno = 0;
    result = strtol(argv[3], &endptr, 10);
    if (errno == ERANGE || result > SHRT_MAX || result <= 0 || *endptr != '\0') {
        fprintf(stderr, COLOR(RED)"Invalid port number '%s'. Must be between 1 and %d\n"COLOR(RESET), argv[3], SHRT_MAX);
        return -1;
    }
    portnum = (int) result;

    // Set other constants
    lenstr = STRLEN;
    numthr = COM_NUM_REQUEST;
    numcli = COM_CLIENT_THREAD_COUNT;

    return 0;
}

/* Handle cleaning up the server in the event of shut down (free memory, shut down connection, etc) */
void CleanUp(void) {
    printf(COLOR(BLUE)"Server shutting down...\n"COLOR(RESET));

    if (close(sockfd) != 0) {
        fprintf(stderr, COLOR(RED)"Could not close the connection to the server! Retrying...\n"COLOR(RESET));
        if (shutdown(sockfd, SHUT_RDWR) < 0) { // Forced shutdown attempt
            fprintf(stderr, COLOR(RED)"Could not shut down the server\n"COLOR(RESET));
            exit(EXIT_FAILURE);
        }
    }

    if (pthread_mutex_destroy(&mutex_rwlock) != 0) {
        fprintf(stderr, COLOR(RED)"Could not destroy mutex_rwlock\n"COLOR(RESET));
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_destroy(&cond_rlock) != 0) {
        fprintf(stderr, COLOR(RED)"Could not destroy cond_rlock\n"COLOR(RESET));
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_destroy(&cond_wlock) != 0) {
        fprintf(stderr, COLOR(RED)"Could not destroy cond_wlock\n"COLOR(RESET));
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < numstr; i++) {
        free(theArray[i]);
    }

    free(thrID);
    free(theArray);
    free(timeArray);

    printf(COLOR(BLUE)"Server shut down successfully\n"COLOR(RESET));
}

/* Handle quiting the server when user issue CTRL-C 
 * 
 * Param: The signal, in this case SIGINT from CTRL-C
 */
void ForceQuit(int signo) {
    printf(COLOR(RED)"\nForce quit server.\nCleaning up server, please wait 3 seconds...\n"COLOR(RESET));
    
    sleep(3); // Give all thread a chance to finish (has to be hard coded for now, somehow pthread_join causes seg fault)

    saveTimes(timeArray, resqno); // Save time on the last run

    CleanUp();
    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
    long               resqdes;    // The request descriptor to pass to each thread
    struct sockaddr_in sockvar;    // Contains IP address, port number

    if (signal(SIGINT, ForceQuit) == SIG_ERR) {
        fprintf(stderr, COLOR(RED)"Cannot force quit the server\n"COLOR(RESET));
        exit(EXIT_FAILURE);
    }

    if (CheckArgs(argc, argv) < 0) exit(EXIT_FAILURE); // Process the arguments

    errno = 0; // Set to 0 to handle errors
    resqno = 0;

    printf("Server: \'%s\', IPADDRESS = \'%s\', PORT = %d\n", argv[0], ipaddr, portnum);
    printf("theArray length = %d\n", numstr);

    if ((thrID = (pthread_t*) malloc(numthr * sizeof(pthread_t))) == NULL) {
        fprintf(stderr, COLOR(RED)"Cannot allocate memory for thrID\n"COLOR(RESET));
        exit(EXIT_FAILURE);
    }
    if ((timeArray = (double*) malloc(numthr * sizeof(double))) == NULL) {
        fprintf(stderr, COLOR(RED)"Cannot allocate memory for timeArray\n"COLOR(RESET));
        exit(EXIT_FAILURE);
    }
    if ((theArray = (char**) malloc(numstr * sizeof(char*))) == NULL) {
        fprintf(stderr, COLOR(RED)"Cannot allocate memory for theArray\n"COLOR(RESET));
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < numstr; i++) {
        if ((theArray[i] = (char*) malloc(lenstr * sizeof(char))) == NULL) {
            fprintf(stderr, COLOR(RED)"Cannot allocate memory for theArray[%d]\n"COLOR(RESET), i);
            exit(EXIT_FAILURE);
        }
        
        // Initialize the string
        snprintf(theArray[i], lenstr, COLOR(BLUE)"String [%d]: the initial value"COLOR(RESET), i);
    }

    if (pthread_mutex_init(&mutex_rwlock, NULL) != 0) {
        fprintf(stderr, COLOR(RED)"Cannot create mutex_rwlock\n"COLOR(RESET));
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&cond_rlock, NULL) != 0) {
        fprintf(stderr, COLOR(RED)"Cannot create cond_rlock\n"COLOR(RESET));
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&cond_wlock, NULL) != 0) {
        fprintf(stderr, COLOR(RED)"Cannot create cond_wlock\n"COLOR(RESET));
        exit(EXIT_FAILURE);
    }

    readers = writers = pending_writers = 0;

    sockvar.sin_addr.s_addr = inet_addr(ipaddr);
    sockvar.sin_port = portnum;
    sockvar.sin_family = AF_INET;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, COLOR(RED)"Cannot create a server socket with IPADDRESS = \'%s\' and PORT = %d\n"COLOR(RESET), ipaddr, portnum);
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr*) &sockvar, sizeof(sockvar)) < 0) {
        fprintf(stderr, COLOR(RED)"Cannot bind server socket with IPADDRESS = \'%s\', PORT = %d\n"COLOR(RESET), ipaddr, portnum);
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, COM_NUM_REQUEST * 2) < 0) {
        fprintf(stderr, COLOR(RED)"Cannot start listening for %d requests\n"COLOR(RESET), COM_NUM_REQUEST);
        exit(EXIT_FAILURE);
    }

    while (1) { // Loop indefinitely:
        resqno = 0;

        /* Initialize the time array to all 0 */
        memset(timeArray, 0, numthr * sizeof(double));

        /* Waiting for the clients to establish connection */
        for (int i = 0; i < numthr; i++) {
            if ((resqdes = accept(sockfd, NULL, NULL)) < 0) {
                fprintf(stderr, COLOR(RED)"Cannot establish connection to request number %d\n"COLOR(RESET), i);
                exit(EXIT_FAILURE);
            }
            
            /* Create the threads needed to handle each client connection */
            if (pthread_create(&thrID[i], NULL, request_handler, (void*) resqdes) != 0) {
                fprintf(stderr, COLOR(RED)"Cannot create thread %d or client %ld\n"COLOR(RESET), i, resqdes);
                exit(EXIT_FAILURE);
            }
        } 

        /* Wait for all thread to terminate */
        for (int i = 0; i < numthr; i++) {
            if (pthread_join(thrID[i], NULL) != 0) {
                fprintf(stderr, COLOR(RED)"Cannot wait for thread %d to terminate\n"COLOR(RESET), i);
                exit(EXIT_FAILURE);
            }
        }

        saveTimes(timeArray, resqno); // Save the average access time
    }

    CleanUp();

    return 0;
}