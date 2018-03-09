#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <wait.h>
#include <errno.h>
#include <semaphore.h>

#include "so_game_protocol.h"
#include "utils.h"
#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"


int shouldAccept, shouldCommunicate, shouldUpdate; //VARIABILI GLOBALI USATE NEI THREAD
sem_t* 		user_sem; //SEMAFORO VARIABILE GLOBALE
ListHead*	user_on;	//LINKED LIST CHE CONTIENE GLI UTENTI

/** SIGNAL HANDLER **/

void action(int sig, siginfo_t *siginfo, void* context){
	switch(sig){
		case SIGQUIT:
		case SIGTERM:
		case SIGINT:
		{
			shouldAccept 				= 0;
			shouldCommunicate 	= 0;
		}
		case SIGALRM:
		{
			shouldUpdate = 1;
			if(shouldAccept && shouldCommunicate) alarm(2);
		}
	}
}

/** GESTORE PACCHETTI PER LA CONNESSIONE UDP**/
int Packet_handler_udp(char* PACKET, char* SEND, struct sockaddr_in client){
	PacketHeader* header = (PacketHeader*)PACKET;
	if(DEBUG)printf("%sSpacchetto...\n",UDP);

	if(header->type == VehicleUpdate){
		if(DEBUG)printf("%sAggiornamento veicolo ricevuto\n", UDP);
		VehicleUpdatePacket* vehicle_packet = (VehicleUpdatePacket*)Packet_deserialize(PACKET, header->size);

		if(vehicle_packet->id < 0){
			Packet_free(&vehicle_packet->header);
			return -1;
		}

		if(DEBUG)printf("%sImmagazzinamento delle informazioni\n", UDP);
		sem_wait(user_sem);
		ListItem* user = List_find_id(user_on, vehicle_packet->id);

		if(user == NULL){
			Packet_free(&vehicle_packet->header);
			sem_post(user_sem);
			return -1;
		}

		user->rotational_force 		= vehicle_packet->rotational_force;
		user->translational_force = vehicle_packet->translational_force;
		user->x 		= vehicle_packet->x;
		user->y 		= vehicle_packet->y;
		user->theta = vehicle_packet->theta;

		user->addr 	= client;

		sem_post(user_sem);

		Packet_free(&vehicle_packet->header);
		return 0;
	}

	else return -1;
}

