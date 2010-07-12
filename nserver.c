/* Networking */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>	// for gethostbyaddr() 

/* Standard */
#include <stdio.h>
#include <stdlib.h>	
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

/* Other */
#include <pthread.h>	// For using threads
#include <signal.h>		// For handling signals


/* Errors */
#define ERR_NUM_P	1	// Error in number of parameters
#define ERR_TYPE_P	2	// Error in types of parameters
#define ERR_PORT	3	// Error listening on port
#define ERR_NET		5	// Other network error
#define ERR_RULES	6	// Error in rules file

/* Log Messages */
#define LOG_START		1	// Server starts
#define LOG_STOP		2	// Server stops
#define LOG_GOOD_CON	3	// Client connects successfully
#define LOG_FULL_CON	4	// Connection attempt to full game
#define LOG_MAX_CON		5	// Connetion attempt while max games running
#define LOG_WIN			6	// Game over -- win
#define LOG_DISCON		7	// Client disconnects before game over
#define LOG_BAD_MAP		8	// Client disconnects due to bad map

/* Structures */

/* 
 * A user type contains information about a connected user
 * All users are linked as a unidirectional list
 */
typedef struct User {	// A linked list of users
	char* id;	    // Name of the user
	int disconns;	// Number of disconnects for this user
	int won;		// Number of games won for this user
	int lost;		// Number of games lost for this user
	struct User* next;
} User;

/*
 * A game type contains information about a currently running game
 * A game can have maximum 2 users playing at once.
 */
typedef struct Game {
	char* id;		// The name of this game
	User* users[2];	// References players of this game (at most two)
	int fd[2];		// The socket each user is associated with

    /* To know when game is full */
    pthread_cond_t startCond;   // To know whether to start
    pthread_mutex_t startMutex;
    bool start;
} Game;

/* Global variables */
int maxGames = 0;   // Max number of games

FILE* rules;    // The rules file
pthread_mutex_t rulesMutex; // Synchronize access to the rules file

User* userListHead = NULL;	// This will point to the first user
pthread_mutex_t userListMutex;	// Lock mutex when adding or updating users

Game** gameArray = NULL;    // Array of current games
pthread_mutex_t gameListMutex;	// Lock mutex when adding or updating games

FILE* logFile = NULL;   // The log file
pthread_mutex_t logMutex;   // Lock the log file before writing to it

/* Helper functions for Stuctures */

/* 
 * Push a new user onto the list
 */
void push_user(User** userHeadRef, char* id) {
	User* newUser = (User*)malloc(sizeof(User));

    newUser->id = (char *)malloc(sizeof(char) * (strlen(id) + 1));
	strcpy(newUser->id, id);
	newUser->disconns = 0;
	newUser->won = 0;
	newUser->lost = 0;
	newUser->next = *userHeadRef;

	pthread_mutex_lock(&userListMutex);
	*userHeadRef = newUser;
	pthread_mutex_unlock(&userListMutex);
}

/*
 * Print stats of all users in the list
 */
void print_user_stats(User* userListHead) {
	User* current = userListHead;

	while(current) {
		fprintf(stdout, "%s\t", current->id);
		fprintf(stdout, "%d\t", current->won);
		fprintf(stdout, "%d\t", current->lost);
		fprintf(stdout, "%d\n", current->disconns);
        fflush(stdout);
		current = current->next;
	}
}

/* 
 * Returns a pointer to the found user, NULL if can't be found 
 */
User* find_user(User* userListHead, char* id) {
	User* current = userListHead;
	while(current) {
		if(strcmp(current->id, id) == 0) {
			return current;
		}
		current = current->next;
	}
	return NULL;
}

/*
 * Returns true if all game slots are taken, false otherwise 
 */
bool at_max_games(Game* gameArray[], int maxGames) {
    for(int i = 0; i < maxGames; i++) {
        if(gameArray[i] == NULL) {
            return false;
        }
    }
    return true;
}

/*
 * Push a new game onto the array
 */
