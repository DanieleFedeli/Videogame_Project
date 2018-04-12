#include "linked_list.h"
#include "image.h"

int server_tcp_packet_handler(char* PACKET, char* SEND, void* arg);
int server_udp_packet_handler(char* PACKET, char* SEND, void* arg);

void * server_tcp_routine(void* arg);
void * server_udp_routine(void* arg);

void * client_udp_routine(void * arg);
int client_udp_packet_handler(char* PACKET, char* SEND, void* arg);

int set_global_communicate(void);
int set_global_update(void);

int reset_update(void);
int reset_communicate(void);

int get_global_update(void);
int get_global_communicate(void);

void notify_user(int idx, int socket_udp);

ListHead get_user_on(void);
ListItem* add_user(int idx);

void print_all_user(void);
int __init__(Image* t_surface, Image* t_elevation);

void quit_server(void);
void insert_texture(Image* toPut, int idx);

int destroy_resources(void);

void first_udp_message(int socket_udp, int idx, struct sockaddr_in server);