/** GESTORE PACCHETTI PER LA CONNESSIONE TCP **/
int Packet_handler_tcp(char* PACKET, char* SEND, void* arg){
	struct communicateTcp argTcp = *(struct communicateTcp*)arg;
	int msg_len = 0;
	PacketHeader* header = (PacketHeader*)PACKET;

	if(DEBUG)printf("%sSpacchetto...\n",TCP);

	if(header->type == GetId){
		if(DEBUG)printf("%sRichiesta di ID ricevuta\n",TCP);
		IdPacket* id_packet = (IdPacket*)Packet_deserialize(PACKET, header->size);

		if(id_packet->id != -1){
			Packet_free(&id_packet->header);
			if(DEBUG)printf("%sID ERRATO\n", TCP);
			return 0;
		}

		Packet_free(&id_packet->header);

		if(DEBUG)printf("%sPreparazione pacchetto da inviare\n",TCP);
		IdPacket* toSend = malloc(sizeof(IdPacket));
		PacketHeader head;

		head.type=GetId;
		toSend->id = argTcp.idx;
		toSend->header = head;

		if(DEBUG)printf("%sSerializzo!\n",TCP);
		
		msg_len = Packet_serialize(SEND, &toSend->header);
		
		Packet_free(&toSend->header);
		return msg_len;
	}

	else if(header->type == GetTexture){

		if(DEBUG)printf("%sRichiesta texture ricevuta\n", TCP);
		ImagePacket* img_packet = (ImagePacket*)Packet_deserialize(PACKET, header->size);

		if(img_packet->id != argTcp.idx){
			Packet_free(&img_packet->header);
			return 0;
		}

		Packet_free(&img_packet->header);

		if(DEBUG)printf("%sPreparazione pacchetto da inviare\n", TCP);
		ImagePacket* toSend = malloc(sizeof(ImagePacket));
		PacketHeader head;

		head.type	 		= PostTexture;
		toSend->image = argTcp.arg.surface_texture;
		toSend->id		= argTcp.idx;
		toSend->header= head;
		if(DEBUG)printf("%sSerializzo!\n", TCP);

		msg_len = Packet_serialize(SEND, &toSend->header);
		//Packet_free(&toSend->header); IDEM SOTTO
		return msg_len;
	}


	else if(header->type == GetElevation){
		if(DEBUG)printf("%sRichiesta elevation ricevuta\n", TCP);
		ImagePacket* img_packet = (ImagePacket*)Packet_deserialize(PACKET, header->size);

		if(img_packet->id != argTcp.idx){
			Packet_free(&img_packet->header);
			return 0;
		}

		Packet_free(&img_packet->header);

		if(DEBUG)printf("%sPreparazione pacchetto da inviare\n", TCP);
		ImagePacket* toSend = malloc(sizeof(ImagePacket));
		PacketHeader head;

		head.type			= PostElevation;
		toSend->image = argTcp.arg.elevation_texture;
		toSend->id		= argTcp.idx;
		toSend->header= head;

		if(DEBUG)printf("%sSerializzo!\n", TCP);

		msg_len = Packet_serialize(SEND, &toSend->header);
		//Packet_free(&toSend->header); CAUSA PROBLEMI NON SO PERCHÈ
		return msg_len;
	}

	else if(header->type == PostTexture){
		if(DEBUG)printf("%sPost texture ricevuta\n", TCP);
		ImagePacket* img_packet = (ImagePacket*)Packet_deserialize(PACKET, header->size);

		if(img_packet->id < 0){
			Packet_free(&img_packet->header);
			return 0;
		}

		Image * toPut = img_packet->image;
		Packet_free(&img_packet->header);

		if(DEBUG)printf("%sInserisco texture in userList\n", TCP);
		sem_wait(user_sem);
		ListItem* user 				= List_find_id(user_on, argTcp.idx);
		user->vehicle_texture = toPut;
		sem_post(user_sem);

		return 0;
	}

	else return 0;
}

/** FUNZIONE PER THREAD CHE GESTISCE UNA SINGOLA CONNESSIONE CON PROTOCOLLO TCP **/
void * tcp_routine(void* arg){
	/** DICHIARAZIONI VARIABILI **/
	struct communicateTcp tcpArg = *(struct communicateTcp*)arg;
	int socket = tcpArg.idx;
	int bytes_read, bytes_sent, msg_len;
	int shouldThread = 1;

	/** INSERISCO NELLA USER LIST CON UN NUOVO USER**/
	if(DEBUG)printf("%sSocket in routine: %d\n", SERVER, socket);
	if(DEBUG)printf("%sAggiungo user\n",TCP);

	sem_wait(user_sem);
	ListItem* user = calloc(1, sizeof(ListItem));
	user->idx = tcpArg.idx;
	user->vehicle_texture = tcpArg.arg.vehicle_texture;
	List_insert(user_on, 0, user);
	sem_post(user_sem);

	int count = 0;
	/** QUESTO CICLO SERVE PER LEGGERE COSA VUOLE IL CLIENT, PROCESSARE LA RISPOSTA ED INVIARLA **/
	while(shouldCommunicate && shouldThread && count < 5){
		bytes_read = 0;
		
		char RECEIVE[BUFFERSIZE], SEND[BUFFERSIZE];
		if(DEBUG) printf("%sSono nel ciclo di comunicazione\n",TCP);

		/** RICEVO DAL SOCKET**/
		
		bytes_read = recv(socket, RECEIVE, BUFFERSIZE, 0);
		
		if(DEBUG) printf("%sByte(s) letti: %d\n",TCP, bytes_read);
		
		if(bytes_read == 0){
			if(DEBUG)printf("%sStringa di %d bytes\n",TCP, bytes_read);
			count++;
			continue;	
		}

		count ^= count; 
		
		/** CHIAMO UN HANDLER PER GENERARE LA RISPOSTA**/
		msg_len = Packet_handler_tcp(RECEIVE, SEND, arg);
		if(DEBUG)printf("%sDevo inviare una stringa di %d bytes\n",TCP, msg_len);
		/** INVIO RISPOSTA AL CLIENT**/
		if(msg_len == 0) continue;

		bytes_sent = send(socket, SEND, msg_len, 0);
		ERROR_HELPER(bytes_sent, "Errore nell'invio di dati");
		if(DEBUG)printf("%sInviata con successo\n",TCP);
		//sleep(1); //USATA PER EVITARE L'USO AL 100% DELLA CPU
		
	}

	/** SE ESCO DAL CICLO SIGNIFICA CHE DEVO CHIUDERE IL PROGRAMMA, MI PREPARO A DEALLOCARE RISORSE**/
	if(DEBUG) printf("%sUtente %d disconnesso.\n", TCP, socket);

	sem_wait(user_sem);
	List_detach(user_on, user);
	sem_post(user_sem);

	free(user);
	pthread_exit(NULL);
}