Game* push_game(Game** gameArray, char* id) {
	Game* newGame = (Game *)malloc(sizeof(Game));

    newGame->id = (char *)malloc(sizeof(char) * (strlen(id) + 1));
	strcpy(newGame->id, id);
	(newGame->users)[0] = NULL;
	(newGame->users)[1] = NULL;
	(newGame->fd)[0] = -1;
	(newGame->fd)[1] = -1;
    pthread_cond_init(&newGame->startCond, NULL);
    pthread_mutex_init(&newGame->startMutex, NULL);
    newGame->start = false;

	pthread_mutex_lock(&gameListMutex);
	for(int i = 0; i < maxGames; i++) {
		if(gameArray[i] == NULL) {
			gameArray[i] = newGame;
			gameArray[i+1] = NULL;
			break;
		}
	}
	pthread_mutex_unlock(&gameListMutex);
    return newGame;
}

/*
 * Remove game from the array
 */
void remove_game(Game* gameArray[], Game* game) {
    int i = 0;

    /* Find and remove game */
    for(i = 0; i < maxGames; i++) {
        if(gameArray[i] == game) {
            free(game);
            break;
        }
    }

    /* Shift down other games */
    for(int j = i; j < maxGames; j++) {
        gameArray[j] = gameArray[j + 1];
    }
}

void print_game_stats(Game* gameArray[]) {
    Game* current;
	fprintf(stdout, "\nGame Stats:\n");
    fflush(stdout);
    for(int i = 0; gameArray[i] != NULL; i++) {
        current = gameArray[i];
		fprintf(stdout, "%s\t", current->id);
		fprintf(stdout, "%p\t", (void *)current->users[0]);
		fprintf(stdout, "%p\t", (void *)current->users[1]);
		fprintf(stdout, "%d\t", current->fd[0]);
		fprintf(stdout, "%d\n", current->fd[1]);
        fflush(stdout);
    }
}

/*
 * Returns a pointer to the game if found, NULL otherwise
 */
Game* find_game(Game* gameArray[], char *id) {
    for(int i = 0; gameArray[i] != NULL; i++) {
        if(strcmp(gameArray[i]->id, id) == 0) {
            return gameArray[i];
        }
    }
    return NULL;
}

/* 
 * Returns true if game is full (has 2 users)
 * Assume game indicated by id exists 
 */
bool is_full_game(Game* gameArray[], char *id) {
    Game* theGame = find_game(gameArray, id);
    if(theGame->users[0] == NULL || theGame->users[1] == NULL) {
        return false;
    }
    return true;
}

/* Other Functions */

/* 
 * Print an error message then exit the program. 
 */
void throw_error(int code) {
	switch(code) {
		case ERR_NUM_P:
			fprintf(stderr, "Usage: nserver logfile max_games rules port\n");
			break;
		case ERR_TYPE_P:
			fprintf(stderr, "Invalid param types or values.\n");
			break;
		case ERR_PORT:
			fprintf(stderr, "Unable to listen on port.\n");
			break;
		case ERR_NET:
			fprintf(stderr, "Network error.\n");
			break;
		case ERR_RULES:
			fprintf(stderr, "Error in rules file.\n");
			break;
	}
	exit(code);
}

/* 
 * Print a message to the log file 
 * Initialize logFile with code 0.
 * Further calls can have logFile NULL to use the same
 */
void log_message(int code, FILE* logFile, char* id, char* game, int port) {
	static FILE* log;
	if(logFile != NULL) {
		log = logFile;
	}

	char message[80] = "";
	switch(code) {
		case LOG_START:
			sprintf(message, "Server started on port %d.\n", port);
            fprintf(stdout, "Server started on port %d.\n", port);
            fflush(stdout);
			break;
		case LOG_STOP:
			sprintf(message, "Server stopped.\n");
			break;
		case LOG_GOOD_CON:
			sprintf(message, "Client %s connected to game %s.\n", id, game);
			break;
		case LOG_FULL_CON:
			sprintf(message, "Rejected %s from full game %s.\n", id, game);
			break;
		case LOG_MAX_CON:
			sprintf(message, "Rejected %s due to too many games.\n", id);
			break;
		case LOG_WIN:
			sprintf(message, "%s won game %s.\n", id, game);
			break;
		case LOG_DISCON:
			sprintf(message, "%s disconnected from game %s.\n", id, game);
			break;
		case LOG_BAD_MAP:
			sprintf(message, "%s disconnected due to bad map.\n", id);
			break;
	}

	if(log != NULL) {
		pthread_mutex_lock(&logMutex);
		fprintf(log, "%s", message);	// Print to log file if it exists
        fflush(log);
		pthread_mutex_unlock(&logMutex);
	}
}

