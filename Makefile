all: main client attacker

main: main.o
	gcc -Wall -pthread -g main.o -o main -lm

main.o: main.c timer.h common.h
	gcc -Wall -pthread -g -c main.c -o main.o


client: client.o
	gcc -Wall -pthread -g client.o -o client -lm

client.o: client.c common.h
	gcc -Wall -pthread -g -c client.c -o client.o


attacker: attacker.o
	gcc -Wall -pthread -g attacker.o -o attacker -lm

attacker.o: attacker.c common.h
	gcc -Wall -pthread -g -c attacker.c -o attacker.o

cleanout:
	rm server_output_time_aggregated

clean:
	rm *.o attacker client main server_output_time_aggregated

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

zipall:
	zip ECE420_Lab02_AllFiles *

zipmain:
	zip ECE420_Lab02 Makefile main.c

cleanzip:
	rm *.zip