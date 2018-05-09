#pragma once

#include <errno.h>
#include <semaphore.h>

#include "image.h"

#define SERVER_IP 	"127.0.0.1"
#define SERVER_PORT 8080
#define BACKLOG			5
#define BUFFERSIZE	2621830
#define BUFF_UDP		1000
#define MAX_USER 		100

//USED FOR DEBUG
#define SERVER	"[SERVER] "
#define CLIENT  "[CLIENT] "
#define TCP			"[ TCP  ] "
#define UDP			"[ UDP  ] "
#define ELEVATION_FILENAME "./images/maze.pgm"
#define VEHICLE_FILENAME   "./images/arrow-right.ppm"
#define SURFACE_FILENAME   "./images/maze.ppm"

#define DEBUG	0

//MACRO PER LA GESTIONE DEGLI ERRORI
#define GENERIC_ERROR_HELPER(cond, errCode, msg) do {               \
        if (cond) {                                                 \
            fprintf(stderr, "%s: %s\n", msg, strerror(errCode));    \
            exit(EXIT_FAILURE);                                     \
        }                                                           \
    } while(0)


#define ERROR_HELPER(ret, msg)          GENERIC_ERROR_HELPER((ret < 0), errno, msg)
#define PTHREAD_ERROR_HELPER(ret, msg)  GENERIC_ERROR_HELPER((ret != 0), ret, msg)


struct args{ //USED BY A THREAD IN SERVER
	int tcp_sock;
	int udp_sock;
	int idx;
	int logger_pipe; //PASSATA DAL CLIENT UDP SOLO PER ESSERE CHIUSA, NON VIENE USATA
};