/* 
 * Open a socket for the server to listen on 
 */
int open_listen(int port)
{
    int fd;	// The connection point for the server
    struct sockaddr_in serverAddr;	// The address of the server
    int optVal = 1;	// Allow immidiate reuse of address	

    /* Create a socket - Internet - TCP */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
		throw_error(ERR_NET);
    }

    /* Set option on socket to immediately reuse address - otherwise
    ** we can get an address in use error until timeout (after exit) 
    */
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(int)) < 0) {
		throw_error(ERR_NET);
    }

    /* Create address structure for the adddress we're listening on */
    serverAddr.sin_family = AF_INET;	// Internet address family 
    serverAddr.sin_port = htons(port);	// Port number 
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);	// Any IP address

    /* Bind the socket to this address - note the cast of address types */
    if(bind(fd, (struct sockaddr*)&serverAddr, 
                sizeof(struct sockaddr_in)) < 0){
		throw_error(ERR_PORT);
    }

    /* Start listening for incoming connections. Second argument is
    ** the maximum number of unaccepted connection requests that will be
    ** queued by the OS. Agave value SOMAXCONN is 128.
    */
    if(listen(fd, SOMAXCONN) < 0) {
		throw_error(ERR_NET);
    }

    return fd;
}

/* 
 * Parse client handshake 
 */
bool parse_handshake(int fd, char** user, char** game) {
    ssize_t numBytesRead;
    char buffer[1024];
    while((numBytesRead = read(fd, buffer, 1024)) > 0) {
        if(sscanf(buffer, "$handshake %s %s", *user, *game) == 2) {
            return true; // Got it
        }
        continue; // Try again
    }
    return false; // Something's wrong
}

/*
 * Sends rules to client, parses response.
 * Returns 1 if OK
 * Returns 2 if Bad Map
 * Returns 0 if Connection Error
 */
int parse_map(int fd, FILE* rules) {
    FILE* clientSend = fdopen(fd, "w");
    FILE* clientGet = fdopen(fd, "r");

    /* Tell client to get ready for rules */
    fprintf(clientSend, "$startrules\n");
    fflush(clientSend);

    /* Send the rules */
    char rule[80];
    pthread_mutex_lock(&rulesMutex);
    while(fgets(rule, 80, rules) != NULL) {
        fprintf(clientSend, rule);
        fflush(clientSend);
    }
    fseek(rules, 0, SEEK_SET);
    pthread_mutex_unlock(&rulesMutex);

    /* Tell client rules complete */
    fprintf(clientSend, "$endrules\n");
    fflush(clientSend);

    char mapStatus[80];
    while(fgets(mapStatus, 80, clientGet) != NULL) {
        if(strcmp(mapStatus, "$map good\n") == 0) {
            return 1; // Good map
        } else if(strcmp(mapStatus, "$map bad\n") == 0) {
            return 2; // Bad map
        }
        continue; // Try again
    }
    return 0; // Something's wrong
}

/*
 * Called when a user disconnects 
 */
void handle_disconnect(int fd, User* user, Game* game, bool first ) {
    /* Close the appropriate socket */
    close(fd);
    
    /* Increase disconnects for appropriate user */
    if(user != NULL) {
        pthread_mutex_lock(&userListMutex);
        user->disconns++;
        pthread_mutex_unlock(&userListMutex);
    }

    if(game != NULL) {
        /* Remove the game */
        remove_game(gameArray, game);
        pthread_mutex_unlock(&gameListMutex);
    }
}

/*
 * Called by the user who lost the game
 */
void handle_loss(Game* game, bool first) {
    int playerNum = first ? 0 : 1;
    int opponentNum = first ? 1 : 0;

    pthread_mutex_lock(&gameListMutex);
    /* Disconnect both players */
    close(game->fd[playerNum]);
    close(game->fd[opponentNum]);

    /* Increase win/loss for appropriate player */
    game->users[playerNum]->lost++;
    game->users[opponentNum]->won++;

    /* Log win */
    log_message(LOG_WIN, NULL, game->users[opponentNum]->id, game->id, 0);

    /* Remove the game */
    remove_game(gameArray, game);

    pthread_mutex_unlock(&gameListMutex);
}

/*
 * Allows communication between clients.
 * Returns 1 if player lost
 * Returns 0 if player disconnected
 * Returns -1 if opponent disconnected
 */
