#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <wait.h>
#include <semaphore.h>

#include "handler.h"
#include "utils.h"
#include "image.h"
#include "logger.h"


int shouldAccept;//VARIABILI GLOBALI USATE NEI THREAD
pid_t logger_pid;
time_t curr_time;

/** SIGNAL HANDLER **/

void action(int sig, siginfo_t *siginfo, void* context){
	
	switch(sig){
		case SIGQUIT:
		case SIGTERM:
		case SIGINT:
		{
			shouldAccept = 0;
			quit_server();
		}
		case SIGSEGV:
		{
			ERROR_HELPER_LOGGER(-1, "SEGMENTATION FAULT!!!\n");
		}
		case SIGALRM:
		{
			set_global_update();
			
			alarm(2);
		}
	}
}

sem_t* UDPEXEC;

/** ROUTINE CHE ASCOLTA LE CONNESSIONI IN ENTRATA E LANCIA UN TCP_ROUTINE PER CONNESSIONE **/
void * thread_generatore(void* arg){
	struct args* param = (struct args*)arg;
	int socket_tcp = param -> tcp_sock;
	pthread_t tcp_handler, udp_handler;
	int new_sock, ret;
	
	/** SE LA VARIABILE GLOBALE SHOULDACCEPT = 1, ACCETTA EVENTUALI CONNESSIONI IN ENTRATA **/	
	while(shouldAccept){
	
		int addrlen = sizeof(struct sockaddr_in);
		struct sockaddr_in client;
		
		/** ACCETTA CONNESSIONI IN ENTRATA**/
		
		new_sock = accept(socket_tcp, (struct sockaddr*) &client, (socklen_t*) &addrlen);
		if(new_sock == -1){ 
			sleep(1);
			continue;
			
		}
		if(DEBUG) print_all_user(); 
		
		time(&curr_time);
		fprintf(stderr, "%s%sConnessione accettata. ID: %d\n", ctime(&curr_time), SERVER, new_sock);
		param->idx = new_sock;
		
		/** LANCIO IL THREAD CON LA FUNZIONE TCP_RUOTINE **/
		ret = pthread_create(&tcp_handler, NULL, server_tcp_routine, (void*) param);
		PTHREAD_ERROR_HELPER(ret, "Errore creazione thread");
		
		/** NON ASPETTO LA FINE DEL THREAD **/
		ret = pthread_detach(tcp_handler);
		PTHREAD_ERROR_HELPER(ret, "Errore detach thread");
		
		/** ASPETTO L'AGGIUNTA DEL VEICOLO E LANCIO IL THREAD CON LA FUNZIONE UDP_ROUTINE **/
		
		sem_wait(UDPEXEC);
		ret = pthread_create(&udp_handler, NULL, server_udp_routine, (void*) param);
		PTHREAD_ERROR_HELPER(ret, "Errore nella creazione del thread");
		
		/** NON ASPETTO LA FINE DEL THREAD **/
		ret = pthread_detach(udp_handler);
		PTHREAD_ERROR_HELPER(ret, "Errore detach thread");
		
		sleep(1);
		
	}
	time(&curr_time);
	fprintf(stderr, "%s%sChiusura server!\n", ctime(&curr_time), SERVER);
	pthread_exit(NULL);
}