/** ROUTINE CHE ASCOLTA LE CONNESSIONI IN ENTRATA E LANCIA UN TCP_ROUTINE PER CONNESSIONE **/
void * tcp_accept(void* arg){
	struct acceptTcp args = *(struct acceptTcp*)arg;
	int socket_tcp = args.tcp_sock;

	pthread_t tcp_handler;
	int new_sock, ret;

	/** SE LA VARIABILE GLOBALE SHOULDACCEPT = 1, ACCETTA EVENTUALI CONNESSIONI IN ENTRATA **/
	while(shouldAccept){
		int addrlen = sizeof(struct sockaddr_in);
		struct sockaddr_in client;

		if(DEBUG){ //SCRIVE SUL STDOUT GLI UTENTI CONNESSI CON LE LORO TEXTURE
			ListItem* toPrint = user_on->first;
			printf("%sSTAMPA USER:\nSize: %d ",SERVER, user_on->size);
			for(int i = 0; i < user_on->size; i++){
				printf("(ID: %d P_IMG: %p) ", toPrint->idx, toPrint->vehicle_texture);
				toPrint = toPrint->next;
			}
			printf("\n");
		}
		
		/** ACCETTA CONNESSIONI IN ENTRATA**/
		if(DEBUG)printf("%sAttesa di una connessione\n",SERVER);
		new_sock = accept(socket_tcp, (struct sockaddr*) &client, (socklen_t*) &addrlen);
		if(new_sock == -1){
			sleep(1); 	//USATA PER NON USARE IL 100% DELLA CPU
			continue;
		}

		if(DEBUG)printf("%sConnessione accettata\n",SERVER);

		struct communicateTcp argTcp;
		argTcp.arg = args;
		argTcp.idx = new_sock;

		/** LANCIO IL THREAD CON LA FUNZIONE TCP_RUOTINE **/
		if(DEBUG)printf("%sCreazione thread per la gestione della connessione TCP\n",SERVER);
		ret = pthread_create(&tcp_handler, NULL, tcp_routine, (void*) &argTcp);
		PTHREAD_ERROR_HELPER(ret, "Errore creazione thread");

		/** NON ASPETTO LA FINE DEL THREAD **/
		ret = pthread_detach(tcp_handler);
		PTHREAD_ERROR_HELPER(ret, "Errore detach thread");

		//sleep(1); // USATA PER NON USARE IL 100% DELLA CPU
	}

	pthread_exit(NULL);
}

