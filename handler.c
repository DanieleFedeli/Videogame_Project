#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "linked_list.h"
#include "utils.h"
#include "so_game_protocol.h"

int shouldCommunicate = 1, shouldUpdate = 0;
int init = 0;
ListHead* user_on;
sem_t* user_sem;

/** FUNZIONI PER SETTARE VARIABILI **/
ListItem* add_user(int idx){
	sem_wait(user_sem);
	ListItem* toPut = (ListItem*) calloc(1, sizeof(ListItem));
	toPut -> idx = idx;
	List_insert(user_on, 0, toPut);
	return toPut;
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

void insert_texture(Image* toPut, int idx);

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

ListHead get_user_on(){
	return *user_on;
}

int get_global_update(){
	return shouldUpdate;
}

int destroy_resources(){
	return 0;
}

int __init__(){
	user_sem = calloc(1, sizeof(sem_t));
	sem_init(user_sem, 0, 1);
	
	user_on = (ListHead*) calloc(1, sizeof(ListHead));
	List_init(user_on);
	
	init = 1;
	return 1;
}

/** HANDLER PER PACCHETTI RICEVUTI VIA TCP PER SERVER **/
int server_tcp_packet_handler(char* PACKET, char* SEND, void* arg){
	struct args argTcp = *(struct args*)arg;
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
		toSend->image = argTcp.surface_texture;
		toSend->id		= argTcp.idx;
		toSend->header= head;
		if(DEBUG)printf("%sSerializzo!\n", TCP);

		msg_len = Packet_serialize(SEND, &toSend->header);
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
		toSend->image = argTcp.elevation_texture;
		toSend->id		= argTcp.idx;
		toSend->header= head;

		if(DEBUG)printf("%sSerializzo!\n", TCP);

		msg_len = Packet_serialize(SEND, &toSend->header);
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
		insert_texture(toPut, img_packet->id);
		
		Packet_free(&img_packet->header);
		
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
		
		ListItem* user = List_find_id(user_on, id);
		if(user == NULL) return -1;
		user->addr = *(struct sockaddr*)arg;
		
		return 0;
	}
	
	else if(header->type == VehicleUpdate){
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

		sem_post(user_sem);

		Packet_free(&vehicle_packet->header);
		return 0;
	}

	else return -1;
}

/** ROUTINE PER I VARI THREAD DEL SERVER **/
void * server_tcp_routine(void* arg){
	if(!init) __init__();
	
	/** DICHIARAZIONI VARIABILI **/
	struct args tcpArg = *(struct args*)arg;
	int socket = tcpArg.idx;
	int bytes_read, msg_len;
	int shouldThread = 1;

	/** INSERISCO NELLA USER LIST CON UN NUOVO USER**/
	if(DEBUG)printf("%sSocket in routine: %d\n", SERVER, socket);
	if(DEBUG)printf("%sAggiungo user\n",TCP);

	sem_wait(user_sem);
	ListItem* user = calloc(1, sizeof(ListItem));
	user->idx = tcpArg.idx;
	user->vehicle_texture = tcpArg.vehicle_texture;
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
		msg_len = server_tcp_packet_handler(RECEIVE, SEND, arg);
		if(DEBUG)printf("%sDevo inviare una stringa di %d bytes\n",TCP, msg_len);
		/** INVIO RISPOSTA AL CLIENT**/
		if(msg_len == 0) continue;

		send(socket, SEND, msg_len, 0);
		if(DEBUG)printf("%sInviata con successo\n",TCP);
		sleep(1); //USATA PER EVITARE L'USO AL 100% DELLA CPU
		
	}

	/** SE ESCO DAL CICLO SIGNIFICA CHE DEVO CHIUDERE IL PROGRAMMA, MI PREPARO A DEALLOCARE RISORSE**/
	if(DEBUG) printf("%sUtente %d disconnesso.\n", TCP, socket);

	sem_wait(user_sem);
	List_detach(user_on, user);
	sem_post(user_sem);

	free(user);
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
		msg_len = server_udp_packet_handler(RECEIVE, SEND, (void *) &client_temp);
		if(DEBUG)printf("%sPacchetto analizzato, msg_len %d\n", UDP, msg_len);

		if(msg_len < 0) if(DEBUG)printf("%sQuesto aggiornamento non è stato processato\n", UDP);
	}

	
	pthread_exit(NULL);
}

/** ROUTINE PER I VARI THREAD DEL CLIENT **/
void client_udp_routine(void* arg){
	struct args* param = (struct args*)arg;
	int socket_udp = param -> udp_sock;
	
	if(init == 0) __init__();
	int addrlen, n;
	char* BUFFER = (char*)calloc(BUFFERSIZE, sizeof(char));
	struct sockaddr_in server;
	memset((char *) &server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(SERVER_PORT);
	
	inet_aton(SERVER_IP, &server.sin_addr);
	addrlen = sizeof(server);
	
	first_udp_message(socket_udp, param->idx, server); //USATO PER FAR ACQUISIRE AL SERVER IL PROPRIO INDIRIZZO
	
	while(1){
			
		n = sendto(socket_udp, BUFFER, BUFFERSIZE, 0, (struct sockaddr*) &server, (socklen_t) addrlen);
		printf("BUFFER: %s\n", BUFFER);
		
	}
}

/** HANDLER PER PACCHETTI RICEVUTI VIA UDP PER CLIENT **/
int client_udp_packet_handler(char* PACKET, char* SEND, void* arg){
	//struct communicateTcp argTcp = *(struct communicateTcp*)arg;
	PacketHeader* header = (PacketHeader*)PACKET;

	if(DEBUG)printf("%sSpacchetto...\n",TCP);
	
	switch(header->type){
		case NewUser:{
			ImagePacket* img_packet = (ImagePacket*)Packet_deserialize(PACKET,header->size);
		
			add_user(img_packet -> id);
			insert_texture(img_packet->image, img_packet->id);
		
			Packet_free(&img_packet->header);
			return 0;
		}
		default: return -1;
	}
	
	return 0;
}

/** FUNZIONI DI CIRCOSTANZA **/
void print_all_user(){
	if(init == 0) __init__();
	ListItem* toPrint = user_on -> first;
	int length = user_on->size;
	printf("%sSTAMPA USER:\nSize: %d ",SERVER, length);
	for(int i = 0; i < length; i++){
		printf("(ID: %d P_IMG: %p) ", toPrint->idx, toPrint->vehicle_texture);
		toPrint = toPrint->next;
	}
	printf("\n");
}

void quit_server(){
	if(shouldUpdate) shouldUpdate = 0;
	if(shouldCommunicate) shouldCommunicate = 0;
	printf("ShouldUpdate = %d, ShouldCommunicate = %d\n", shouldUpdate, shouldCommunicate);
}

void instert_texture(Image* toPut, int idx){	
		if(DEBUG)printf("%sInserisco texture in userList\n", TCP);
		sem_wait(user_sem);
		ListItem* user 				= List_find_id(user_on, idx);
		user->vehicle_texture = toPut;
		sem_post(user_sem);
}
