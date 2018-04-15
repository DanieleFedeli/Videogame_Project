#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>


#include "handler.h"
#include "world.h"
#include "utils.h"
#include "so_game_protocol.h"

int shouldCommunicate = 1, shouldUpdate = 0;
int init = 0;
Image* surface;
Image* elevation;
World w;

/** FUNZIONI PER SETTARE VARIABILI **/
void __init__(Image* surface_t, Image* elevation_t){
	World_init(&w, surface_t, elevation_t, 0.5, 0.5, 0.5);
	surface = surface_t;
	elevation = elevation_t;
	init = 1;
}

int set_global_communicate(){
	shouldCommunicate = 1;
	return shouldCommunicate;
}

int set_global_update(){
	shouldUpdate = 1;
	return shouldUpdate;
}

int reset_update(){
	shouldUpdate = 0;
	return shouldUpdate;
}

int reset_communicate(){
	shouldCommunicate = 0;
	return shouldCommunicate;
}

int get_global_communicate(){
	return shouldCommunicate;
}

int get_global_update(){
	return shouldUpdate;
}

/** HANDLER PER PACCHETTI RICEVUTI VIA TCP PER SERVER **/
int server_tcp_packet_handler(char* PACKET, char* SEND, Vehicle* v, int id, Image** texture,const struct args* arg){
	int msg_len = 0;
	PacketHeader* header = (PacketHeader*)PACKET;

	if(header->type == GetId){
		if(DEBUG)printf("%sRichiesta ID ricevuta!\n", TCP);
		IdPacket* id_packet = (IdPacket*)Packet_deserialize(PACKET, header->size);

		if(id_packet->id != -1){
			Packet_free(&id_packet->header);
			return 0;
		}
		Packet_free(&id_packet->header);

		IdPacket* toSend = (IdPacket*) calloc(1, sizeof(IdPacket));
		PacketHeader head;

		head.type=GetId;
		toSend->id = id;
		toSend->header = head;
		
		msg_len = Packet_serialize(SEND, &toSend->header);
		
		Packet_free(&toSend->header);
		return msg_len;
	}

	else if(header->type == GetTexture){
		if(DEBUG)printf("%sRichiesta GET TEXTURE ricevuta!\n", TCP);
		ImagePacket* img_packet = (ImagePacket*)Packet_deserialize(PACKET, header->size);
		ImagePacket* toSend = (ImagePacket*) calloc(1, sizeof(ImagePacket));
		PacketHeader head;
		
		if(img_packet->id > 0){
			sleep(3);
			if(DEBUG)printf("%sCerco texture di %d!\n", TCP, img_packet->id);
			sem_wait(arg->create_sem);
			Vehicle* v = World_getVehicle(&w, img_packet->id);
			head.type = PostTexture;
			toSend->image = v->texture;
			toSend->id 		= v->id;
			toSend->header= head;
			msg_len = Packet_serialize(SEND, &toSend->header);
			sem_post(arg->create_sem);
			return msg_len;
		}
		if(DEBUG)printf("%sRichiesta surface!\n", TCP);
		
		head.type	 		= PostTexture;
		toSend->image = surface;
		toSend->id		= 0;
		toSend->header= head;

		msg_len = Packet_serialize(SEND, &toSend->header);
		return msg_len;
	}

	else if(header->type == GetElevation){
		if(DEBUG)printf("%sRichiesta GET ELEVATION ricevuta!\n", TCP);
		ImagePacket* toSend = (ImagePacket*) calloc(1, sizeof(ImagePacket));
		PacketHeader head;

		head.type			= PostElevation;
		toSend->image = elevation;
		toSend->id		= 0;
		toSend->header= head;
		
		msg_len = Packet_serialize(SEND, &toSend->header);
		//free(toSend);
		return msg_len;
	}

	else if(header->type == PostTexture){
		if(DEBUG)printf("%sRichiesta POST TEXTURE ricevuta!\n", TCP);
		ImagePacket* img_packet = (ImagePacket*)Packet_deserialize(PACKET, header->size);
		sem_wait(arg->create_sem);
		Vehicle_init(v, &w, id, img_packet->image);
		World_addVehicle(&w, v);
		sem_post(arg->create_sem);
		sem_post(arg->udptcp_sem);
		return 0;
	}
	
	else{
		if(DEBUG)printf("%sERORRE PACCHETTO!\n", TCP);
		return -1;
	}
}

