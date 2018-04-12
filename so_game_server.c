#include <sys/ioctl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <wait.h>

#include "handler.h"
#include "utils.h"
#include "image.h"


int shouldAccept;//VARIABILI GLOBALI USATE NEI THREAD

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
			if(shouldAccept && get_global_communicate()) alarm(2);
		}
	}
}


/** ROUTINE CHE ASCOLTA LE CONNESSIONI IN ENTRATA E LANCIA UN TCP_ROUTINE PER CONNESSIONE **/
void * tcp_accept(void* arg){
	struct args* param = (struct args*)arg;
	int socket_tcp = param -> tcp_sock;
	int socket_udp = param -> udp_sock;
	pthread_t tcp_handler;
	int new_sock, ret;

	/** SE LA VARIABILE GLOBALE SHOULDACCEPT = 1, ACCETTA EVENTUALI CONNESSIONI IN ENTRATA **/
	while(shouldAccept){
		int addrlen = sizeof(struct sockaddr_in);
		struct sockaddr_in client;
		if(DEBUG) print_all_user();
		/** ACCETTA CONNESSIONI IN ENTRATA**/
		if(DEBUG)printf("%sAttesa di una connessione\n",SERVER);
		new_sock = accept(socket_tcp, (struct sockaddr*) &client, (socklen_t*) &addrlen);
		if(new_sock == -1){
			sleep(1); 	//USATA PER NON USARE IL 100% DELLA CPU
			continue;
		}

		if(DEBUG)printf("%sConnessione accettata\n",SERVER);
		
		param->idx = new_sock;
		
		/** LANCIO IL THREAD CON LA FUNZIONE TCP_RUOTINE **/
		if(DEBUG)printf("%sCreazione thread per la gestione della connessione TCP\n",SERVER);
		ret = pthread_create(&tcp_handler, NULL, server_tcp_routine, (void*) param);
		PTHREAD_ERROR_HELPER(ret, "Errore creazione thread");
		
		/** NON ASPETTO LA FINE DEL THREAD **/
		ret = pthread_detach(tcp_handler);
		PTHREAD_ERROR_HELPER(ret, "Errore detach thread");
	}
	
	if(DEBUG)printf("%sChiusura server\n",SERVER);
	pthread_exit(NULL);
}


