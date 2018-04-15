#pragma once

#include <errno.h>
#include <semaphore.h>

#include "image.h"

#define SERVER_IP 	"127.0.0.1"
#define SERVER_PORT 8080
#define BACKLOG			5
#define ACTIVE			8
#define BUFFERSIZE	1000000

//USED FOR DEBUG
#define SERVER	"[SERVER] "
#define CLIENT  "[CLIENT] "
#define TCP			"[ TCP  ] "
#define UDP			"[ UDP  ] "
#define ELEVATION_FILENAME "./images/maze.pgm"
#define VEHICLE_FILENAME   "./images/arrow-right.ppm"
#define SURFACE_FILENAME   "./images/maze.ppm"

#define DEBUG	0



#define GENERIC_ERROR_HELPER(cond, errCode, msg) do {               \
        if (cond) {                                                 \
            fprintf(stderr, "%s: %s\n", msg, strerror(errCode));    \
            exit(EXIT_FAILURE);                                     \
        }                                                           \
    } while(0)


#define ERROR_HELPER(ret, msg)          GENERIC_ERROR_HELPER((ret < 0), errno, msg)
#define PTHREAD_ERROR_HELPER(ret, msg)  GENERIC_ERROR_HELPER((ret != 0), ret, msg)


struct args{ //USED BY A THREAD IN SERVER
	Image * surface_texture;
	Image * elevation_texture;
	sem_t* create_sem;
	sem_t* udptcp_sem;
	int tcp_sock;
	int udp_sock;
	int idx;
};

