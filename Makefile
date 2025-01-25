# Compile the demo file
demos: arrayRW simpleClient simpleServer

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

clean:
	rm *.o simpleClient simpleServer arrayRW