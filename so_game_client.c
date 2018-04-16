#include <GL/glut.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>

#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "so_game_protocol.h"
#include "utils.h"

sem_t* world_sem;
sem_t* request;
int shouldUpdate = 1;
int window, socket_tcp, socket_udp;
World world;
Vehicle* vehicle; // The vehicle

void* getter(void* arg){
	int ret, msg_len;
	struct args param = *(struct args*)arg;
	char* RECEIVE = (char*)calloc(BUFFERSIZE, sizeof(char));
	char* SEND = (char*)calloc(BUFFERSIZE, sizeof(char));
	Vehicle* toAdd = (Vehicle*) calloc(1, sizeof(Vehicle));
	PacketHeader h;
	ImagePacket* image_packet = (ImagePacket*)calloc(1, sizeof(ImagePacket));
	
	h.type = GetTexture;
	image_packet->header = h;
	image_packet->id = param.idx;
	
	msg_len = Packet_serialize(SEND, &image_packet->header);
	//Packet_free(&image_packet->header);
	
	if(DEBUG)printf("%sPrima della send\n", CLIENT);
	ret = send(param.tcp_sock, SEND, msg_len, 0);
	ERROR_HELPER(ret, "Errore nella send");
	if(DEBUG)printf("%sPrima della rcv\n", CLIENT);
		
	ret = recv(param.tcp_sock, RECEIVE, BUFFERSIZE, 0);
	if(DEBUG)printf("%sDopo la recv\n", CLIENT);
	ERROR_HELPER(ret, "Errore nella receive");
	
	if(DEBUG)printf("%sRicevuti %d bytes..\n", CLIENT, ret);
	
	h = *(PacketHeader*)RECEIVE;
	image_packet = (ImagePacket*)Packet_deserialize(RECEIVE, h.size);
	
	sem_wait(world_sem);
	if(DEBUG)printf("%sAggiungo user con id:%d\n", CLIENT, param.idx);
	Vehicle_init(toAdd, &world, param.idx, image_packet->image);
	
	World_addVehicle(&world, toAdd);
	sem_post(world_sem);
	//free(RECEIVE);
	//free(SEND);
	pthread_exit(NULL);
}

int packet_handler_udp(char* PACKET, char* SEND){
	PacketHeader* h = (PacketHeader*)PACKET;
	struct args* param;
	int ret;
	if(h->type == WorldUpdate){
		if(DEBUG)printf("%sWorldUpdate..\n",CLIENT);
		WorldUpdatePacket* wup = (WorldUpdatePacket*) Packet_deserialize(PACKET, h->size);
		for(int i = 0; i < wup->num_vehicles; i++){
			sem_wait(world_sem);
			Vehicle* v = World_getVehicle(&world, wup->updates[i].id);
			sem_post(world_sem);
			if(v == vehicle) continue;
			if(v == NULL){
				if(DEBUG)printf("%sCreo thread..\n",CLIENT);
				pthread_t get;
				param = (struct args*) calloc(1, sizeof(struct args));
				param->idx = wup->updates[i].id;
				param->tcp_sock = socket_tcp;
				ret = pthread_create(&get, NULL, getter, (void*) param);
				PTHREAD_ERROR_HELPER(ret, "Errore nella generazione thread");
				
				ret = pthread_join(get, NULL);
				if(DEBUG)printf("%sThread concluso..\n",CLIENT);
				PTHREAD_ERROR_HELPER(ret, "Errore nella detach");
				
			}
			else{
				sem_wait(world_sem);
				if(DEBUG)printf("%sAggiorno veicolo %d..\n",CLIENT, wup->updates[i].id);
				v->theta 	= wup->updates[i].theta;
				v->x 			= wup->updates[i].x;
				v->y 			= wup->updates[i].y;
				sem_post(world_sem);
			}
			
			if(DEBUG)printf("%sSEMAFORO SGANCIATO IN HANDLER..\n",CLIENT);
		}
		
		
		return 0;
	}
	
	else return -1;
}

