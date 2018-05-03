#include <GL/glut.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <wait.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include "world_viewer.h"
#include "image.h"
#include "world.h"
#include "vehicle.h"
#include "so_game_protocol.h"
#include "utils.h"


sem_t *world_sem, *request;
char* server_address;
int shouldUpdate, window, socket_tcp, socket_udp, my_id;
Image *map_elevation, *map_texture, *my_texture;
time_t curr_time;
World world;		
Vehicle* vehicle; 


/**FUNZIONE CHIAMATA ALLA RICEZIONE DI SIGINT/SIGQUIT/SIGTERM/SIGSEGV DA PARTE DEL CLIENT**/
void action(int sig, siginfo_t *siginfo, void* context){
	switch(sig){
		case SIGQUIT:
		case SIGTERM:
		case SIGINT:
		{
			/** SO CHE NON CI VANNO FUNZIONI PESANTI NELL'HANDLER DEI SEGNALI,
			 * MA ERO OBBLIGATO VISTO CHE NON SI PUÒ USCIRE DAL CICLO GLUT*/
			shouldUpdate = 0;
			sleep(1);
			
			close(socket_tcp);
			close(socket_udp);
			
			sem_destroy(world_sem);
			sem_destroy(request);
			free(world_sem);
			free(request);
			World_destroy(&world);

			
			exit(0);
		}
		
		case SIGSEGV:
		{
			printf("%sSEGMENTATION\n", CLIENT);
			shouldUpdate = 0;
			sleep(2);
			
			close(socket_tcp);
			close(socket_udp);
			sem_destroy(world_sem);
			sem_destroy(request);
			free(world_sem);
			free(request);
			World_destroy(&world);
			
			//Image_free(map_elevation);
			//Image_free(map_texture);			
			exit(0);
		}
		
	}
}

/** FUNZIONE USATA PER OTTENERE VARIABILI DI UN VEICOLO APPENA CONNESSO **/
void* getter(void* arg){
	/** DICHIARAZIONI VARIABILI NECESSARIE**/
	int ret, msg_len;
	struct args param = *(struct args*)arg;
	char* RECEIVE = (char*)calloc(BUFFERSIZE, sizeof(char));
	char* SEND = (char*)calloc(BUFFERSIZE, sizeof(char));
	Vehicle* toAdd = (Vehicle*) calloc(1, sizeof(Vehicle));
	ImagePacket* image_packet;
	
	/** CICLO DI ACQUISIZIONE TEXTURE **/
	while(1){
		image_packet = (ImagePacket*)calloc(1, sizeof(ImagePacket));
		PacketHeader h;
		h.type = GetTexture;
		image_packet->header = h;
		image_packet->id = param.idx;
		image_packet->image = calloc(1, sizeof(Image));
		msg_len = Packet_serialize(SEND, &image_packet->header);
	
		ret = send(param.tcp_sock, SEND, msg_len, 0);
		ERROR_HELPER(ret, "Errore nella send");
		ret = recv(param.tcp_sock, RECEIVE, BUFFERSIZE, MSG_WAITALL);
		ERROR_HELPER(ret, "Errore nella receive");
		
		time(&curr_time);
		fprintf(stderr, "%s%sRicevuti %d bytes da parte di %d\n", ctime(&curr_time), CLIENT, ret, my_id);
		h = *(PacketHeader*)RECEIVE;
		image_packet = (ImagePacket*)Packet_deserialize(RECEIVE, ret);
		
		if(ret > 0) break;
		//if(h.size > 150000 && ret > 150000 && h.size < 400000) break;
		// Molte volte capitava di ricevere texture corrotte, facendo crashare il client
		// in questo modo si riducono le probabilità al minimo
	}
	
	/**CICLO FINITO -> AGGIUNGO USER **/
	sem_wait(world_sem);
	time(&curr_time);
	fprintf(stderr, "%s%sAggiungo user cond id %d da parte di %d\n", ctime(&curr_time), CLIENT, param.idx, my_id);
	Vehicle_init(toAdd, &world, param.idx, image_packet->image);
	
	World_addVehicle(&world, toAdd);
	sem_post(world_sem);
	pthread_exit(NULL);
}