int main(int argc, char **argv) {
  char* elevation_filename = ELEVATION_FILENAME;
  char* texture_filename = SURFACE_FILENAME;
  char* vehicle_texture_filename="./images/arrow-right.ppm";
  printf("loading elevation image from %s ... ", elevation_filename);

  // load the images
  Image* surface_elevation = Image_load(elevation_filename);
  Image* surface_texture = Image_load(texture_filename);
  Image* vehicle_texture = Image_load(vehicle_texture_filename);

	/** DICHIARAZIONE VARIABILI DI RETE **/
	int ret, socket_tcp;
	struct sockaddr_in my_addr = {0};

	/** GENERO IL SOCKET PER LA COMUNICAZIONE TCP **/
	socket_tcp = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(socket_tcp, "Errore socket");

  /** RIEMPO LA STRUCT MY_ADDR CHE VERRÀ USATA PER LA COMUNICAZIONE **/
  my_addr.sin_family			= AF_INET;
  my_addr.sin_port				= htons(SERVER_PORT);
  my_addr.sin_addr.s_addr	= INADDR_ANY;

  /** SETTO IL SOCKET RIUSABILE, IN SEGUITO A CRASH POTRÀ ESSERE RIUSATO **/
  int reuseaddr_opt = 1;
  ret = setsockopt(socket_tcp, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
	ERROR_HELPER(ret, "Errore socketOpt");

	/** SETTO IL SOCKET NON BLOCCANTE **/
	ret = ioctl(socket_tcp, FIONBIO, &reuseaddr_opt, sizeof(reuseaddr_opt));
	ERROR_HELPER(ret, "Errore ioctl");
	
	/** EFFETTO IL BINDING DELL'INDIRIZZO AD UN INTERFACCIA **/
  ret = bind(socket_tcp, (struct sockaddr*) &my_addr, sizeof(my_addr));
	ERROR_HELPER(ret, "Errore bind");

	/** ASCOLTO UN TOTALE DI BACKLOG CONNESSIONI **/
	ret = listen(socket_tcp, BACKLOG);
  ERROR_HELPER(ret, "Errore nella listen");

  /** SETTO VARIABILI GLOBALI **/
  shouldAccept			= 1;


	/** CREAZIONE DEL LOG **/
	char* logfile_name = "./Logger/server-logger.txt";
	int logfile_desc = open(logfile_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ERROR_HELPER(logfile_name, "Errore nella creazione del logger");
	
	int logger_pipe[2];
	ret = pipe(logger_pipe);
	ERROR_HELPER(ret, "Impossibile creare pipe");
	
	logger_pid = fork();
	if(logger_pid == -1)	ERROR_HELPER(-1, "Impossibile creare figlio logger");
	else if(logger_pid == 0){
		ret = close(socket_tcp);
		ERROR_HELPER(ret, "Impossibile chiudere il listening socket");
		
		startLogger(logfile_desc, logger_pipe);
	}
	else{
		/** PARTE PARALLELA **/
		close(logfile_desc);
		close(logger_pipe[0]);
		
		dup2(logger_pipe[1], STDERR_FILENO);
		
		//Current time
		time(&curr_time);
		fprintf(stderr, "%s%sServer startato\n", ctime(&curr_time), SERVER);
		
		pthread_t generatore;
		struct args arg;
		arg.tcp_sock 					= socket_tcp;
		sem_t* create_sem = (sem_t*) calloc(1, sizeof(sem_t));
		sem_t* udptcp_sem = (sem_t*) calloc(1, sizeof(sem_t));
		UDPEXEC = (sem_t*) calloc(1, sizeof(sem_t));
  
		ret = sem_init(create_sem, 0, 1);
		ERROR_HELPER_LOGGER(ret, "Inizializzazione semaforo");
		ret = sem_init(udptcp_sem, 0, 1);
		ERROR_HELPER_LOGGER(ret, "Inizializzazione semaforo");
		ret = sem_init(UDPEXEC, 0, 0);
		ERROR_HELPER_LOGGER(ret, "Inizializzazione semaforo");
  
		__init__(surface_texture, surface_elevation, create_sem, udptcp_sem, UDPEXEC);
		
		fprintf(stderr, "%s%sStart thread listener\n", ctime(&curr_time), SERVER);
		ret = pthread_create(&generatore, NULL, thread_generatore, (void*) &arg);
		PTHREAD_ERROR_HELPER(ret, "Errore nella creazione del thread");
  
		/** PARTE DELLA GESTIONE SEGNALI**/
		struct sigaction act;
		memset(&act, '\0', sizeof(act));

		act.sa_sigaction = &action;
		act.sa_flags		 = SA_SIGINFO;

		ret = sigaction(SIGINT, &act, NULL);
		ERROR_HELPER_LOGGER(ret, "Errore nella sigaction");
		ret = sigaction(SIGQUIT, &act, NULL);
		ERROR_HELPER_LOGGER(ret, "Errore nella sigaction");
		ret = sigaction(SIGTERM, &act, NULL);
		ERROR_HELPER_LOGGER(ret, "Errore nella sigaction");
		ret = sigaction(SIGSEGV, &act, NULL);
		ERROR_HELPER_LOGGER(ret, "Errore nella sigaction");
		ret = sigaction(SIGALRM, &act, NULL);
		ERROR_HELPER_LOGGER(ret, "Errore nella sigaction");
		alarm(2);
	
		/** ASPETTO LA FINE DEI THREAD **/
		ret = pthread_join(generatore, NULL);
		PTHREAD_ERROR_HELPER(ret, "Errore nella detach");
		
		ret = close(logger_pipe[1]);
		ERROR_HELPER_LOGGER(ret, "Errore nella chiusura della pipe");
		
		/** DEALLOCAZIONE RISOSE **/	
		Image_free(surface_elevation);
		Image_free(surface_texture);
		Image_free(vehicle_texture);

		exit(EXIT_SUCCESS);
	}
}