void* client_udp_routine(void* arg){
	struct sockaddr_in addr = {0};
	struct args* param = (struct args*)arg;
	char* SEND;
	char* RECEIVE;
	int length, socket_udp, ret;
	
	addr.sin_family 			= AF_INET;
  addr.sin_port 				= htons(SERVER_PORT);
  addr.sin_addr.s_addr 	= inet_addr(SERVER_IP);
	
	socket_udp = param -> udp_sock;
	if(DEBUG)printf("%sRoutine udp avviata..\n",CLIENT);
	while(shouldUpdate){
		SEND 		= (char*) calloc(BUFFERSIZE, sizeof(char*));
		RECEIVE = (char*) calloc(BUFFERSIZE, sizeof(char*));
		
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
		
		
		length = Packet_serialize(SEND, &vup->header);
		if(DEBUG)printf("%sPreparo pacchetto da inviare..\n",CLIENT);
		ret = sendto(socket_udp, SEND, length, 0,(struct sockaddr*) &addr, length);
		ERROR_HELPER(ret, "Errore nella sendto");
		if(DEBUG)printf("%sInviati %d bytes da id: %d..\n",CLIENT, ret, vehicle->id);
		length = sizeof(struct sockaddr);
		ret = recvfrom(socket_udp, RECEIVE, BUFFERSIZE, 0,(struct sockaddr*) &addr, (socklen_t*) &length);
		if(ret == -1){
			printf("%sDISCONNESSIONE PER TIMEOUT\n", CLIENT);
			shouldUpdate = 0;
		}
		if(ret == 0) continue;
		packet_handler_udp(RECEIVE, SEND);
		
		Packet_free(&vup->header);
		free(SEND);
		free(RECEIVE);
	}
	
	pthread_exit(NULL);
}

void getter_TCP(Image* my_texture, Image** map_elevation, Image** map_texture, int* my_id){
  char* id_buffer 					= (char*) calloc(BUFFERSIZE, sizeof(char)); //USATO PER TRANSAZIONI DI ID
  char* image_packet_buffer = (char*) calloc(BUFFERSIZE, sizeof(char)); //USATO PER TRANSAZIONE DI IMG
  int ret, length;
  PacketHeader header, im_head;
  
  /** ID **/
  if(DEBUG) printf("%sPreparazione richiesta ID...\n", CLIENT);
  IdPacket* id_packet = (IdPacket*)calloc(1, sizeof(IdPacket));

  header.type   			= GetId;
  id_packet -> header = header;
  id_packet -> id     = -1;
	
	//SERIALIZE PART
  length = Packet_serialize(id_buffer, &id_packet->header);
  printf("Bytes scritti nel buffer: %d\n", length);
	//Packet_free(&id_packet->header);
	
	if(DEBUG) printf("%sInvio...\n", CLIENT);
	//INVIO BUFFER
	ret = send(socket_tcp, id_buffer, length, 0);
	ERROR_HELPER(ret, "Errore nella send");

	if(DEBUG) printf("%sRicevo...\n", CLIENT);
	//RICEVO BUFFER
	ret = recv(socket_tcp, id_buffer, length, 0);
	ERROR_HELPER(ret, "Errore nella recv");
	
	//DESERIALIZE PART
  id_packet = (IdPacket*) Packet_deserialize(id_buffer, length);
  *my_id = id_packet -> id;
  if(DEBUG) printf("%sId trovato: %d\n", CLIENT, *my_id);

	sleep(1); //SLEEP DOPO OGNI RICHIESTA PER NON FAR ACCAVALLARE LE RICHIESTE
	

	/** POST TEXTURE **/
	if(DEBUG) printf("%sPreparazione richiesta post texture...\n", CLIENT);
  ImagePacket* image_packet = (ImagePacket*)calloc(1, sizeof(ImagePacket));
  im_head.type = PostTexture;
	
	image_packet->id		= *my_id;
  image_packet->header= im_head;
  image_packet->image = my_texture;
	
	//SERIALIZE PART
  length = Packet_serialize(image_packet_buffer, &image_packet->header);
  printf("Bytes scritti nel buffer: %d\n", length);
	//Packet_free(&image_packet->header);
		
	//INVIO BUFFER
	ret = send(socket_tcp, image_packet_buffer, length, 0);
	//free(image_packet_buffer);
	if(DEBUG) printf("%smy_texture: %p\n",CLIENT, my_texture);
	
	sleep(1); //NON HO BISOGNO DI RISPOSTA
	
	/** ELEVATION MAP **/
	if(DEBUG) printf("%sPreparazione richiesta elevation map\n", CLIENT);
	image_packet_buffer = (char*) calloc(BUFFERSIZE, sizeof(char));
	image_packet = (ImagePacket*) calloc(1, sizeof(ImagePacket));
	
  im_head.type = GetElevation;

  image_packet -> header = im_head;
  image_packet -> image = NULL;
  
  //SERIALIZE PART
  length = Packet_serialize(image_packet_buffer, &image_packet->header);
  printf("Bytes scritti nel buffer: %d\n", length);
	//Packet_free(&image_packet->header);
	
	//INVIO BUFFER
	ret = send(socket_tcp, image_packet_buffer,length, 0);
	//free(image_packet_buffer);
	
	//RICEVO BUFFER
	image_packet_buffer = (char*)calloc(BUFFERSIZE, sizeof(ImagePacket*));
	ret = recv(socket_tcp, image_packet_buffer, BUFFERSIZE, 0);
	
	//DESERIALIZE PART
	image_packet = (ImagePacket*) Packet_deserialize(image_packet_buffer, ret);
	*map_elevation = image_packet -> image;
	if(DEBUG) printf("%selevation: %p\n",CLIENT, *map_elevation);
	
	sleep(1);
	
	/**SURFACE MAP **/
	if(DEBUG) printf("%sPreparazione richiesta surface map\n", CLIENT);
	image_packet_buffer = (char *) calloc(BUFFERSIZE, sizeof(char));
	image_packet = (ImagePacket*) calloc(1, sizeof(ImagePacket));
	
	im_head.type = GetTexture;
	
	image_packet -> header = im_head;
	image_packet -> id = 0;
	image_packet -> image = NULL;
	
	//SERIALIZE PART
	length = Packet_serialize(image_packet_buffer, &image_packet->header);
	printf("Bytes scritti nel buffer: %d\n", length);
	//Packet_free(&image_packet -> header);
	
	//INVIO BUFFER
	ret = send(socket_tcp, image_packet_buffer, length, 0);
	
	//RICEVO BUFFER
	ret = recv(socket_tcp, image_packet_buffer, BUFFERSIZE, 0);
	
	//DESERIALIZE PART
	image_packet = (ImagePacket*) Packet_deserialize(image_packet_buffer, ret);
	*map_texture = image_packet -> image;
}


