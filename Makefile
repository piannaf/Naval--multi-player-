CC = gcc
CFLAGS = -Wall -std=gnu99 -pedantic
CFLAGS_AGAVE = -lsocket -lnsl
CFLAGS_LINUX = -lpthread
OBJECTS_CLIENT = nclient.o #ass1solution.o
OBJECTS_SERVER = nserver.o

all: nclient nserver

allLinux: nclientLinux nserverLinux

nclient: $(OBJECTS_CLIENT)
	$(CC) -o $@ $^ $(CFLAGS) $(CFLAGS_AGAVE)

nserver: $(OBJECTS_SERVER)
	$(CC) -o $@ $^ $(CFLAGS) $(CFLAGS_AGAVE)

nserverLinux: $(OBJECTS_SERVER) 
	$(CC) -o $@ $^ $(CFLAGS) $(CFLAGS_LINUX)

nclientLinux: $(OBJECTS_CLIENT)
	$(CC) -o $@ $^ $(CFLAGS) $(CFLAGS_LINUX)

debugServerLinux: $(OBJECTS_SERVER) 
	$(CC) -o $@ $^ $(CFLAGS) $(CFLAGS_LINUX) -g

debugServer: $(OBJECTS_SERVER) 
	$(CC) -o $@ $^ $(CFLAGS) $(CFLAGS_AGAVE) -g

clean:
	rm -r *.o
remove:
	rm nclient
	rm nserver