int parse_communication(Game* myGame, bool first) {
    int playerNum = first ? 0 : 1;
    int opponentNum = first ? 1 : 0;
    int fdPlayer = myGame->fd[playerNum];
    int fdOpponent = myGame->fd[opponentNum];

    FILE* playerGet = fdopen(fdPlayer, "r");
    FILE* opponentSend = fdopen(fdOpponent, "w");

    bool turn = first;
    char message[80];

    /* Keep reading more input until otherwise */
    while(1) {
        if(turn) {
            if(fgets(message, 80, playerGet) != NULL) {
                if(strcmp(message, "$response over\n") == 0) {
                    fprintf(opponentSend, message);
                    if(fflush(opponentSend) == EOF) {
                        return -1;
                    }
                    return 1;
                }

                fprintf(opponentSend, "awef\n");    //Don't know why this
                fflush(opponentSend);               //is necessary...

                fprintf(opponentSend, message);
                if(fflush(opponentSend) == EOF) {
                    return -1;
                }
            } else {
                return 0;
            }

            /* Signal next player to go */
            pthread_mutex_lock(&myGame->startMutex);
            turn = !turn;
            pthread_cond_signal(&myGame->startCond);
            pthread_mutex_unlock(&myGame->startMutex);
        } else {
            /* Player waits for signal that it's his turn */
            pthread_mutex_lock(&myGame->startMutex);
            pthread_cond_wait(&myGame->startCond, &myGame->startMutex);
            turn = !turn;
            pthread_mutex_unlock(&myGame->startMutex);
        }
    }
}

/*
 * The thread for interacting with clients
 */
void* client_thread(void* arg) {
    int fd;
    bool first;   // 0 or 1 depending on whether first or second player
    User* me = NULL;
    Game* myGame = NULL;

    fd  = (int)arg;
	char* id  = (char *)malloc(sizeof(char) * 80);
	char* game = (char *)malloc(sizeof(char) * 80);

    /* Get info about new player */
	if(parse_handshake(fd, &id, &game)) {
	    /* Push user if isn't already in the list */
        me = find_user(userListHead, id);
    	if(me == NULL) {
	    	push_user(&userListHead, id);
            me = userListHead;
	    }

        /* Check for good/bad map */
        int mapStatus = parse_map(fd, rules);
		if(mapStatus == 1) {

            /* Add user to game */
            if((myGame = find_game(gameArray, game)) == NULL) {
                /* Create new game if not at max games */
                if(!at_max_games(gameArray, maxGames)) {
                    myGame = push_game(gameArray, game);
                    myGame->users[0] = me;
                    myGame->fd[0] = fd;
                    first = true;
                    log_message(LOG_GOOD_CON, NULL, id, game, 0);

                    /* Wait for second player */
                    pthread_mutex_lock(&myGame->startMutex);
                    while(myGame->start == false) {
                        fflush(stdout);
                        pthread_cond_wait(&myGame->startCond, 
                                &myGame->startMutex);
                        fflush(stdout);
                    }
                    pthread_mutex_unlock(&myGame->startMutex);

                    /* Send $yourmove to first player */
                    write(fd, "$yourmove\n", 11);
                } else {
                    log_message(LOG_MAX_CON, NULL, id, NULL, 0);
                    handle_disconnect(fd, NULL, NULL, -1);
                    fflush(stdout);
                    pthread_exit(NULL);
                    return NULL;

                }
            } else {
                /* Add to current game if game not full */
                if(!is_full_game(gameArray, game)) {
                    /* Set second player */
                    myGame->users[1] = me;
                    myGame->fd[1] = fd;
                    first = false ;
                    log_message(LOG_GOOD_CON, NULL, id, game, 0);

                    /* Signal first player */
                    pthread_mutex_lock(&myGame->startMutex);
                    pthread_cond_signal(&myGame->startCond);
                    myGame->start = true;
                    pthread_mutex_unlock(&myGame->startMutex);
                } else {
                    log_message(LOG_FULL_CON, NULL, id, game, 0);
                    handle_disconnect(fd, NULL, NULL, -1);
                    fflush(stdout);
                    pthread_exit(NULL);
                    return NULL;
                }
            }

            /* Parse further input */
            int communicationStatus = parse_communication(myGame, first);
            if(communicationStatus == 1) {
                /* I lost */
                handle_loss(myGame, first);
                fflush(stdout);
                pthread_exit(NULL);
                return NULL;
            } else if(communicationStatus == 0) {
                /* I disconnected */
                User* opponent = myGame->users[first ? 1 : 0];
                handle_disconnect(fd, opponent, myGame, first);
                log_message(LOG_DISCON, NULL, opponent->id, id, 0); 
            } else if(communicationStatus == -1) {
                /* Opponent disconnected on my turn */
                handle_disconnect(fd, me, myGame, first);
                log_message(LOG_DISCON, NULL, me->id, id, 0); 
            }

        } else if(mapStatus == 2) {
            log_message(LOG_BAD_MAP, NULL, id, NULL, 0);
        }
    }

    /* Disconnection catch-all */
    handle_disconnect(fd, NULL, NULL, -1);
    fflush(stdout);
    pthread_exit(NULL);
    return NULL;
}

