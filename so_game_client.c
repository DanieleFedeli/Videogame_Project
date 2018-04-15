#include <sys/socket.h>
#include <GL/glut.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <pthread.h>

#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "so_game_protocol.h"
#include "utils.h"
#include "handler.h"

typedef enum ViewType {Inside, Outside, Global} ViewType;

typedef struct WorldViewer{
  World* world;
  float zoom;
  float camera_z;
  int window_width, window_height;
  Vehicle* self;
  ViewType view_type;
} WorldViewer;

sem_t* world_sem;
int shouldUpdate = 1;
int window, socket_tcp, socket_udp;
WorldViewer viewer;
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
	ret = send(param.tcp_sock, SEND, msg_len, 0);
	ERROR_HELPER(ret, "Errore nella send");
	
	ret = recv(param.tcp_sock, RECEIVE, BUFFERSIZE, 0);
	ERROR_HELPER(ret, "Errore nella receive");
	if(DEBUG)printf("%sRicevuti %d bytes..\n", CLIENT, ret);
	
	if(DEBUG)printf("%sAggiungo user con id:%d\n", CLIENT, param.idx);
	image_packet = (ImagePacket*) Packet_deserialize(RECEIVE, ret);
	Vehicle_init(toAdd, &world, param.idx, image_packet->image);
	
	World_addVehicle(&world, toAdd);
	
	free(RECEIVE);
	free(SEND);
	pthread_exit(NULL);
}

int packet_handler_udp(char* PACKET, char* SEND){
	PacketHeader* h = (PacketHeader*)PACKET;
	struct args* param;
	int ret;
	if(h->type == WorldUpdate){
		WorldUpdatePacket* wup = (WorldUpdatePacket*) Packet_deserialize(PACKET, h->size);
		
		for(int i = 0; i < wup->num_vehicles; i++){
			Vehicle* v = World_getVehicle(&world, wup->updates[i].id);
			if(v == vehicle) continue;
			if(v == NULL){
				pthread_t get;
				param = (struct args*) calloc(1, sizeof(struct args));
				param->idx = wup->updates[i].id;
				param->tcp_sock = socket_tcp;
				ret = pthread_create(&get, NULL, getter, (void*) param);
				PTHREAD_ERROR_HELPER(ret, "Errore nella generazione thread");
				
				ret = pthread_join(get, NULL);
				PTHREAD_ERROR_HELPER(ret, "Errore nella detach");
				
			}
			else{
				v->theta 	= wup->updates[i].theta;
				v->x 			= wup->updates[i].x;
				v->y 			= wup->updates[i].y;
			}
			
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
	
	sleep(5);
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
		
		if(DEBUG)printf("%sPreparato pacchetto vup con:\nid: %d\nrotational_force: %f\ntranslational_force: %f\nx: %f\ny: %f\ntheta: %f\n",CLIENT, vup->id, vup->rotational_force, vup->translational_force, vup->x, vup->y, vup->theta);
		header.type = VehicleUpdate;
		vup -> header = header;
		
		
		length = Packet_serialize(SEND, &vup->header);
		if(DEBUG)printf("%sProvo ad inviare %d bytes...\n", CLIENT, length);
		ret = sendto(socket_udp, SEND, length, 0,(struct sockaddr*) &addr, length);
		ERROR_HELPER(ret, "Errore nella sendto");
		if(DEBUG)printf("%sInviati...\n", CLIENT);
		
		length = sizeof(struct sockaddr);
		if(DEBUG)printf("%sProvo a ricevere...\n", CLIENT);
		ret = recvfrom(socket_udp, RECEIVE, BUFFERSIZE, 0,(struct sockaddr*) &addr, (socklen_t*) &length);
		ERROR_HELPER(ret, "Errore nella recvfrom");
		if(DEBUG)printf("%sRicevuti %d bytes...\n", CLIENT, ret);
		if(ret == 0) continue;
		packet_handler_udp(RECEIVE, SEND);
		
		free(SEND);
		free(RECEIVE);
	}
	
	pthread_exit(NULL);
}

void getter_TCP(Image* my_texture, Image** map_elevation, Image** map_texture, int* my_id){
  char* id_buffer 					= (char*) calloc(BUFFERSIZE, sizeof(char)); //USATO PER TRANSAZIONI DI ID
  char* image_packet_buffer = (char*) calloc(BUFFERSIZE, sizeof(char)); //USATO PER TRANSAZIONE DI IMG
  int ret, length, bytes_sent, bytes_read;
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
	Packet_free(&id_packet->header);
	
	//INVIO BUFFER
	bytes_sent = 0;
  while(bytes_sent < length){
		ret = send(socket_tcp, id_buffer+bytes_sent, length - bytes_sent, 0);
		if(ret == -1 && errno == EINTR) continue;
		if(ret == 0) break;
		bytes_sent += ret;
	}

	//RICEVO BUFFER
	bytes_read = 0;
  while(bytes_read < length){
		ret = recv(socket_tcp, id_buffer + bytes_read, length - bytes_read, 0);
		//if(DEBUG) printf("%sRET ORA: %d\n", CLIENT, ret);
		if(ret == -1 && errno == EINTR) continue;

		if(ret <= 0) break;

		bytes_read += ret;
		ERROR_HELPER(ret, "Errore nella recv");
	}
	
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
	free(image_packet_buffer);
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
	Packet_free(&image_packet->header);
	
	//INVIO BUFFER
	ret = send(socket_tcp, image_packet_buffer,length, 0);
	free(image_packet_buffer);
	
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
	Packet_free(&image_packet -> header);
	
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

  getter_TCP(my_texture, &map_elevation, &map_texture, &my_id);
	
  // COSTRUZIONE MONDO
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle= (Vehicle*) malloc(sizeof(Vehicle));
  Vehicle_init(vehicle, &world, my_id, my_texture);
  World_addVehicle(&world, vehicle);
  
  pthread_t udp_thread;
  int socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
  
  struct args* arg = (struct args*) calloc(1, sizeof(struct args));
  arg -> idx = my_id;
	arg -> surface_texture = map_texture;
	arg -> elevation_texture = map_elevation;
	arg -> tcp_sock = socket_tcp;
	arg -> udp_sock = socket_udp;
	
	/**INIZIALIZZO SEMAFORO**/
	world_sem = (sem_t*) calloc(1, sizeof(sem_t*));
	sem_init(world_sem, 0, 1);
	
	ret = pthread_create(&udp_thread, NULL, client_udp_routine, arg);
  
  PTHREAD_ERROR_HELPER(ret, "Errore nella creazioni del thread");
  ret = pthread_detach(udp_thread);
  
  
  WorldViewer_runGlobal(&world, vehicle, &argc, argv);

	free(arg);
	close(socket_udp);
	close(socket_tcp);
  World_destroy(&world);
  return 0;
  
}
