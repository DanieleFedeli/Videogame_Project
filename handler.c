#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "handler.h"
#include "world.h"
#include "utils.h"
#include "so_game_protocol.h"

time_t curr_time;
int shouldCommunicate = 1, shouldUpdate = 0;
int shouldThread[MAX_USER];
Image* surface;
Image* elevation;
sem_t* world_sem, *thread_sem, *UDPEXEC, *cancelThread;
World w;

int set_global_update(){
	shouldUpdate = 1;
	return shouldUpdate;
}

/** FUNZIONI PER SETTARE VARIABILI **/
void __init__(Image* surface_t, Image* elevation_t, sem_t* world_sem_t, sem_t* thread_sem_t, sem_t* UDPEXEC_t){
	
	time(&curr_time);
	fprintf(stderr, "%s%s...Inizializzazione risorse\n", ctime(&curr_time), SERVER);
	World_init(&w, surface_t, elevation_t, 0.5, 0.5, 0.5);
	surface = surface_t;
	elevation = elevation_t;
	
	for(int i = 0; i < MAX_USER; i++){
		shouldThread[i] = 0;
	}
	
	cancelThread = (sem_t*) calloc(1, sizeof(sem_t));
	sem_init(cancelThread, 0, 0);
	
	UDPEXEC = UDPEXEC_t;
	world_sem = world_sem_t;
	thread_sem = thread_sem_t;
	
	time(&curr_time);
	fprintf(stderr, "%s%sRisorse inizializzate\n", ctime(&curr_time), SERVER);
}

/** HANDLER PER PACCHETTI RICEVUTI VIA TCP PER SERVER **/
int server_tcp_packet_handler(char* PACKET, char* SEND, Vehicle* v, int id, Image** texture,const struct args* arg){
	int msg_len = 0;
	PacketHeader header = *(PacketHeader*)PACKET;
	
	if(header.type == GetId){
		time(&curr_time);
		fprintf(stderr, "%s%sRichiesta id ricevuta da %d\n", ctime(&curr_time), TCP, id);
		IdPacket id_packet = *(IdPacket*)Packet_deserialize(PACKET, header.size);

		if(id_packet.id != -1) return 0;
		
		IdPacket toSend;
		PacketHeader head;

		head.type = GetId;
		toSend.id = id;
		toSend.header = head;
		
		msg_len = Packet_serialize(SEND, &toSend.header);
		return msg_len;
	}

	else if(header.type == GetTexture){
		time(&curr_time);
		fprintf(stderr, "%s%sRichiesta gettexture ricevuta da %d\n", ctime(&curr_time), TCP, id);
		ImagePacket img_packet = *(ImagePacket*)Packet_deserialize(PACKET, header.size);
		ImagePacket toSend;
		PacketHeader head;
		
		if(img_packet.id > 0){
			time(&curr_time);
			fprintf(stderr, "%s%sCerco texture di %d!\n", ctime(&curr_time), TCP, img_packet.id);
			sem_wait(world_sem);
			Vehicle* v = World_getVehicle(&w, img_packet.id);
			head.type = PostTexture;
			
			toSend.image = v->texture;
			toSend.id 		= v->id;
			toSend.header= head;
			msg_len = Packet_serialize(SEND, &toSend.header);
			sem_post(world_sem);
			
			return msg_len;
		}

		time(&curr_time);
		fprintf(stderr, "%s%sRichiesta getsurface ricevuta da %d!\n", ctime(&curr_time), TCP, id);
		
		head.type	 		= PostTexture;
		toSend.image = surface;
		toSend.id		= 0;
		toSend.header= head;

		msg_len = Packet_serialize(SEND, &toSend.header);
		return msg_len;
	}

	else if(header.type == GetElevation){		
		time(&curr_time);
		fprintf(stderr, "%s%sRichiesta getelevation ricevuta da %d!\n", ctime(&curr_time), TCP, id);
		ImagePacket toSend;
		PacketHeader head;

		head.type			= PostElevation;
		toSend.image = elevation;
		toSend.id		= 0;
		toSend.header= head;
		
		msg_len = Packet_serialize(SEND, &toSend.header);
		return msg_len;
	}

	else if(header.type == PostTexture){
		time(&curr_time);
		fprintf(stderr, "%s%sRichiesta posttexture ricevuta da %d!\n", ctime(&curr_time), TCP, id);
		ImagePacket img_packet = *(ImagePacket*)Packet_deserialize(PACKET, header.size);
		sem_wait(world_sem);
		Vehicle_init(v, &w, id, img_packet.image);
		World_addVehicle(&w, v);
		sem_post(UDPEXEC);
		sem_post(world_sem);
		
		SEND = memset((void*) PACKET, 0, BUFFERSIZE);
		return 0;
	}
	
	else{
		time(&curr_time);
		fprintf(stderr, "%s%sErrore pacchetto da %d!\n", ctime(&curr_time), TCP, id);
		return -1;
	}
}

