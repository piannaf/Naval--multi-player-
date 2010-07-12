/* Standards */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Networking */
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

typedef enum {
	OK = 0,         // USE
	BAD_CMD = 10,   // USE
    BAD_PARAM,      // USE
	NO_RULES = 20,
	NO_MAP = 30,    // USE
	BAD_RULES = 40,
	OVL_MAP = 50,   // USE
	BOU_MAP,        // USE
	BAD_MAP,        // USE
	END_INPUT = 60, // -> OK
	MISS,
	HIT,
	SUNK,
	ALL_SUNK,
	INVALID_GUESS,
    GO_WIN = 70,    // USE
    GO_LOSS,        // USE
    GO_DISCONN,     // USE
    CONN_REF = 80,  // USE
    CONN_LOST       // USE
} ErrCond;

typedef struct {
	unsigned int length;	// Number of cells occupied by this ship
	char id;			// Separates this ship from other ships
	int lives;			// How much of the ship is left
} Ship;

typedef struct {
	char *hidden;			// Grid with ship locations
	char *guess;			// Grid with player guesses
	unsigned int height;	// Height of the above grids
	unsigned int width;		// Width of the above grids
	unsigned int nShips;	// How many ships are in the game
	unsigned int alive;		// How many ships have not been sunk
	Ship **ships;			// Details of each ship
} Board;

/*
 * Return the name corresponding to the error condition c
 */
const char* get_str(ErrCond c) 
{
    switch (c) {
		case OK:
		    return "";
		case BAD_CMD:
		    return "Usage: nclient id game map port\n";
        case BAD_PARAM:
            return "I: Param error.\n";
		case NO_MAP:
		    return "I: Missing map file.\n";
		case OVL_MAP:
		    return "I: Overlap in map file.\n";
		case BOU_MAP:
		    return "I: Out of bounds in map file.\n";
		case BAD_MAP:
		    return "I: Error in map file.\n";
        case GO_WIN:
            return "I: Game over - win.\n";
        case GO_LOSS:
            return "I: Game over - loss.\n";
        case GO_DISCONN:
            return "I: Game over - disconnect.\n";
        case CONN_REF:
            return "I: Unable to connect to server.\n";
        case CONN_LOST:
            return "I: Lost connection.\n";
		default:
		    return "Unknown.\n"; 
	}
}

/*
 * Macros to map 2D coords into a 1D array and check if 2D coords are in
 * bounds
 */
#define MAP(S, y, x) S->width * y + x
#define INRANGE(S, y, x) ((x < S->width) && (y < S->height))

/*
 * Constants
 */
#define SHORT_LEN 20	// Max length of an input line

/* 
** Read a line from the file f.
** Do not free the memory returned from this function.
** Returns 0 if the line was too long or eof happens before \n
** In the event of a long line, the function gobbles chars until newline
*/
const char* get_short_line(FILE* f)
{
    int sLen;
    int c;
    static char buffer[SHORT_LEN + 1];	/* to allow for \0 */
    
	if (fgets(buffer, SHORT_LEN, f)) {
		if (feof(f)) {
			return 0;
		}

		/* Now we check to see if the line was too long */
		sLen = strlen(buffer);
		if (buffer[sLen-1] != '\n') {
		    while (c = fgetc(f), (c != EOF) && (c != '\n')) {
		    }		/* gobble until newline */
		    return 0;
		}
			return buffer;	
		} else {
		return 0;
    }
}
    
/*
** For debug purposes only 
** Prints both boards in b to the screen.
*/
void show_boards(Board* b)
{
    int i, j;
    
	for (i = 0; i < b->height; ++i) {
		for (j = 0; j < b->width; ++j) {
		    printf("%c", b->hidden[MAP(b, i, j)]);
		}
		printf("\n");
    }
    printf("\n");
    
	for (i = 0; i < b->height; ++i) {
		for (j = 0; j < b->width; ++j) {
		    printf("%c", b->guess[MAP(b, i, j)]);
		}
		printf("\n");
    }
}

/*
** Frees all memory associated with b.
*/
void dealloc_board(Board* b)
{
    unsigned int i;
    
	free(b->hidden);
    free(b->guess);
    
	for (i = 0; i < b->nShips; ++i) {
		if (b->ships[i] != 0) {
		    free(b->ships[i]);
		}
	}
    free(b->ships);
}

/*
** Adds a ship to the board.
** The position is measured with the top left being 0,0.
** Orientation is one of N, S, E, W
** Function returns OK on success and error code otherwise
*/
ErrCond stamp_ship(Board* b, Ship* s, char orientation, int xPos, int yPos)
{
    int i, x = xPos, y = yPos;
    int xStep = 0, yStep = 0;
    
	switch (orientation) {/* Set things up so we can walk along the ship*/
    	case 'N' : 
			yStep = -1;
			break;
    	case 'E' : 
			xStep = 1;
			break;
    	case 'W' :
			xStep = -1;
			break;  
    	case 'S' :
			yStep = 1;
			break;		
    	default:
			return BAD_MAP;
    }
    
	for (i = 0; i < s->length; ++i) {	/* For each cell in the ship */
		if (!INRANGE(b, y, x)) {
		    return BOU_MAP;
		}
		if (b->hidden[MAP(b, y, x)] != '.') {
		    return OVL_MAP;
		}
		
		/*Write the ship's id to hidden grid */
		b->hidden[MAP(b, y, x)] = s->id;	
		x += xStep;
		y += yStep;
	}
    return OK;
}

