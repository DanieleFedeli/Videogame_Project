#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

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

void first_udp_message(int socket_udp, int idx, struct sockaddr_in server){
		IdPacket* id_packet = (IdPacket*)calloc(1, sizeof(IdPacket));
		PacketHeader header;
		char BUFFER[BUFFERSIZE];
		header.type = GetId;
		id_packet->header = header;
		id_packet->id = idx;
		Packet_serialize(BUFFER, &id_packet->header);
		sendto(socket_udp, BUFFER, BUFFERSIZE, 0, (struct sockaddr*) &server, sizeof(server));
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
	if(DEBUG) printf("shouldUpdate: %d\n", shouldUpdate);
	return shouldUpdate;
}

int reset_communicate(){
	shouldCommunicate = 0;
	if(DEBUG) printf("shouldUpdate: %d\n", shouldUpdate);
	return shouldCommunicate;
}

int get_global_communicate(){
	return shouldCommunicate;
}

int get_global_update(){
	return shouldUpdate;
}

int destroy_resources(){
	return 0;
}

/** HANDLER PER PACCHETTI RICEVUTI VIA TCP PER SERVER **/
int server_tcp_packet_handler(char* PACKET, char* SEND,Vehicle* v, int id, Image** texture){
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
		toSend->id = id;
		toSend->header = head;

		if(DEBUG)printf("%sSerializzo!\n",TCP);
		
		msg_len = Packet_serialize(SEND, &toSend->header);
		
		Packet_free(&toSend->header);
		return msg_len;
	}

	else if(header->type == GetTexture){

		if(DEBUG)printf("%sRichiesta texture ricevuta\n", TCP);
		ImagePacket* img_packet = (ImagePacket*)Packet_deserialize(PACKET, header->size);
		ImagePacket* toSend = (ImagePacket*) calloc(1, sizeof(ImagePacket));
		PacketHeader head;
		
		if(img_packet->id > 0){
			Vehicle* v = World_getVehicle(&w, img_packet->id);
			head.type = PostTexture;
			toSend->image = v->texture;
			toSend->id 		= v->id;
			toSend->header= head;
			msg_len = Packet_serialize(SEND, &toSend->header);
			//Packet_free(&img_packet->header);
			return msg_len;
		}

		Packet_free(&img_packet->header);

		if(DEBUG)printf("%sPreparazione pacchetto da inviare\n", TCP);

		head.type	 		= PostTexture;
		toSend->image = surface;
		toSend->id		= 0;
		toSend->header= head;
		if(DEBUG)printf("%sSerializzo!\n", TCP);
		
		Vehicle_init(v, &w, id, *texture);
		msg_len = Packet_serialize(SEND, &toSend->header);
		return msg_len;
	}

	else if(header->type == GetElevation){
		if(DEBUG)printf("%sRichiesta elevation ricevuta\n", TCP);

		//Packet_free(header);

		if(DEBUG)printf("%sPreparazione pacchetto da inviare\n", TCP);
		ImagePacket* toSend = (ImagePacket*) calloc(1, sizeof(ImagePacket));
		PacketHeader head;

		head.type			= PostElevation;
		toSend->image = elevation;
		toSend->id		= 0;
		toSend->header= head;

		if(DEBUG)printf("%sSerializzo!\n", TCP);

		msg_len = Packet_serialize(SEND, &toSend->header);
		return msg_len;
	}

	else if(header->type == PostTexture){
		if(DEBUG)printf("%sPost texture ricevuta\n", TCP);
		ImagePacket* img_packet = (ImagePacket*)Packet_deserialize(PACKET, header->size);
		*texture = img_packet->image;
		return 0;
	}

	else return 0;
}

int server_udp_packet_handler(char* PACKET, char* SEND, void* arg){
PacketHeader* header = (PacketHeader*)PACKET;
	if(DEBUG)printf("%sSpacchetto...\n",UDP);
	
	if(header->type == GetId){
		IdPacket* id_packet = (IdPacket*)Packet_deserialize(PACKET, header->size);
		
		int id = id_packet->id;
		
		Vehicle* v = World_getVehicle(&w, id);
		v->list.addr = *(struct sockaddr*)arg;
		
		return 0;
	}
	
	else if(header->type == VehicleUpdate){
		if(DEBUG)printf("%sAggiornamento veicolo ricevuto\n", UDP);
		//DA DEFINIRE
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
	int ready = 0;
	
	/** INSERISCO NELLA USER LIST CON UN NUOVO USER**/
	if(DEBUG)printf("%sSocket in routine: %d\n", SERVER, socket);
	if(DEBUG)printf("%sAggiungo user\n",TCP);
	
	Vehicle* v = (Vehicle*) calloc(1, sizeof(Vehicle));
	Image* texture;
	if(!init) __init__(tcpArg.surface_texture, tcpArg.elevation_texture);
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
		
		msg_len = server_tcp_packet_handler(RECEIVE, SEND, v, tcpArg.idx, &texture);
		if(DEBUG)printf("%sDevo inviare una stringa di %d bytes\n",TCP, msg_len);
		/** INVIO RISPOSTA AL CLIENT**/
		if(msg_len == 0) continue;

		send(socket, SEND, msg_len, 0);
		if(DEBUG)printf("%sInviata con successo\n",TCP);
		sleep(1); //USATA PER EVITARE L'USO AL 100% DELLA CPU
		
	}

	/** SE ESCO DAL CICLO SIGNIFICA CHE DEVO CHIUDERE IL PROGRAMMA, MI PREPARO A DEALLOCARE RISORSE**/
	if(DEBUG) printf("%sUtente %d disconnesso.\n", TCP, socket);
	
	World_detachVehicle(&w, v);
	Vehicle_destroy(v);
	
	pthread_exit(NULL);
}

void * server_udp_routine(void* arg){
	
	struct sockaddr_in client_temp = {0};
	char RECEIVE[BUFFERSIZE], SEND[BUFFERSIZE];
	int ret, msg_len = 0, addrlen, socket_udp = *(int*)arg;
	shouldUpdate = 1;
	while(shouldCommunicate){
		sleep(1);
		if(shouldUpdate){
			//DA COMPLETARE
		}

		if(DEBUG)printf("%sMi preparo a ricevere dati in udp\n", UDP);
		addrlen = sizeof(client_temp);
		memset(&client_temp, 0, sizeof(addrlen));
		ret = recvfrom(socket_udp, RECEIVE, BUFFERSIZE, 0, (struct sockaddr*) &client_temp, (socklen_t*) &addrlen);
		ERROR_HELPER(ret, "Errore nella recvfrom");
		
		if(ret == 0) continue; //NESSUNA RISPOSTA
		if(DEBUG)printf("%sAnalizzo i dati ricevuti\n", UDP);
		msg_len = server_udp_packet_handler(RECEIVE, SEND, (void *) &client_temp);
		if(DEBUG)printf("%sPacchetto analizzato, msg_len %d\n", UDP, msg_len);

		if(msg_len < 0) if(DEBUG)printf("%sQuesto aggiornamento non è stato processato\n", UDP);
	}

	
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
	if(shouldUpdate) shouldUpdate = 0;
	if(shouldCommunicate) shouldCommunicate = 0;
	printf("ShouldUpdate = %d, ShouldCommunicate = %d\n", shouldUpdate, shouldCommunicate);
}