int server_udp_packet_handler(char* PACKET,const struct args* arg){
	if(!shouldThread[arg->idx] || !shouldCommunicate) {
		time(&curr_time);
		fprintf(stderr, "%s%sCondizione nulla nel packet handler udp di %d\n", ctime(&curr_time), UDP, arg->idx);
		return 0;
	}
	PacketHeader* header = (PacketHeader*)PACKET;
	if(header->type == VehicleUpdate){
		time(&curr_time);
		VehicleUpdatePacket* vup = (VehicleUpdatePacket*)header;
		sem_wait(world_sem);
		Vehicle* v = World_getVehicle(&w, vup->id);
		v->rotational_force_update = vup->rotational_force;
		v->translational_force_update = vup->translational_force;
		v->x = vup->x;
		v->y = vup->y;
		v->theta = vup->theta;
		sem_post(world_sem);
		return 0;
	}
	
	else return -1;
}

/** ROUTINE PER I VARI THREAD DEL SERVER **/
void * server_tcp_routine(void* arg){
	
	/** DICHIARAZIONI VARIABILI **/
	struct args tcpArg = *(struct args*)arg;
	int socket = tcpArg.idx;
	int bytes_read, msg_len;
	sem_wait(thread_sem);	
	shouldThread[tcpArg.idx] = 1;
	sem_post(thread_sem);	

	/** INSERISCO NELLA USER LIST CON UN NUOVO USER**/
	Vehicle* v = (Vehicle*) calloc(1, sizeof(Vehicle));
	Image* texture;
	int count = 0;
	char SEND[BUFFERSIZE], RECEIVE[BUFFERSIZE] = {'\0'};
	
	memset((void*) &SEND, '\0', BUFFERSIZE);
	//memset((void*) &RECEIVE, '\0', BUFFERSIZE);
	
	/** QUESTO CICLO SERVE PER LEGGERE COSA VUOLE IL CLIENT, PROCESSARE LA RISPOSTA ED INVIARLA **/
	time(&curr_time);
	fprintf(stderr, "%s%sInizio ciclo tcp di %d\n", ctime(&curr_time), TCP, tcpArg.idx);
	while(shouldCommunicate && shouldThread[tcpArg.idx] && count < 5){
		bytes_read = 0;
		/** RICEVO DAL SOCKET**/
		bytes_read = recv(socket, RECEIVE, BUFFERSIZE, MSG_NOSIGNAL);

		if(bytes_read == 0){
			time(&curr_time);
			fprintf(stderr, "%s%sRicevuti %d bytes di %d. Disconnessione.\n", ctime(&curr_time), TCP, bytes_read, tcpArg.idx);
			break;
		}
		
		if(bytes_read == -1){
			time(&curr_time);
			fprintf(stderr, "%s%sRicevuti %d bytes di %d. Disconnessione.\n", ctime(&curr_time), TCP, bytes_read, tcpArg.idx);
			break;
		}
				
		/** CHIAMO UN HANDLER PER GENERARE LA RISPOSTA**/
		msg_len = server_tcp_packet_handler(RECEIVE, SEND, v, tcpArg.idx, &texture, &tcpArg);
		time(&curr_time);
		fprintf(stderr, "%s%sPacchetto analizzato da parte di %d\n", ctime(&curr_time), TCP, tcpArg.idx);
		
		/** INVIO RISPOSTA AL CLIENT**/
		if(msg_len < 1) continue;
		
		time(&curr_time);
		fprintf(stderr, "%s%sProvo ad inviare %d bytes da parte di %d\n", ctime(&curr_time), TCP, msg_len,tcpArg.idx);
		msg_len = send(socket, SEND, msg_len, 0);
		if(msg_len == -1 && errno != EINTR){
			time(&curr_time);
			fprintf(stderr, "%s%sTIMEOUT da parte di %d\n", ctime(&curr_time), TCP, tcpArg.idx);
			break;
		}
		time(&curr_time);
	}
	
	sem_wait(thread_sem);	
	time(&curr_time);
	fprintf(stderr, "%s%sSetto shouldThread a 0 da parte di %d\n", ctime(&curr_time), TCP, tcpArg.idx);
	shouldThread[tcpArg.idx] = 0;
	sem_wait(cancelThread);
	Vehicle* toDelete = World_detachVehicle(&w, v);
	Vehicle_destroy(toDelete);
	sem_post(thread_sem);	
	fprintf(stderr, "%s%sVeicolo %d eliminato\n", ctime(&curr_time), TCP, tcpArg.idx);
	
	
	time(&curr_time);
	fprintf(stderr, "%s%sChiusura thread da parte di %d\n", ctime(&curr_time), TCP, tcpArg.idx);
	
	close(socket);
	pthread_exit(NULL);
}