/** FUNZIONE USATA PER ANALIZZARE I PACCHETTI RICEVUTI VIA UDP **/
int packet_handler_udp(char* PACKET, char* SEND){
	//PACKET contiene già il pacchetto, mentre SEND conterrà 
	//il pacchetto serializzato da inviare se necessario
	/** DICHIARAZIONI VARIABILI NECESSARIE**/
	PacketHeader* h = (PacketHeader*)PACKET;
	struct args* param;
	int ret;
	if(h->type == WorldUpdate){
		/** RICEVUTO UN PACCHETTO WORLD UPDATE**/
		WorldUpdatePacket* wup = (WorldUpdatePacket*) Packet_deserialize(PACKET, h->size);
		/** AGGIORNO OGNI VEICOLO CONTENTENTE IL PACCHETTO**/
		for(int i = 0; i < wup->num_vehicles; i++){
			sem_wait(world_sem);
			Vehicle* v = World_getVehicle(&world, wup->updates[i].id);
			sem_post(world_sem);
			if(v == vehicle) continue; //Se il veicolo sono io, passa al prossimo (Ho gia tutti i dati che mi servono)
			if(v == NULL){ //Se il veicolo è null, aggiungilo al world
				/** VEICOLO NON TROVATO NEL WORLD, LANCIO LA ROUTINE GETTER**/
				time(&curr_time);
				fprintf(stderr, "%s%sCreo thread per aggiungere veicolo %d da parte di %d\n", ctime(&curr_time), CLIENT, wup->updates[i].id, my_id);
				pthread_t get;
				param = (struct args*) calloc(1, sizeof(struct args));
				param->idx = wup->updates[i].id;
				param->tcp_sock = socket_tcp;
				ret = pthread_create(&get, NULL, getter, (void*) param);
				PTHREAD_ERROR_HELPER(ret, "Errore nella generazione thread");
				
				ret = pthread_join(get, NULL);
				time(&curr_time);
				fprintf(stderr, "%s%sThread concluso da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
				PTHREAD_ERROR_HELPER(ret, "Errore nella detach");
				
			}
			else{
				/** IL VEICOLO ESISTE E AGGIORNO I SUOI DATI**/
				
				sem_wait(world_sem);
				v->theta 	= wup->updates[i].theta;
				v->x 			= wup->updates[i].x;
				v->y 			= wup->updates[i].y;
				sem_post(world_sem);
			}
		}
		
		if(world.vehicles.size == wup->num_vehicles) {
			Packet_free(&wup->header);
			return 0; //NON CI SONO VEICOLO INATTIVI
		}
		
		/** ROUTINE PER RICERCARE IL VEICOLO INATTIVO ED ELIMINARLO**/
		time(&curr_time);
		fprintf(stderr, "%s%sAlla ricerca di un veicolo inattivo da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
		sem_wait(world_sem);
		Vehicle* current = (Vehicle*) world.vehicles.first;
		
		//SCORRO TUTTI I VEICOLI PRESENTI NEL WORLD
		//SE VENGONO TROVATI ALL'INTERNO DEL PACCHETTO
		//LA VARIABILE "IN" VIENE SETTATA AD 1
		//A FINE CICLO SE LA VAR "IN" È SETTATA A 0
		//SI ELIMINA IL VEICOLO.
		
		for(int i = 0; i < world.vehicles.size; i++){
			int in = 0, forward = 0;
			for(int j = 0; j < wup->num_vehicles && !in; j++){
				if(current->id == wup->updates[j].id){
					in = 1;
				}
			}
			
			if(!in){
				time(&curr_time);
				fprintf(stderr, "%s%sElimino veicolo %d da parte di %d\n", ctime(&curr_time), CLIENT, current->id, my_id);
				Vehicle* toDelete = current;
				World_detachVehicle(&world, toDelete);
				current = (Vehicle*) current->list.next;
				forward = 1;
				free(toDelete);
			}
			
			if(!forward) current = (Vehicle*) current->list.next;	
		}
		
		sem_post(world_sem);
		Packet_free(&wup->header);
		return 0;
		
	}
	
	else return -1;
}

/** ROUTINE LANCIATA DAL THREAD UDP**/
void* client_udp_routine(void* arg){
	/** DICHIARAZIONI VARIABILI DI RETE**/
	struct sockaddr_in addr = {0};
	struct args* param = (struct args*)arg;
	char* SEND;
	char* RECEIVE;
	int length, socket_udp, ret;
	
	addr.sin_family 			= AF_INET;
  addr.sin_port 				= htons(SERVER_PORT);
  addr.sin_addr.s_addr 	= inet_addr(server_address);
	
	socket_udp = param -> udp_sock;
	
	/** CICLO DI ACQUISIZIONI DATI **/
	while(shouldUpdate){
		SEND 		= (char*) calloc(BUFF_UDP, sizeof(char));
		
		/** CREAZIONE PACCHETTO **/
		PacketHeader header;
		VehicleUpdatePacket* vup = (VehicleUpdatePacket*)calloc(1, sizeof(VehicleUpdatePacket));
		
		vup -> id 									= vehicle->id;
		vup -> rotational_force 		= vehicle->rotational_force_update;
		vup -> translational_force	= vehicle->translational_force_update;
		vup -> x										= vehicle->x;
		vup -> y										= vehicle->y;
		vup -> theta 								= vehicle->theta;
		
		header.type = VehicleUpdate;
		vup -> header = header;
		
		/** SERIALIZZAZIONE ED INVIO PACCHETTO**/
		length = Packet_serialize(SEND, &vup->header);
		ret = sendto(socket_udp, SEND, length, 0,(struct sockaddr*) &addr, length);
		ERROR_HELPER(ret, "Errore nella sendto");
		length = sizeof(struct sockaddr);
		RECEIVE = (char*) calloc(BUFF_UDP, sizeof(char));
		ret = recvfrom(socket_udp, RECEIVE, BUFF_UDP, 0,(struct sockaddr*) &addr, (socklen_t*) &length);
		if(ret == -1){
			time(&curr_time);
			fprintf(stderr, "%s%sDISCONNESSIONE PER TIMEOUT da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
			shouldUpdate = 0;
		}
		
		if(ret == 0) continue;
		packet_handler_udp(RECEIVE, SEND);
		
		/** DEALLOCO RISORSE**/
		Packet_free(&vup->header);
		free(SEND);
		free(RECEIVE);
	}
	
	/** CLIENT DISCONNESSO **/
	time(&curr_time);
	fprintf(stderr, "%s%sDisconnessione da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
	
	close(param->logger_pipe);
	free(arg);
	
	pthread_exit(NULL);
}

/** FUNZIONE USATA APPENA IL CLIENT SI CONNETTE VIA TCP PER RICHIEDERE TEXTURE **/
int getter_TCP(void){
	//VIENE USATA SLEEP(1) ALLA FINE DI OGNI RICHIESTA AL FINE DI NON FAR ACCAVALLARE
	//LE RICHIESTA AL CLIENT
	
  char* id_buffer 					= (char*) calloc(BUFFERSIZE, sizeof(char)); //USATO PER TRANSAZIONI DI ID
  char* image_packet_buffer = (char*) calloc(BUFFERSIZE, sizeof(char)); //USATO PER TRANSAZIONE DI IMG
  int ret, length, result = 1, count = 0;
  PacketHeader header, im_head;
  
  while(result && count < 5){
  /** ID PART**/
	  time(&curr_time);
	  fprintf(stderr, "%s%sPreparazione richista ID da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
	  IdPacket* id_packet = (IdPacket*)calloc(1, sizeof(IdPacket));
	
	  header.type   			= GetId;
	  id_packet -> header = header;
	  id_packet -> id     = -1;
		
	  length = Packet_serialize(id_buffer, &id_packet->header);
	  time(&curr_time);
	  fprintf(stderr, "%s%sBytes scritti nel buffer %d da parte di %d\n", ctime(&curr_time), CLIENT, length, my_id);
		
		ret = send(socket_tcp, id_buffer, length, 0);
		ERROR_HELPER(ret, "Errore nella send");
		Packet_free(&id_packet->header);
		
		ret = recv(socket_tcp, id_buffer, length, MSG_WAITALL);
		ERROR_HELPER(ret, "Errore nella recv");
		
	  id_packet = (IdPacket*) Packet_deserialize(id_buffer, length);
	  my_id = id_packet -> id;
	  time(&curr_time);
	  fprintf(stderr, "%s%sID trovato da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
	  Packet_free(&id_packet->header);
	  
	  if(my_id > 0) result = 0;
	  else count++;
	}
	
	result = 1;
	if(count >= 5) return -1;
	count ^= count;
	sleep(1); //SLEEP DOPO OGNI RICHIESTA PER NON FAR ACCAVALLARE LE RICHIESTE
	

	/** POST TEXTURE **/
	while(result && count < 5){
		time(&curr_time);
		fprintf(stderr, "%s%sPreparazione alla richiesta PostTexture da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
	  ImagePacket* image_packet = (ImagePacket*)calloc(1, sizeof(ImagePacket));
	  im_head.type = PostTexture;
		
		image_packet->id		= my_id;
	  image_packet->header= im_head;
	  image_packet->image = my_texture;
		
	  length = Packet_serialize(image_packet_buffer, &image_packet->header);
			
		ret = send(socket_tcp, image_packet_buffer, length, 0);
		time(&curr_time);
		fprintf(stderr, "%s%sRichiesta inviata da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
		Packet_free(&image_packet->header);
		free(image_packet_buffer);
		
		if(ret == length) result = 0;
		else count++;
	}
	
	result = 1;
	if(count >=5 ) return -1;
	count ^= count;
	
	sleep(1);
	
	/** ELEVATION MAP **/
	while(result && count < 5){
		time(&curr_time);
		fprintf(stderr, "%s%sPreparazione alla richiesta elevation map da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
		image_packet_buffer = (char*) calloc(BUFFERSIZE, sizeof(char));
		ImagePacket* image_packet = (ImagePacket*) calloc(1, sizeof(ImagePacket));
		
	  im_head.type = GetElevation;
	
	  image_packet -> header = im_head;
	  image_packet -> image = NULL;
	  
	  length = Packet_serialize(image_packet_buffer, &image_packet->header);
		
		ret = send(socket_tcp, image_packet_buffer,length, 0);
		Packet_free(&image_packet->header);
		free(image_packet_buffer);
		
		image_packet_buffer = (char*)calloc(BUFFERSIZE, sizeof(ImagePacket*));
		ret = recv(socket_tcp, image_packet_buffer, BUFFERSIZE, MSG_WAITALL);
		time(&curr_time);
		fprintf(stderr, "%s%sBuffer ricevuto di %d bytes da parte di %d\n", ctime(&curr_time), CLIENT, ret, my_id);
		
		image_packet = (ImagePacket*) Packet_deserialize(image_packet_buffer, ret);
		map_elevation = image_packet -> image;
		Packet_free(&image_packet->header);
		free(image_packet_buffer);
		
		time(&curr_time);
		fprintf(stderr, "%s%sElevation map ricevuta da parte di %d size: %d bytes\n", ctime(&curr_time), CLIENT, my_id, ret);
		
		if(map_elevation != NULL) result = 0;
		else count++;
	}
	
	result = 1;
	if(count >= 5) return -1;
	count ^= count;
	
	sleep(1);
	
	/**SURFACE MAP **/
	while(result && count < 5){
		time(&curr_time);
		fprintf(stderr, "%s%sPreparazione alla richiesta surface map da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
		image_packet_buffer = (char *) calloc(BUFFERSIZE, sizeof(char));
		ImagePacket* image_packet = (ImagePacket*) calloc(1, sizeof(ImagePacket));
		
		im_head.type = GetTexture;
		
		image_packet -> header = im_head;
		image_packet -> id = 0;
		
		length = Packet_serialize(image_packet_buffer, &image_packet->header);
	
		ret = send(socket_tcp, image_packet_buffer, length, 0);
		ERROR_HELPER(ret, "Errore nella send");
		Packet_free(&image_packet->header);
	
		ret = recv(socket_tcp, image_packet_buffer, BUFFERSIZE, MSG_WAITALL);
		ERROR_HELPER(ret, "Errore nella recv");
		
		
		
		if(ret < 0){
			count++;
			continue;
		}
		
		image_packet = (ImagePacket*) Packet_deserialize(image_packet_buffer, ret);
		map_texture = image_packet -> image;
		free(image_packet_buffer);
		Packet_free(&image_packet->header);
		
		time(&curr_time);
		fprintf(stderr, "%s%sPreparazione alla richiesta surface map da parte di %d size: %d bytes\n", ctime(&curr_time), CLIENT, my_id, ret);
		
		free(id_buffer);
		
		if(map_texture != NULL) result = 0;
	}
	
	result = 0;
	if(count >= 5) return -1;
	else return 0;
}


int main(int argc, char **argv) {
  if (argc<3) {
    printf("usage: %s <server_address> <player texture>\n", argv[1]);
    exit(-1);
  }

	int ret;
	my_texture = Image_load(argv[2]);
	
  /** DICHIARAZIONE VARIABILI DI RETE**/
  struct sockaddr_in addr = {0};
	
	server_address = argv[1];
	
	struct sigaction act;
	memset(&act, '\0', sizeof(act));
	
	act.sa_sigaction 	= &action;
	act.sa_flags			= SA_SIGINFO;
	
	ret = sigaction(SIGINT, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");
	ret = sigaction(SIGQUIT, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");
	ret = sigaction(SIGTERM, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");
	ret = sigaction(SIGSEGV, &act, NULL);
	ERROR_HELPER(ret, "Errore nella sigaction");
	
  socket_tcp = socket(AF_INET, SOCK_STREAM, 0);
	ERROR_HELPER(socket_tcp, "Errore creazione socket");
	
  addr.sin_family 			= AF_INET;
  addr.sin_port 				= htons(SERVER_PORT);
  addr.sin_addr.s_addr 	= inet_addr(server_address);
  
  /** COUNTDOWN PER CONNETTERSI, DOPO 5 TENTATIVI SMETTE**/
	int count = 0;
  while(count < 5){
		ret = connect(socket_tcp, (struct sockaddr*) &addr, sizeof(struct sockaddr_in));
		if(ret > -1) break;
		count++;
		sleep(1);
	}
	if(count > 4) ERROR_HELPER(ret, "Errore nella connect\n");
	count ^= count;
	
	time(&curr_time);
	fprintf(stderr, "%s%s Socket connesso via tcp\n", ctime(&curr_time), CLIENT);
  /** CONNESSIONE EFFETTUATA -> OTTIMIZZO LA RECV E RICHIEDO VARIABILI VIA TCP**/
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec= 0;
	ret = setsockopt(socket_tcp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval));
	ERROR_HELPER(ret, "Errore nella setsockopt");
	
	ret = getter_TCP();
	if(ret == -1){
		printf("%sERRORE ACQUISIZIONE DATI VIA TCP, ABORT!\n", CLIENT);
		exit(EXIT_FAILURE);
	}
	
  /** COSTRUISCO IL MONDO CON LE VARIABILI APPENA OTTENUTA **/
  time(&curr_time);
	fprintf(stderr, "%s%s Costruisco mondo da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle= (Vehicle*) malloc(sizeof(Vehicle));
  Vehicle_init(vehicle, &world, my_id, my_texture);
  World_addVehicle(&world, vehicle);
  
  /** PARTE UDP**/
  fprintf(stderr, "%s%sCreazione thread UDP da parte di %d\n", ctime(&curr_time), CLIENT, my_id);
  pthread_t udp_thread;
  int socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
	ERROR_HELPER(ret, "Impossibile creare socket UDP");
	
  struct args* arg = (struct args*) calloc(1, sizeof(struct args));
  arg -> idx = my_id;
	arg -> tcp_sock = socket_tcp;
	arg -> udp_sock = socket_udp;
	
	/**INIZIALIZZO SEMAFORO**/
	world_sem = (sem_t*) calloc(1, sizeof(sem_t));
	sem_init(world_sem, 0, 1);
	
	request = (sem_t*) calloc(1, sizeof(sem_t));
	sem_init(request, 0, 1);
	
	/** LANCIO THREAD UDP**/
	time(&curr_time);
	fprintf(stderr, "%s%sLancio routine UDP parte di %d\n", ctime(&curr_time), CLIENT, my_id);
	shouldUpdate = 1;
	ret = pthread_create(&udp_thread, NULL, client_udp_routine, arg);  
  PTHREAD_ERROR_HELPER(ret, "Errore nella creazioni del thread");
  
  ret = pthread_detach(udp_thread);
	PTHREAD_ERROR_HELPER(ret, "Errore nella detach");
	
	fprintf(stderr, "%s%sLancio worldviewer da parte di %d\n", ctime(&curr_time), CLIENT, my_id); 
  WorldViewer_runGlobal(&world, vehicle, &argc, argv);		
  return 0;

}
