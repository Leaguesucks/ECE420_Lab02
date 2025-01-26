/*
    * main.c:
    *   The main server
    * 
    * Run as: ./main
    *         ./main <array len> <serverIP> <server port>                       -- MAIN REQUIREMENT: Focus on this for now
    *         ./main <array len> <serverIP> <server port> <strlen>              -- OPTIONAL
    *         ./main <array len> <serverIP> <server port> <strlen> <thread num> -- OPTIONAL
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

char      **theArray;              // The main array: for read and write
double    *timeArray;              // An array to hold the time it takes to process each reques
char      ipaddr[20];              // The server IP address
short int portnum;                 // Port number is represented by a 16 bits integer
int       numstr, lenstr, numthr;  // Number of string, length of each string in the main array, and number of thread created (= num of client)
int       resqnum;                 // To keep track of the number of request

/* The thread function */
void* thr_fn(void* arg) {
    int cfd = (int) arg; // Assigned the client descriptor to this thread

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
            numthr = COM_CLIENT_THREAD_COUNT;
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
            result2 = strtol(argc[3], &endptr1, 10);

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
                fprintf(stderr, "Array length = %d: OVERFLOW! MAX <array len> = %d\n", result1, INT_MAX);
                OK--;
            }
            if (result1 <= 0) {
                fprintf(stderr, "Array length cannot be negative or 0\n");
                OK--;
            }

            if (result2 > SHRT_MAX) {
                fprintf(stderr, "Port number = %d to large! MAX PORT = %d\n", result2, SHRT_MAX);
                OK--;
            }
            if (result2 < 0) {
                fprintf(stderr, "Port number cannot be negative\n");
                OK--;
            }

        lenstr = STRLEN;
        numthr = COM_CLIENT_THREAD_COUNT;

        break;

        default:
            fprintf(stderr, "Number of argument = %d is invalid! Must be 0 or 3\n", argv - 1);
            OK--;    
    }

    return OK;
}

int main(int argv, char* argc[]) {
    int                sockfd;     // Socket descriptor
    int                *clientfd;  // An array to hold the client descriptors
    struct sockaddr_in sockvar;    // Contains IP address, port number
    
    ClientRequest      creqst;     // To store the client requests

    pthread_t          *thrID;     // An array to store the threads ID

    if (CheckArgs(argv, argc) < 0) exit(EXIT_FAILURE); // Process the arguments

    printf("Server: \'%s\', IPADDRESS = \'%s\', PORT = %d\n", argc[0], ipaddr, portnum);

    if ((thrID = (pthread_t*) malloc(numthr * sizeof(pthread_t))) == NULL) {
        fprintf(stderr, "Cannot allocate memory for thrID\n");
        exit(EXIT_FAILURE);
    }
    if ((clientfd = (int*) malloc(numthr * sizeof(int))) == NULL) {
        fprintf(stderr, "Cannot allocate memory for clientfd\n");
        exit(EXIT_FAILURE);
    }
    if ((timeArray = (double*) malloc(numthr * sizeof(double))) == NULL) {
        fprintf(stderr, "Cannot allocate memory for timeArray\n");
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
        memset(theArray[i], INIT_VAL, sizeof(theArray[i]));
    }

    resqnum = 0;

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

    while (1) { // Loop indefinitely
        for (int i = 0; i < numthr; i++) {
            clientfd[i] = accept(sockfd, NULL, NULL);
            
            if (clientfd[i] < 0) {
                fprintf(stderr, "Cannot establish connection to client number %d\n", i);
                exit(EXIT_FAILURE);
            }

            if (pthread_create(&thrID[i], NULL, thr_fn, (void*) clientfd[i]) != 0) {
                fprintf(stderr, "Cannot create thread %d for client %d\n", i, clientfd[i]);
                exit(EXIT_FAILURE);
            }

        }
    }

    if (close(sockfd) != 0) {
        fprintf(stderr, "Could not shut down the server! Retrying...\n");
        if (shutdown(sockfd, SHUT_RDWR) < 0) { // Forced shutdown attempt
            fprintf(stderr, "Could not shut down the server\n");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < numstr; i++) {
        free(theArray[i]);
    }

    free(thrID);
    free(clientfd);
    free(theArray);
    free(timeArray);

    return 0;
}