void * server_udp_routine(void* arg){
	struct sockaddr client = {0};
	struct args param = *(struct args*)arg;
	char RECEIVE[BUFF_UDP] = {'\0'}, SEND[BUFF_UDP] = {'\0'};
	int ret = 0, msg_len = 0, addrlen, id = param.idx;
	
	int socket_udp;
	shouldUpdate = 1;
	/** CREO SOCKET PER LA UDP +*/
	socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
	ERROR_HELPER(ret, "Errore nella socket");
	
	/** SETTO IL SOCKET RIUSABILE, IN SEGUITO A CRASH POTRÀ ESSERE RIUSATO **/
	int reuseaddr_opt = 1;
	
	struct timeval tv;
	tv.tv_sec 	= 15;
	tv.tv_usec 	= 0;
	
	ret = setsockopt(socket_udp, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
	ERROR_HELPER(ret, "Errore socketOpt");
	ret = setsockopt(socket_udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval));
	ERROR_HELPER(ret, "Errore socketOpt");
	
	/** EFFETTO IL BINDING DELL'INDIRIZZO AD UN INTERFACCIA **/
	struct sockaddr_in my_addr = {0};
	my_addr.sin_family			= AF_INET;
	my_addr.sin_port				= htons(SERVER_PORT);
	my_addr.sin_addr.s_addr	= INADDR_ANY;
		
	ret = bind(socket_udp, (struct sockaddr*) &my_addr, sizeof(my_addr));
	ERROR_HELPER(ret, "Errore bind");
	
	sem_wait(world_sem);
	Vehicle* v = World_getVehicle(&w, id);
	v->list.socket_udp = socket_udp;
	sem_post(world_sem);
	
	time(&curr_time);
	fprintf(stderr, "%s%sInizio comunicazione UDP da parte di %d\n", ctime(&curr_time), UDP, param.idx);
	
	while(shouldThread[id] && shouldCommunicate){	
		addrlen = sizeof(struct sockaddr);
		ret = recvfrom(socket_udp, RECEIVE, BUFF_UDP, 0, &client, (socklen_t*) &addrlen);
		if(ret == -1 && errno != EAGAIN){
			time(&curr_time);
			fprintf(stderr, "%s%sTIMEOUT da parte di %d\n", ctime(&curr_time), UDP, param.idx);
			ERROR_HELPER(ret, "ERRORE TIMEOUT");
		}
		
		server_udp_packet_handler(RECEIVE,&param);
		if(!shouldThread[id] || !shouldCommunicate) break;
		sem_wait(world_sem);
		v->addr = client;
		sem_post(world_sem);
			
		//CREO IL PACCHETTO
		WorldUpdatePacket* wup = (WorldUpdatePacket*)calloc(1, sizeof(WorldUpdatePacket));
		PacketHeader header;
			
		header.type = WorldUpdate;
		wup->header = header;
		
		sem_wait(world_sem);	
		wup->num_vehicles = w.vehicles.size;
		wup->updates = (ClientUpdate*) calloc(wup->num_vehicles, sizeof(ClientUpdate));
		Vehicle* current = (Vehicle*) w.vehicles.first;
		if(current == NULL) exit(EXIT_FAILURE);
		for(int i = 0; i < wup->num_vehicles; i++){
			wup->updates[i].id 		= current->id;
			wup->updates[i].theta = current->theta;
			wup->updates[i].x 		= current->x;
			wup->updates[i].y 		= current->y;
			ListItem* l = &current->list;
			current = (Vehicle*) l->next;
		}

		sem_post(world_sem);
		
		//INVIO IL PACCHETTO
		msg_len = Packet_serialize(SEND, &wup->header);
		sem_wait(world_sem);
		if(shouldThread[id] || shouldCommunicate){
			current = (Vehicle*) World_getVehicle(&w, id);	
			addrlen = sizeof(struct sockaddr);
			ret = sendto(current->list.socket_udp, SEND, msg_len, 0, &client, (socklen_t) addrlen);		
			char string[100];
			sprintf(string, "Errore nella send to id:%d\n", id);
			ERROR_HELPER(ret, "string");
		}
		sem_post(world_sem);
		
		//DEALLOCO RISORSE
		Packet_free(&wup->header);
		time(&curr_time);
		//usleep(10000);
	}
	
	sem_post(cancelThread);
	
	close(socket_udp);
	time(&curr_time);
	fprintf(stderr, "%s%sComunicazione udp terminata da parte di %d\n", ctime(&curr_time), UDP, param.idx);
	sem_wait(thread_sem);	
	shouldThread[id] = 0;
	sem_post(thread_sem);	
	
	pthread_exit(NULL);
}


