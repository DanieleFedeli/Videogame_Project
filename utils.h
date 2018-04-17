#pragma once

#include <errno.h>
#include <semaphore.h>

#include "image.h"

#define SERVER_IP 	"127.0.0.1"
#define SERVER_PORT 8080
#define BACKLOG			5
#define ACTIVE			8
#define BUFFERSIZE	1000000
#define MAX_USER 		100

//USED FOR DEBUG
#define SERVER	"[SERVER] "
#define CLIENT  "[CLIENT] "
#define TCP			"[ TCP  ] "
#define UDP			"[ UDP  ] "
#define ELEVATION_FILENAME "./images/maze.pgm"
#define VEHICLE_FILENAME   "./images/arrow-right.ppm"
#define SURFACE_FILENAME   "./images/maze.ppm"

#define DEBUG	1

//MACRO PER LA GESTIONE DEGLI ERRORI
#define GENERIC_ERROR_HELPER(cond, errCode, msg) do {               \
        if (cond) {                                                 \
            fprintf(stderr, "%s: %s\n", msg, strerror(errCode));    \
            exit(EXIT_FAILURE);                                     \
        }                                                           \
    } while(0)


#define ERROR_HELPER(ret, msg)          GENERIC_ERROR_HELPER((ret < 0), errno, msg)
#define PTHREAD_ERROR_HELPER(ret, msg)  GENERIC_ERROR_HELPER((ret != 0), ret, msg)

//MACRO PER IL LOGGER
#define ERROR_HELPER_LOGGER(ret, message)  do {         \
            if (ret < 0) {                                              \
                fprintf(stderr, "%s: %s\n", message, strerror(errno));  \
                exit(EXIT_FAILURE);                                     \
            }                                                           \
        } while (0)


struct args{ //USED BY A THREAD IN SERVER
	int tcp_sock;
	int udp_sock;
	int idx;
};