int server_udp_packet_handler(char* PACKET,const struct args* arg){
	PacketHeader* header = (PacketHeader*)PACKET;
	if(header->type == VehicleUpdate){
		VehicleUpdatePacket* vup = (VehicleUpdatePacket*)header;
		
		sem_wait(arg->create_sem);
		Vehicle* v = World_getVehicle(&w, vup->id);
		v->rotational_force_update = vup->rotational_force;
		v->translational_force_update = vup->translational_force;
		v->x = vup->x;
		v->y = vup->y;
		v->theta = vup->theta;
		sem_post(arg->create_sem);
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
	int shouldThread = 1;
	
	/** INSERISCO NELLA USER LIST CON UN NUOVO USER**/
	Vehicle* v = (Vehicle*) calloc(1, sizeof(Vehicle));
	Image* texture;
	int count = 0;
	
	/** QUESTO CICLO SERVE PER LEGGERE COSA VUOLE IL CLIENT, PROCESSARE LA RISPOSTA ED INVIARLA **/
	while(shouldCommunicate && shouldThread && count < 5){
		bytes_read = 0;
		char RECEIVE[BUFFERSIZE], SEND[BUFFERSIZE];

		/** RICEVO DAL SOCKET**/
		bytes_read = recv(socket, RECEIVE, BUFFERSIZE, MSG_NOSIGNAL);
		if(bytes_read == -1 && errno != EINTR) shouldCommunicate = 0;
		
		if(bytes_read == 0){
			count++;
			continue;	
		}
		count ^= count; 
		
		/** CHIAMO UN HANDLER PER GENERARE LA RISPOSTA**/
		msg_len = server_tcp_packet_handler(RECEIVE, SEND, v, tcpArg.idx, &texture, &tcpArg);
		
		/** INVIO RISPOSTA AL CLIENT**/
		if(msg_len <= 0){
			count++;
			continue;
		}
		
		msg_len = send(socket, SEND, msg_len, MSG_NOSIGNAL);
		ERROR_HELPER(msg_len, "Errore nella send");
	}

	/** SE ESCO DAL CICLO SIGNIFICA CHE DEVO CHIUDERE IL PROGRAMMA, MI PREPARO A DEALLOCARE RISORSE**/
	sem_wait(tcpArg.create_sem);
	World_detachVehicle(&w, v);
	sem_post(tcpArg.create_sem);
	
	pthread_exit(NULL);
}

void * server_udp_routine(void* arg){
	struct sockaddr client = {0};
	struct args param = *(struct args*)arg;
	char* RECEIVE;
	char* SEND;
	int ret = 0, msg_len = 0, addrlen, id = param.idx;
	
	int socket_udp;
	shouldUpdate = 1;
	/** CREO SOCKET PER LA UDP +*/
	socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
	ERROR_HELPER(ret, "Errore nella socket");
	
	/** SETTO IL SOCKET RIUSABILE, IN SEGUITO A CRASH POTRÀ ESSERE RIUSATO **/
	int reuseaddr_opt = 1;
	ret = setsockopt(socket_udp, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
	ERROR_HELPER(ret, "Errore socketOpt");
	
	/** EFFETTO IL BINDING DELL'INDIRIZZO AD UN INTERFACCIA **/
	struct sockaddr_in my_addr = {0};
	my_addr.sin_family			= AF_INET;
	my_addr.sin_port				= htons(SERVER_PORT);
	my_addr.sin_addr.s_addr	= INADDR_ANY;
		
	ret = bind(socket_udp, (struct sockaddr*) &my_addr, sizeof(my_addr));
	ERROR_HELPER(ret, "Errore bind");
	
	sem_wait(param.create_sem);
	Vehicle* v = World_getVehicle(&w, id);
	v->list.socket_udp = socket_udp;
	sem_post(param.create_sem);
	
	while(shouldCommunicate){
		RECEIVE = (char*)calloc(BUFFERSIZE, sizeof(char));
		SEND = (char*)calloc(BUFFERSIZE, sizeof(char));
		
		addrlen = sizeof(struct sockaddr);
		ret = recvfrom(socket_udp, RECEIVE, BUFFERSIZE, 0, &client, (socklen_t*) &addrlen);
		if(ret == 1) shouldCommunicate = 0;
		
		server_udp_packet_handler(RECEIVE,&param);
		sem_wait(param.create_sem);
		v->addr = client;
		sem_post(param.create_sem);
		
		//CREO IL PACCHETTO
		WorldUpdatePacket* wup = (WorldUpdatePacket*)calloc(1, sizeof(WorldUpdatePacket));
		PacketHeader header;
			
		header.type = WorldUpdate;
		wup->header = header;
		
		sem_wait(param.create_sem);	
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
		sem_post(param.create_sem);
		//INVIO IL PACCHETTO
		msg_len = Packet_serialize(SEND, &wup->header);
		
		sem_wait(param.create_sem);
		current = (Vehicle*) World_getVehicle(&w, id);	
		addrlen = sizeof(struct sockaddr);
		ret = sendto(current->list.socket_udp, SEND, msg_len, 0, &client, (socklen_t) addrlen);
		sem_post(param.create_sem);
		ERROR_HELPER(ret, "Errore nella send to");
		Packet_free(&wup->header);
		
		free(SEND);
		free(RECEIVE);
	}
	
	sem_wait(param.create_sem);
	World_detachVehicle(&w, v);
	sem_post(param.create_sem);
	pthread_exit(NULL);
}


/** FUNZIONI DI CIRCOSTANZA **/
void print_all_user(){
	
	Vehicle* v = (Vehicle*) w.vehicles.first;
	int length = w.vehicles.size;
	printf("%sSTAMPA USER:\nSize: %d ",SERVER, length);
	for(int i = 0; i < length; i++){
		
		printf("(ID: %d P_IMG: %p) ", v->id, v->texture);
		ListItem* l = &v->list;
		v = (Vehicle*) l->next;
	}
	
	printf("\n");
}

void quit_server(){
	if(shouldCommunicate) shouldCommunicate = 0;
	World_destroy(&w);
}