/*
** Prompts the user for a guess (which is returned via
** reference params x, y).
** Returns 1 on success, 0 otherwise
*/
int read_guess(Board* b, unsigned int* x, unsigned int* y)
{
    int res;
    const char* line;
    char dummy;
    
	printf("(x,y)>");
    
	line=get_short_line(stdin);
    if (line==0) {
		return 0;	/* hit eof or a long line */
    }
    
	res = sscanf(line, "%u %u%c", x, y, &dummy);	/* no trailing chars */
    if ((res != 3) || (dummy != '\n')) {
		return 0;
    }
    
	if ((*x >= b->width) || (*y >= b->height)) {
        return 0;
    }
    return 1;
}

/* 
** Takes a Board which has been allocated and populated its fields
** from the file describing the rules for the game and the map file
** describing the position of the ships.
** Returns error code or OK
** Note: This function is set up so that either things are completely
** allocated or not at all
** if error returned... no need to dealloc (not even the board).
*/
ErrCond alloc_board(Board* b, FILE* rules, FILE* map) {
    unsigned int h, w, n;   /* height, width and number of ships */
    unsigned int i, j;    /* loop counters */
    const char *line;
    char dummy;
    int res;
    char id = 'a';     /* starting point for ids */
    
	b->hidden = 0;     /* ensure that even if we exit we have sane object */
    b->guess = 0;
    b->height = 0;
    b->width = 0;
    b->nShips = 0;
    b->ships = 0;

    line = get_short_line(rules);
    if (line == 0) {
		return BAD_RULES;
    }

	/* check for exactly two params*/
    res = sscanf(line, "%u %u%c", &w, &h, &dummy); 
    if ((res != 3) || (dummy != '\n')) {
		return BAD_RULES;
    }

    line = get_short_line(rules);	/* read number of ships */
    if ((line == 0) || (sscanf(line, "%u%c", &n, &dummy) != 2)) {
		return BAD_RULES;
    }

    if ((h < 1) || (w < 1) || (n < 1))
    {
		return BAD_RULES;
    }
	
	/* blank both boards with . */
    b->hidden = (char*)malloc(sizeof(char) * h * w);
    for (i = 0; i < h * w; ++i) {
		b->hidden[i] = '.';
    }

    b->guess = (char*)malloc(sizeof(char) * h * w);
    for (i = 0; i < h * w; ++i) {
		b->guess[i] = '.';
    }

    b->height = h;
    b->width = w;
    b->nShips = n;
    
	b->ships=(Ship**)malloc(sizeof(Ship*) * n);
    for (i = 0; i < n; ++i) {
		b->ships[i] = 0;	/* So we know what to dealloc later */
    }
    
	for (i = 0; i < n; ++i) {	/* For each ship */
		b->ships[i] = (Ship*)malloc(sizeof(Ship));
		line = get_short_line(rules);
	
		/* Find out how long the ship is */
		if ((line == 0) || (sscanf(line, "%u", &j) == 0)) {	
			dealloc_board(b);
			return BAD_RULES;
		}

		b->ships[i]->id = id;
		b->ships[i]->length = j;
		b->ships[i]->lives = j;
		id++;
    }
    b->alive = b->nShips;
	
	/* Now we look at the map file to find where to put the ships */
    for (i = 0; i < b->nShips; ++i) {
		unsigned int x, y;
		char c;
		
		line=get_short_line(map);
		
		/*read x, y, direction */
		if ((line != 0) && (sscanf(line, "%u %u %c\n", &x, &y, &c) == 3)) {  
		    ErrCond res = stamp_ship(b, b->ships[i], c, x, y);
		    
			if (res != 0) {
				dealloc_board(b);
				return res;
		    }
		} else {
		    dealloc_board(b);
		    return BAD_MAP;
		}
    }
    return OK;
}

int parse_cmd_line(int argc, char* argv[], char** idC, char** idG, FILE** map, 
        int* port) {
    /* 5 and only 5 params */
    if (argc < 5 || argc > 5) {
		printf("%s", get_str(BAD_CMD));
		return BAD_CMD;
    }
    
    /* argv[1] should be a string representing the client id */
    *idC = (char *)malloc(sizeof(char) * (strlen(argv[1]) + 1));
    strcpy(*idC, argv[1]);

    /* argv[2] should be a string representing the game id */
    *idG = (char *)malloc(sizeof(char) * (strlen(argv[2]) + 1));
    strcpy(*idG, argv[2]);

    /* argv[3] should be the location of the map file */
	*map = fopen(argv[3], "r");
    if (*map == NULL) {
		printf("%s", get_str(NO_MAP));
		return NO_MAP;
    }
    
    /* argv[4] should be the port to connect to */
    if(sscanf(argv[4], "%d", port) != 1 || *port < 0 || *port > 65535) {
        printf("%s", get_str(BAD_PARAM));
        return BAD_PARAM;
    }

    return 0;
}