/** ROUTINE CHE ASCOLTA I MESSAGGI IN UDP E ELABORA RISPOSTE **/

void * udp_communicate(void* arg){

	struct sockaddr_in client_temp = {0};
	char RECEIVE[BUFFERSIZE], SEND[BUFFERSIZE];
	int ret, msg_len = 0, addrlen, socket_udp = *(int*)arg;
	shouldUpdate = 0;
	while(shouldCommunicate){


		if(shouldUpdate){
			if(DEBUG)printf("%sAggiornamento globale\n", UDP);
			if(DEBUG)printf("%sPreparazioni dati\n", UDP);
			WorldUpdatePacket* big_update = (WorldUpdatePacket*) calloc(1, sizeof(WorldUpdatePacket));
			PacketHeader header = {.type = WorldUpdate};

			big_update->header 			 = header;
			big_update->num_vehicles = user_on->size;
			big_update->updates = calloc(big_update->num_vehicles, sizeof(ClientUpdate));

			sem_wait(user_sem);
			ListItem* users = user_on->first;
			for(int i = 0; i < big_update->num_vehicles; i++){
				ClientUpdate* toPut = (ClientUpdate*)calloc(1, sizeof(ClientUpdate));
				toPut->id 	= users->idx;
				toPut->x  	= users->x;
				toPut->y  	= users->y;
				toPut->theta= users->theta;
				big_update->updates[i] = *toPut;
				if(i != big_update->num_vehicles - 1) users = users->next;
			}
			sem_post(user_sem);

			if(DEBUG)printf("%sPacchetto serializzato\n", UDP);
			msg_len = Packet_serialize(SEND, &big_update->header);

			if(msg_len < 1){
				if(DEBUG)printf("%sErrore serializzazione\n", UDP);
				shouldUpdate = 0;
				continue;
			}
			
			sem_wait(user_sem);
			users = user_on->first;
			if(DEBUG)printf("%sInvio ai client connessi\n", UDP);
			for(int i = 0; i < big_update->num_vehicles; i++){
					addrlen = sizeof(users->addr);
					ret = sendto(socket_udp, SEND, msg_len, 0, (struct sockaddr*) &users->addr, (socklen_t) addrlen);
					if(i != big_update->num_vehicles - 1) users = users->next;
			}
			sem_post(user_sem);

			if(DEBUG)printf("%sDealloco...\n", UDP);
			Packet_free(&big_update->header);
			shouldUpdate = 0;
			if(DEBUG)printf("%sNormale esecuzione ripresa\n", UDP);
		}

		if(DEBUG)printf("%sMi preparo a ricevere dati in udp\n", UDP);
		addrlen = sizeof(client_temp);
		memset(&client_temp, 0, sizeof(addrlen));
		ret = recvfrom(socket_udp, RECEIVE, BUFFERSIZE, 0, (struct sockaddr*) &client_temp, (socklen_t*) &addrlen);
		ERROR_HELPER(ret, "Errore nella recvfrom");

		if(ret == 0) continue; //NESSUNA RISPOSTA
		if(DEBUG)printf("%sAnalizzo i dati ricevuti\n", UDP);
		msg_len = Packet_handler_udp(RECEIVE, SEND, client_temp);
		if(DEBUG)printf("%sPacchetto analizzato, msg_len %d\n", UDP, msg_len);

		if(msg_len < 0) if(DEBUG)printf("%sQuesto aggiornamento non è stato processato\n", UDP);
		sleep(1);
	}


	pthread_exit(NULL);
}



