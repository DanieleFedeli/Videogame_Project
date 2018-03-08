#include "image.h"
<<<<<<< HEAD
#include <errno.h>
=======
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724

#define SERVER_IP 	"127.0.0.1"
#define SERVER_PORT 8080
#define BACKLOG			5
#define ACTIVE			8
<<<<<<< HEAD
#define BUFFERSIZE	1000000

//USED FOR DEBUG
#define SERVER "[SERVER] "
#define CLIENT "[CLIENT] "
#define TCP		 "[ TCP  ] "
#define UDP		 "[ UDP  ] "
#define DEBUG	 1
#define DEFAULT_BLOCK_SIZE 1024
=======
#define BUFFERSIZE	32

//USED FOR DEBUG
#define SERVER"[SERVER] "
#define TCP		"[ TCP  ] "
#define UDP		"[ UDP  ] "
#define DEBUG	1
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724

#define GENERIC_ERROR_HELPER(cond, errCode, msg) do {               \
        if (cond) {                                                 \
            fprintf(stderr, "%s: %s\n", msg, strerror(errCode));    \
            exit(EXIT_FAILURE);                                     \
        }                                                           \
    } while(0)
<<<<<<< HEAD

#define ERROR_HELPER(ret, msg)          GENERIC_ERROR_HELPER((ret < 0), errno, msg)
#define PTHREAD_ERROR_HELPER(ret, msg)  GENERIC_ERROR_HELPER((ret != 0), ret, msg)
=======
    
#define ERROR_HELPER(ret, msg)          GENERIC_ERROR_HELPER((ret < 0), errno, msg)
#define PTHREAD_ERROR_HELPER(ret, msg)  GENERIC_ERROR_HELPER((ret != 0), ret, msg)    													
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724

struct acceptTcp{ //USED BY A THREAD IN SERVER
	Image * surface_texture;
	Image * elevation_texture;
	Image * vehicle_texture;
	int tcp_sock;
};

struct communicateTcp{ //USED BY A THREAD IN SERVER
	struct acceptTcp arg;
	int idx;
};