int connect_to_server(int* fd, int port) {
    if((*fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("%s", get_str(CONN_REF));
        return CONN_REF;
    }

    struct sockaddr_in servaddr;    // Address info of server
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_aton("127.0.0.1", &servaddr.sin_addr);

    if(connect(*fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        printf("%s", get_str(CONN_REF));
        return CONN_REF;
    }

    return 0;
}

int check_map(FILE* serverGet, FILE* serverSend, FILE* map, Board* b) {
    char buffer[80];    // Server output
    int fdRules[2];
    pipe(fdRules);  //Assume it will work
    FILE *readRules = fdopen(fdRules[0], "r");
    FILE *writeRules = fdopen(fdRules[1], "w");

    /* Send rules into rules pipe */
    while(1) {
        if(fgets(buffer, 80, serverGet) == NULL) {
            printf("%s", get_str(CONN_LOST));
            return CONN_LOST;
        }
        if(strcmp(buffer, "$endrules\n") == 0) {
            break;
        }
        fprintf(writeRules, buffer);
        fflush(writeRules);
    }
            
    ErrCond err = alloc_board(b, readRules, map);
    if(err == BAD_MAP) {
        fprintf(serverSend, "$map bad\n");
        fflush(serverSend);
        printf("%s", get_str(err));
        return err;
    }

    fclose(readRules);
    fclose(writeRules);
    fprintf(serverSend, "$map good\n");
    fflush(serverSend);

    return 0;
}

int main(int argc, char* argv[])
{
    /* Parse the command line input */
    char *idC, *idG;    // id strings for client and game
    FILE *map;          // File stream for map file
    int port;           // Port number to connect to
    int parseReturn = parse_cmd_line(argc, argv, &idC, &idG, &map, &port);
    if(parseReturn) {
        return parseReturn;
    }

    /* Connect to server as FILE* */
    int fd;
    int serverReturn = connect_to_server(&fd, port);
    if(serverReturn) {
        return serverReturn;
    }
    FILE* serverGet = fdopen(fd, "r");
    FILE* serverSend = fdopen(fd, "w");

    /* Send handshake */
    fprintf(serverSend, "$handshake %s %s\n", idC, idG);
    fflush(serverSend);

    char buffer[80];    // Server output
    Board b;            // The board game
    unsigned int x, y;  // User guesses
    while(fgets(buffer, 80, serverGet) != NULL) {
        if(strcmp(buffer, "$startrules\n") == 0) {
            int err = check_map(serverGet, serverSend, map, &b);
            if(err) {
                return err;
            }
        }

        /* If it's my turn */
        else if(strcmp(buffer, "$yourmove\n") == 0) {
            show_boards(&b);
            while(!read_guess(&b, &x, &y)) {
                if(feof(stdin)) {
                    fprintf(serverSend, "$bye\n");
                    fflush(serverSend);
                    printf("\n%s", get_str(OK));
                    return OK;
                }
            }

            fprintf(serverSend, "$request %u %u\n", x, y);
            fflush(serverSend);
        }

        /* Put these together  because we don't care about visuals */
        else if(strcmp(buffer, "$response hit\n") == 0 ||
                strcmp(buffer, "$response miss\n") == 0) {
            fprintf(serverSend, "$yourmove\n");
            fflush(serverSend);
        }

        /* I win! */
        else if(strcmp(buffer, "$response over\n") == 0) {
            printf("\n%s", get_str(GO_WIN));
            return GO_WIN;
        }

        /* Opponent disconnected */
        else if(strcmp(buffer, "$bye\n") == 0) {
            printf("\n%s", get_str(GO_DISCONN));
            return GO_DISCONN;
        }

        else if(sscanf(buffer, "$request %u %u\n", &x, &y) == 2) {
            Board* bb = &b;
            char id = bb->hidden[MAP(bb, y, x)];
            
            if(id == '.') {
                fprintf(serverSend, "$response miss\n");
                fflush(serverSend);
            } else {
                /* Not a duplicate hit */
                if(bb->guess[MAP(bb, y, x)] != '*') {
                    bb->guess[MAP(bb, y, x)] = '*';
                    bb->ships[id - 'a']->lives--;

                    /* ship has been sunk */
                    if(bb->ships[id-'a']->lives == 0) {
                        bb->alive--;

                        /* Game over */
                        if(bb->alive == 0) {
                            fprintf(serverSend, "$response over\n");
                            fflush(serverSend);
                            printf("\n%s", get_str(GO_LOSS));
                            return GO_LOSS;
                        }
                    }
                }
                fprintf(serverSend, "$response hit\n");
                fflush(serverSend);
            }
        }

    }

    /* Only get out of above loop if return is explicit or
     * connection lost */
    printf("%s", get_str(CONN_LOST));
    return CONN_LOST;
}