int main(int argc, char **argv) {
	//CODICE NON MIO
  //NON PRENDE PIÙ LE TEXTURE DA STDIN
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


	//CODICE MIO
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
  shouldCommunicate = 1;
  shouldAccept			= 1;

  /** INIZIALIZZO SEMAFORO USER_SEM **/
  if(DEBUG)printf("%sInizializzazione semaforo\n", SERVER);
	user_sem = calloc(1, sizeof(sem_t));
	ret = sem_init(user_sem, 0, 1);
	ERROR_HELPER(ret, "Errore inizializzazione semaforo");

	/** INIZIALIZZO LINKED LIST DEGLI UTENTI CONNESSI **/
	if(DEBUG)printf("%sInizializzazione utenti\n", SERVER);
	user_on = calloc(1, sizeof(ListHead));
	List_init(user_on);

	/** PARTE PARALLELA **/
  if(DEBUG)printf("%sCreazione thread per la gestione delle comunicazioni\n",SERVER);
  pthread_t tcp_handler;

  struct acceptTcp argTcp;

  argTcp.tcp_sock 				= socket_tcp;
  argTcp.surface_texture 	= surface_texture;
  argTcp.elevation_texture= surface_elevation;
  argTcp.vehicle_texture 	= vehicle_texture;


	if(DEBUG)printf("%sThread TCP creato\n",SERVER);
  ret = pthread_create(&tcp_handler, NULL, tcp_accept, (void*) &argTcp);
  PTHREAD_ERROR_HELPER(ret, "Errore nella creazione del thread");
  if(DEBUG)printf("%sThread UDP creato\n",SERVER);
  //ret = pthread_create(&udp_handler, NULL, udp_communicate, (void*) &socket_udp);
  //THREAD_ERROR_HELPER(ret, "Errore nella creazione del thread");

  /** PARTE DELLA GESTIONE SEGNALI**/
  struct sigaction act;
  memset(&act, '\0', sizeof(act));

  act.sa_sigaction = &action;
  act.sa_flags		 = SA_SIGINFO;

	/** DELEGO IL LA FUNZIONE ACTION DI AGIRE QUANDO VENGONO INVOCATI TALI SEGNALI **/
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
	//PTHREAD_ERROR_HELPER(ret, "Errore nella detach");

  ret = pthread_create(&tcp_handler, NULL, tcp_accept, (void*) &argTcp);
  PTHREAD_ERROR_HELPER(ret, "Errore nella creazione del thread");
  //ret = pthread_create(&udp_handler, NULL, udp_communicate, (void*) &socket_udp);
  //PTHREAD_ERROR_HELPER(ret, "Errore nella creazione del thread");


	/** DELEGO IL LA FUNZIONE ACTION DI AGIRE QUANDO VENGONO INVOCATI TALI SEGNALI **/
	ret = sigaction(SIGINT, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");

	ret = sigaction(SIGQUIT, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");

	ret = sigaction(SIGTERM, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");

	ret = sigaction(SIGALRM, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");

	alarm(2);

	/** ASPETTO LA FINE DEI THREAD **/
	ret = pthread_join(tcp_handler, NULL);
	PTHREAD_ERROR_HELPER(ret, "Errore nella detach");
	//ret = pthread_join(udp_handler, NULL);
	//PTHREAD_ERROR_HELPER(ret, "Errore nella detach");

	/** DEALLOCAZIONE RISOSE **/
	if(DEBUG)printf("%sDealloco la lista utenti e chiudo file descriptors\n", SERVER);
	ListItem* users = user_on->first;
	ListItem* temp;

	for(int i = 0; i < user_on->size; i++){
		List_detach(user_on, users);
		temp = users;
		if(i != user_on->size - 1) users = users->next;
		close(temp->idx);
		Image_free(temp->vehicle_texture);
	}

	free(users);
	free(user_on);
	close(socket_tcp);
	close(socket_udp);

	if(DEBUG)printf("%sDealloco e distruggo semafori\n", SERVER);
	sem_destroy(user_sem);
	free(user_sem);

	if(DEBUG)printf("%sDealloco immagini\n",SERVER);

	Image_free(surface_elevation);
	Image_free(surface_texture);
	Image_free(vehicle_texture);

	/** FINE PROGRAMMA **/
	exit(EXIT_SUCCESS);
}
