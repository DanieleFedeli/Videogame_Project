#include <sys/socket.h>
#include <GL/glut.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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


int window, socket_tcp, socket_udp;
WorldViewer viewer;
World world;
Vehicle* vehicle; // The vehicle


void getter_TCP(Image* my_texture, Image** map_elevation, Image** map_texture, int* my_id){
	//send your texture to the server (so that all can see you)
  //   -get an elevation map
  //   -get the texture of the surface
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
	free(image_packet_buffer);
	
	//RICEVO BUFFER
	ret = recv(socket_tcp, image_packet_buffer, BUFFERSIZE, 0);
	
	//DESERIALIZE PART
	image_packet = (ImagePacket*) Packet_deserialize(image_packet_buffer, ret);
	*map_texture = image_packet -> image;
}


int main(int argc, char **argv) {

  //Image* my_texture_from_server = my_texture;
  //todo: connect to the server
  //   -get an id
  //   -send your texture to the server (so that all can see you)
  //   -get an elevation map
  //   -get the texture of the surface

  // these come from the server
  if (argc<3) {
    printf("usage: %s <server_address> <player texture>\n", argv[1]);
    exit(-1);
  }
	
  printf("loading texture image from %s ... ", argv[2]);
  Image* my_texture = Image_load(argv[2]);
  if (my_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }
  
  if(DEBUG) printf("%sQui inizia il mio codice\n", CLIENT);
  int my_id, ret;
  char* server_address;
	//GENERA ERRORE PASSARE QUESTA ALLA FUNZIOEN GETTER
  
  Image* map_elevation;
  Image* map_texture;
  
  
  //CONNECTION PART
  struct sockaddr_in client_addr = {0};
	
	server_address = argv[1];
  socket_tcp = socket(AF_INET, SOCK_STREAM, 0);
	ERROR_HELPER(socket_tcp, "Errore creazione socket");

  if(DEBUG) printf("%sSocket TCP creato\n", CLIENT);
  client_addr.sin_family 			= AF_INET;
  client_addr.sin_port 				= htons(SERVER_PORT);
  client_addr.sin_addr.s_addr = inet_addr(server_address);

  ret = connect(socket_tcp, (struct sockaddr*) &client_addr, sizeof(struct sockaddr_in));
	ERROR_HELPER(ret, "Errore nella connect\n");
  if(DEBUG) printf("%sConnect eseguita\n", CLIENT);
  //CONNESSIONE EFFETTUATA

  getter_TCP(my_texture, &map_elevation, &map_texture, &my_id);
  
  //UTENTE EFFETTIVAMENTE CONNESSO
  
	
  // COSTRUZIONE MONDO
	if(DEBUG) printf("%smap_elevation: %p\n%smap_texture: %p\n%smy_texture_from_server: %p\n", CLIENT, map_elevation, CLIENT, map_texture,CLIENT, my_texture);
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle= (Vehicle*) malloc(sizeof(Vehicle));
  Vehicle_init(vehicle, &world, my_id, my_texture);
  World_addVehicle(&world, vehicle);
	 
  // spawn a thread that will listen the update messages from
  // the server, and sends back the controls
  // the update for yourself are written in the desired_*_force
  // fields of the vehicle variable
  // when the server notifies a new player has joined the game
  // request the texture and add the player to the pool
  /*FILLME*/
  /*
  int socket_udp;

  socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
  
  if(DEBUG) printf("%sParte UDP\n", CLIENT);
  pthread_t udp_thread;
  
  socket_udp = socket(AF_INET, SOCK_DGRAM, 0);
  
  struct args* arg = (struct args*) calloc(1, sizeof(struct args));
  arg -> idx = my_id;
	arg -> surface_texture = map_texture;
	//arg -> vehicle_texture = my_texture;
	arg -> elevation_texture = map_elevation;
	arg -> tcp_sock = socket_tcp;
	arg -> udp_sock = socket_udp;
	

  
  if(DEBUG) printf("%sCreazione server per ricezione UDP\n", CLIENT);
  ret = pthread_create(&udp_thread, NULL, client_udp_routine, NULL);
  PTHREAD_ERROR_HELPER(ret, "Errore nella creazioni del thread");
  
  ret = pthread_detach(udp_thread);
  
  */
	if(DEBUG) printf("%sWorld run\n", CLIENT);
  WorldViewer_runGlobal(&world, vehicle, &argc, argv);
	
  // cleanup
  World_destroy(&world);
  return 0;
  
}