/* 
 * Listen for new connections and send them off to new threads
 */
void process_connections(int fdServer) {
    int fd;	// Newly accepted connection end-point
    struct sockaddr_in fromAddr;	// Address of newly acccepted connection
    unsigned int fromAddrSize; 
    struct hostent *hp; 
    pthread_t thread_id;

    /* Accept new client connections until server is terminted */
    while(1) {
		fromAddrSize = sizeof(struct sockaddr_in);
		
		/* Accept a connection - wait if none are pending */
		/* Note that fd is a new one */
		fd = accept(fdServer, (struct sockaddr*)&fromAddr,  &fromAddrSize);
		if(fd < 0) {
			throw_error(ERR_NET);
		}
		 
		/* Determine where the connection request came from and print it out */
		hp = gethostbyaddr((char *)&fromAddr.sin_addr.s_addr,
				    sizeof(fromAddr.sin_addr.s_addr), AF_INET);
		
		pthread_create(&thread_id, NULL, client_thread, (void*)fd);
		pthread_detach(thread_id);
    }
}

/* 
 * Handle various signals 
 */
void handle_sigs(int sigNum) {
	switch(sigNum) {
		case SIGINT:
			log_message(LOG_STOP, NULL, NULL, NULL, 0);
			exit(0);
	}
}

/* 
 * Handle SIGHUP 
 */
void* hup_handler(void *arg) {
    sigset_t* new = (sigset_t *)arg;
    while(1) {
        sigwait(new);
        print_user_stats(userListHead);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
	/* Catch SIGINT to log message */
	signal(SIGINT, handle_sigs);	// Trying to stop the server

    /* Ignore SIGPIPE */
    struct sigaction pipeSigAction;
    pipeSigAction.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &pipeSigAction, NULL); //Assume it works

    /* Block SIGHUP for threads */
    sigset_t new;
    sigemptyset(&new);
    sigaddset(&new, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &new, NULL);
    pthread_t hupThreadID;
    pthread_create(&hupThreadID, NULL, hup_handler, (void *)&new);
    pthread_detach(hupThreadID);

    if(argc != 5) {
		throw_error(ERR_NUM_P);
    }

	/* Open log file */
	if((logFile = fopen(argv[1], "w")) == NULL) {
        throw_error(ERR_TYPE_P);
    }
	log_message(0, logFile, NULL, NULL, 0); // Initialize the log function
	pthread_mutex_init(&logMutex, NULL);

	/* Parse max number of games */
	if(sscanf(argv[2], "%d", &maxGames) != 1 || maxGames <= 0) {
		throw_error(ERR_TYPE_P);	
	}

	/* Set max number of games and setup (null terminated) array */
	gameArray = (Game **)malloc(sizeof(Game *) * (maxGames + 1));
	gameArray[0] = NULL;

	/* Assume if rules can be opened, it is valid */
	if((rules = fopen(argv[3], "r")) == NULL) {
		throw_error(ERR_RULES);
	}
    pthread_mutex_init(&rulesMutex, NULL);
	
    /* Convert our ASCII port number to an integer */
    int portnum;
	if(sscanf(argv[4], "%d", &portnum) != 1 || 
            portnum < 0 || portnum > 65535) {
		throw_error(ERR_TYPE_P);
    }

    int fdServer = open_listen(portnum); // Open a socket for the server
	log_message(LOG_START, NULL, NULL, NULL, portnum);

    process_connections(fdServer); // Wait for connections
    return 0;
}