int main(int argc, char **argv) {
  if (argc<3) {
    printf("usage: %s <server_address> <player texture>\n", argv[1]);
    exit(-1);
  }
	
  Image* my_texture = Image_load(argv[2]);
  
  int my_id, ret;
  char* server_address;
  
  Image* map_elevation;
  Image* map_texture;
  
  //CONNECTION PART
  struct sockaddr_in addr = {0};
	
	server_address = argv[1];
  socket_tcp = socket(AF_INET, SOCK_STREAM, 0);
	ERROR_HELPER(socket_tcp, "Errore creazione socket");

  addr.sin_family 			= AF_INET;
  addr.sin_port 				= htons(SERVER_PORT);
  addr.sin_addr.s_addr 	= inet_addr(server_address);
	int count = 0;
  while(count < 5){
		ret = connect(socket_tcp, (struct sockaddr*) &addr, sizeof(struct sockaddr_in));
		if(ret > -1) break;
		count++;
		sleep(1);
	}
	if(count > 4) ERROR_HELPER(ret, "Errore nella connect\n");
	count ^= count;
	
  //CONNESSIONE EFFETTUATA
	struct timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec= 0;
	ret = setsockopt(socket_tcp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval));
  getter_TCP(my_texture, &map_elevation, &map_texture, &my_id);
	
  // COSTRUZIONE MONDO
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle= (Vehicle*) malloc(sizeof(Vehicle));
  Vehicle_init(vehicle, &world, my_id, my_texture);
  World_addVehicle(&world, vehicle);
  
  pthread_t udp_thread;
  int socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
  
  ret = setsockopt(socket_udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval));
	ERROR_HELPER(ret, "Errore socketOpt");
	
  struct args* arg = (struct args*) calloc(1, sizeof(struct args));
  arg -> idx = my_id;
	arg -> tcp_sock = socket_tcp;
	arg -> udp_sock = socket_udp;
	
	/**INIZIALIZZO SEMAFORO**/
	world_sem = (sem_t*) calloc(1, sizeof(sem_t));
	sem_init(world_sem, 0, 1);
	
	request = (sem_t*) calloc(1, sizeof(sem_t));
	sem_init(request, 0, 1);
	
	if(DEBUG)printf("%sCreo thread per ricezione udp\n", CLIENT);
	ret = pthread_create(&udp_thread, NULL, client_udp_routine, arg);
  
  PTHREAD_ERROR_HELPER(ret, "Errore nella creazioni del thread");
  ret = pthread_detach(udp_thread);
  
  
  WorldViewer_runGlobal(&world, vehicle, &argc, argv);
	World_destroy(&world);
  return 0;
  
}
