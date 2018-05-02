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


int shouldAccept, logger_shouldStop;//VARIABILI GLOBALI USATE NEI THREAD
pid_t logger_pid;
time_t curr_time;
sem_t* UDPEXEC;

#define ERROR_HELPER_LOGGER(ret, message)  do {         								\
            if (ret < 0) {                                              \
                fprintf(stderr, "%s: %s\n", message, strerror(errno));  \
                kill(logger_pid, SIGTERM);                              \
                exit(EXIT_FAILURE);                                     \
            }                                                           \
        } while (0)


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
		case SIGALRM:
		{
			set_global_update();
			alarm(2);
		}
	}
}

void action_logger(int sig, siginfo_t *siginfo, void* context){
	sleep(3);
	logger_shouldStop = 1;
}

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
		
		sleep(0.5);	
	}
	
	time(&curr_time);
	fprintf(stderr, "%s%sChiusura server!\n", ctime(&curr_time), SERVER);
	pthread_exit(NULL);
}


void startLogger(int logfile_desc, int logging_pipe[2]){
	int ret;

  ret = close(logging_pipe[1]);
  ERROR_HELPER(ret, "Cannot close pipe's write descriptor in Logger");

  char* toWrite;
  logger_shouldStop = 0;
  while(1) {
		int msg_len = 0;
		toWrite = (char*) calloc(BUFFERSIZE, sizeof(char));
		
		while (1) {
			ret = read(logging_pipe[0], toWrite + msg_len, 1);
      if (ret == -1 && errno == EINTR) continue;
      
      ERROR_HELPER(ret, "Cannot read from pipe");
      
      if (ret == 0) break;
      if (toWrite[msg_len++] == '\n') break;
    }

		int written_bytes = 0;
		int bytes_left = msg_len;
		while (bytes_left > 0) {
      ret = write(logfile_desc, toWrite + written_bytes, bytes_left);
      
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "Cannot write to log file");

      bytes_left -= ret;
      written_bytes += ret;
    }
    
    free(toWrite);
		if (logger_shouldStop) break;
		
   }

  ret = close(logfile_desc);
  ERROR_HELPER(ret, "Cannot close log file from Logger");

  close(logging_pipe[0]); // what happens when you write on a closed pipe?
  ERROR_HELPER(ret, "Cannot close pipe's read descriptor in Logger");

  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  char* elevation_filename = ELEVATION_FILENAME;
  char* texture_filename = SURFACE_FILENAME;
  printf("loading elevation image from %s ... ", elevation_filename);

  Image* surface_elevation = Image_load(elevation_filename);
  Image* surface_texture = Image_load(texture_filename);

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
		Image_free(surface_elevation);
		Image_free(surface_texture);
		
		struct sigaction act;
		memset(&act, '\0', sizeof(act));

		act.sa_sigaction = &action_logger;
		act.sa_flags		 = SA_SIGINFO;

		ret = sigaction(SIGINT, &act, NULL);
		ERROR_HELPER_LOGGER(ret, "Errore nella sigaction");
		ret = sigaction(SIGQUIT, &act, NULL);
		ERROR_HELPER_LOGGER(ret, "Errore nella sigaction");
		ret = sigaction(SIGTERM, &act, NULL);
		ERROR_HELPER_LOGGER(ret, "Errore nella sigaction");
		
		ret = close(socket_tcp);
		ERROR_HELPER(ret, "Impossibile chiudere il listening socket");
		
		startLogger(logfile_desc, logger_pipe);
		
		exit(EXIT_SUCCESS);
		
	}
	else{
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
		ret = sigaction(SIGALRM, &act, NULL);
		ERROR_HELPER_LOGGER(ret, "Errore nella sigaction");
		alarm(2);
	
		/** ASPETTO LA FINE DEI THREAD **/
		ret = pthread_join(generatore, NULL);
		PTHREAD_ERROR_HELPER(ret, "Errore nella detach");
		
		ret = close(logger_pipe[1]);
		ERROR_HELPER_LOGGER(ret, "Errore nella chiusura della pipe");
		
		kill(logger_pid, SIGKILL);
		/** DEALLOCAZIONE RISOSE **/	
		close(socket_tcp);
		
		exit(EXIT_SUCCESS);
	}
}