int main(int argc, char **argv) {
  char* elevation_filename = ELEVATION_FILENAME;
  char* texture_filename = SURFACE_FILENAME;
  char* vehicle_texture_filename="./images/arrow-right.ppm";
  printf("loading elevation image from %s ... ", elevation_filename);

  // load the images
  Image* surface_elevation = Image_load(elevation_filename);
  if (surface_elevation) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  printf("loading texture image from %s ... ", texture_filename);
  Image* surface_texture = Image_load(texture_filename);
  if (surface_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  printf("loading vehicle texture (default) from %s ... ", vehicle_texture_filename);
  Image* vehicle_texture = Image_load(vehicle_texture_filename);
  if (vehicle_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

	/** DICHIARAZIONE VARIABILI DI RETE **/
	int ret, socket_tcp, socket_udp;
	struct sockaddr_in my_addr = {0};

	if(DEBUG)printf("%s.......TCP INIT.......\n",SERVER);
	if(DEBUG)printf("%sCreazione socket\n",SERVER);

	/** GENERO IL SOCKET PER LA COMUNICAZIONE TCP **/
	socket_tcp = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(socket_tcp, "Errore socket");

  /** RIEMPO LA STRUCT MY_ADDR CHE VERRÀ USATA PER LA COMUNICAZIONE **/
  my_addr.sin_family			= AF_INET;
  my_addr.sin_port				= htons(SERVER_PORT);
  my_addr.sin_addr.s_addr	= INADDR_ANY;

  /** SETTO IL SOCKET RIUSABILE, IN SEGUITO A CRASH POTRÀ ESSERE RIUSATO **/
  if(DEBUG)printf("%sOttimizzazione socket\n",SERVER);
  int reuseaddr_opt = 1;
  ret = setsockopt(socket_tcp, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
	ERROR_HELPER(ret, "Errore socketOpt");


	/** SETTO IL SOCKET NON BLOCCANTE **/

	if(DEBUG)printf("%sRendo socket non bloccante\n",SERVER);
	ret = ioctl(socket_tcp, FIONBIO, &reuseaddr_opt, sizeof(reuseaddr_opt));
	ERROR_HELPER(ret, "Errore ioctl");

	/** EFFETTO IL BINDING DELL'INDIRIZZO AD UN INTERFACCIA **/
	if(DEBUG)printf("%sBinding in corso\n",SERVER);
  ret = bind(socket_tcp, (struct sockaddr*) &my_addr, sizeof(my_addr));
	ERROR_HELPER(ret, "Errore bind");

	/** ASCOLTO UN TOTALE DI BACKLOG CONNESSIONI **/
	ret = listen(socket_tcp, BACKLOG);
  ERROR_HELPER(ret, "Errore nella listen");

  if(DEBUG)printf("%s.......UDP INIT.......\n",SERVER);
  if(DEBUG)printf("%sCreazione socket\n",SERVER);

  /** CREO SOCKET PER LA UDP +*/
  socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(ret, "Errore nella socket");


  /** EFFETTO IL BINDING DELL'INDIRIZZO AD UN INTERFACCIA **/
  if(DEBUG)printf("%sBind in corsot\n",SERVER);
  ret = bind(socket_udp, (struct sockaddr*) &my_addr, sizeof(my_addr));
  ERROR_HELPER(ret, "Errore bind");

  /** SETTO VARIABILI GLOBALI **/
  shouldAccept			= 1;

	/** PARTE PARALLELA **/
  if(DEBUG)printf("%sCreazione thread per la gestione delle comunicazioni\n",SERVER);
  pthread_t tcp_handler, udp_handler;

  struct args arg;
	
	arg.udp_sock					= socket_udp;
  arg.tcp_sock 					= socket_tcp;
	__init__(surface_texture, surface_elevation);

	if(DEBUG)printf("%sThread TCP creato\n",SERVER);
  ret = pthread_create(&tcp_handler, NULL, tcp_accept, (void*) &arg);
  PTHREAD_ERROR_HELPER(ret, "Errore nella creazione del thread");
  if(DEBUG)printf("%sThread UDP creato\n",SERVER);
  //ret = pthread_create(&udp_handler, NULL, server_udp_routine, (void*) &arg);
  PTHREAD_ERROR_HELPER(ret, "Errore nella creazione del thread");

  /** PARTE DELLA GESTIONE SEGNALI**/
  struct sigaction act;
  memset(&act, '\0', sizeof(act));

  act.sa_sigaction = &action;
  act.sa_flags		 = SA_SIGINFO;

	ret = sigaction(SIGINT, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");
	ret = sigaction(SIGQUIT, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");
	ret = sigaction(SIGTERM, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");
	ret = sigaction(SIGALRM, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");
	alarm(2);
	
	if(DEBUG)printf("%sThread creati\n",SERVER);
	/** ASPETTO LA FINE DEI THREAD **/
	ret = pthread_join(tcp_handler, NULL);
	PTHREAD_ERROR_HELPER(ret, "Errore nella detach");
	//ret = pthread_join(udp_handler, NULL);
	PTHREAD_ERROR_HELPER(ret, "Errore nella detach");

	/** DEALLOCAZIONE RISOSE **/
	if(DEBUG)printf("%sDealloco la lista utenti e chiudo file descriptors\n", SERVER);
	
	//close(socket_tcp);
	//close(socket_udp);

	if(DEBUG)printf("%sDealloco immagini\n",SERVER);

	Image_free(surface_elevation);
	Image_free(surface_texture);
	Image_free(vehicle_texture);

	exit(EXIT_SUCCESS);
}
