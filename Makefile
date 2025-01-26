
####################################################################################################################################################
# Copy the demos files into the main folder and compile them

# Compile the demo files
alldemos: arrayRW simpleClient simpleServer

# Copy files from demos folder to the main folder
mkdemos:
	cp demos/* .

simpleServer: simpleServer.o
	gcc -Wall -pthread -g simpleServer.o -o simpleServer -lm

simpleServer.o: simpleServer.c
	gcc -Wall -pthread -g -c simpleServer.c -o simpleServer.o

simpleClient: simpleClient.o
	gcc -Wall -pthread -g simpleClient.o -o simpleClient -lm

simpleClient.o: simpleClient.c
	gcc -Wall -pthread -g -c simpleClient.c -o simpleClient.o

arrayRW: arrayRW.o
	gcc -Wall -pthread -g arrayRW.o -o arrayRW -lm

arrayRW.o: arrayRW.c timer.h common.h
	gcc -Wall -pthread -g -c arrayRW.c -o arrayRW.o

# Clean the demos executable and object files
cleandemos:
	rm simpleClient.o simpleServer.o arrayRW.o simpleClient simpleServer arrayRW

# Remove all demos files from the main folder
cleardemos:
	rm simpleClient.o simpleServer.o arrayRW.o simpleClient simpleServer arrayRW Icon_ arrayRW.c simpleClient.c simpleServer.c

#################################################################################################################################################