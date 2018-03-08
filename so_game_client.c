#include <sys/socket.h>
#include <GL/glut.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "so_game_protocol.h"
#include "utils.h"

int window, socket_tcp, socket_udp;
//WorldViewer viewer;
//World world;
//Vehicle* vehicle; // The vehicle

int getter_TCP(Image* my_texture, Image** map_elevation, Image** map_texture, int* my_id){
  //   -get an id
  //   -send your texture to the server (so that all can see you)
  //   -get an elevation map
  //   -get the texture of the surface
  //ID PART
  char id_buffer[BUFFERSIZE], image_packet_buffer[BUFFERSIZE]; 
  int ret, length, bytes_sent, bytes_read;
	if(DEBUG) printf("%sPreparo richiesta id\n", CLIENT);
  PacketHeader header;
  IdPacket* id_packet = (IdPacket*) malloc(sizeof(IdPacket));

  header.type   = GetId;
  id_packet -> header = header;
  id_packet -> id     = -1;

  if(DEBUG) printf("%sRichiesta ID in corso nel socket: %d\n", CLIENT, socket_tcp);
  length = Packet_serialize(id_buffer, &id_packet->header);

	bytes_sent = 0;
  while(bytes_sent < length){
		ret = send(socket_tcp, id_buffer+bytes_sent, length - bytes_sent, 0);
		if(ret == -1 && errno == EINTR) continue;
		if(ret == 0) break;	
		bytes_sent += ret;
		if(DEBUG) printf("%sBytes inviati: %d\n", CLIENT, bytes_sent);
	}
  
  if(DEBUG) printf("%sRichiesta ID inviata con %d byte(s). In attesa...\n", CLIENT, bytes_sent);
  
	bytes_read = 0;
  while(bytes_read < length){
		ret = recv(socket_tcp, id_buffer + bytes_read, length - bytes_read, 0);
		if(DEBUG) printf("%sRET ORA: %d\n", CLIENT, ret);
		if(ret == -1 && errno == EINTR) continue;
		
		if(ret <= 0) break;
		
		bytes_read += ret;
		ERROR_HELPER(ret, "Errore nella recv");
	}
	
	if(DEBUG) printf("%sBytes letti: %d\n", CLIENT, bytes_read);
	
  id_packet = (IdPacket*) Packet_deserialize(id_buffer, length);
  *my_id = id_packet -> id;
  if(DEBUG) printf("%sId trovato: %d\n", CLIENT, *my_id);
	
	sleep(1);
	
	//POST TEXTURE PART
  ImagePacket* image_packet = (ImagePacket*)malloc(sizeof(ImagePacket));

  PacketHeader im_head;
  im_head.type = PostTexture;

  if(DEBUG) printf("%sCaricamento immagine\n", CLIENT);
  Image* im = my_texture;
  if(DEBUG) printf("%sCaricata\n", CLIENT);

  image_packet->header = im_head;
  image_packet->image = im;
	
	if(DEBUG) printf("%sImmagine salvata\n", CLIENT);
  //Image_save(image_packet->image, "in.pgm");
	
	printf("image_packet with:\ntype\t%d\nsize\t%d\n",
      image_packet->header.type,
      image_packet->header.size);
      
	if(DEBUG) printf("%sSerializzo immagine\n", CLIENT);
  int image_packet_buffer_size = Packet_serialize(image_packet_buffer, &image_packet->header);
  printf("Bytes scritti nel buffer: %d\n", image_packet_buffer_size);
	
	printf("deserialize\n");
  ImagePacket* deserialized_image_packet = (ImagePacket*)Packet_deserialize(image_packet_buffer, image_packet_buffer_size);

  printf("deserialized packet with:\ntype\t%d\nsize\t%d\n",
      deserialized_image_packet->header.type,
      deserialized_image_packet->header.size);

  if(DEBUG) printf("%sInvio immagine al server\n", CLIENT);
  bytes_sent = 0;
  while(bytes_sent < image_packet_buffer_size){
		ret = send(socket_tcp, image_packet_buffer + bytes_sent, image_packet_buffer_size - bytes_sent, 0);
		if(ret == -1 && errno == EINTR) continue;
		if(ret == 0) break;	
		bytes_sent += ret;
		if(DEBUG) printf("%sBytes inviati: %d\n", CLIENT, bytes_sent);
	}
	
	//ELEVATION MAP
	memset(image_packet, 0, sizeof(ImagePacket));
	return 0;
	
}	

int main(int argc, char **argv) {
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
  //Image* my_texture_for_server;
  // todo: connect to the server
  //   -get an id
  //   -send your texture to the server (so that all can see you)
  //   -get an elevation map
  //   -get the texture of the surface

  // these come from the server
  if(DEBUG) printf("%sQui inizia il mio codice\n", CLIENT);
  int my_id, ret;
  //Image* map_elevation;
  //Image* map_texture;
  struct sockaddr_in client_addr = {0};

  socket_tcp = socket(AF_INET, SOCK_STREAM, 0);
	ERROR_HELPER(socket_tcp, "Errore creazione socket");

  if(DEBUG) printf("%sSocket TCP creato\n", CLIENT);
  client_addr.sin_family 			= AF_INET;
  client_addr.sin_port 				= htons(SERVER_PORT);
  client_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  ret = connect(socket_tcp, (struct sockaddr*) &client_addr, sizeof(struct sockaddr_in));
	ERROR_HELPER(ret, "Errore nella connect\n");
  if(DEBUG) printf("%sConnect eseguita\n", CLIENT);
  //CONNESSIONE EFFETTUATA
  
  ret = getter_TCP(my_texture, NULL, NULL, &my_id);
	
	printf("%d My id\n", my_id);
  // construct the world
  //World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  //vehicle=(Vehicle*) malloc(sizeof(Vehicle));
  //Vehicle_init(&vehicle, &world, my_id, my_texture_from_server);
  //World_addVehicle(&world, v);

  // spawn a thread that will listen the update messages from
  // the server, and sends back the controls
  // the update for yourself are written in the desired_*_force
  // fields of the vehicle variable
  // when the server notifies a new player has joined the game
  // request the texture and add the player to the pool
  /*FILLME*/

  //WorldViewer_runGlobal(&world, vehicle, &argc, argv);

  // cleanup
  //World_destroy(&world);
  return 0;
}