/** FUNZIONI DI CIRCOSTANZA **/
void print_all_user(){
	
	printf("%sShouldThread:\t", SERVER);
	for(int i = 0; i < MAX_USER; i++){
		printf("[%d] := %d\t",i, shouldThread[i]);
	}
	printf("\n");
	
	sem_wait(world_sem);
	Vehicle* v = (Vehicle*) w.vehicles.first;
	int length = w.vehicles.size;
	char BUFFER[BUFFERSIZE];
	int ret, bytes_write = 0;
	for(int i = 0; i < length; i++){
		ret = sprintf(BUFFER + bytes_write, "(ID: %d P_IMG: %p) ", v->id, v->texture);
		ListItem* l = &v->list;
		v = (Vehicle*) l->next;
		bytes_write += ret;
	}
	sem_post(world_sem);
	time(&curr_time);
	fprintf(stderr, "%s%sLista utenti connessi: \t %s\n", ctime(&curr_time), SERVER, BUFFER);
}

void quit_server(){
	if(shouldCommunicate) shouldCommunicate = 0;
	sleep(3);
	
	World_destroy(&w);
	Image_free(surface);
	Image_free(elevation);
		
	sem_destroy(cancelThread);
	sem_destroy(thread_sem);
	sem_destroy(world_sem);
	sem_destroy(UDPEXEC);
	
	free(cancelThread);
	free(thread_sem);
	free(world_sem);
	free(UDPEXEC);
	time(&curr_time);
	fprintf(stderr, "%s%sRisorse deallocate\n", ctime(&curr_time), SERVER);
}


