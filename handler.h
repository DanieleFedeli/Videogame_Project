#include "linked_list.h"
#include "image.h"
#include "utils.h"
#include "vehicle.h"

int server_tcp_packet_handler(char* PACKET, char* SEND,Vehicle* v, int id, Image** texture,const struct args* arg);
int server_udp_packet_handler(char* PACKET, const struct args* arg);

void * server_tcp_routine(void* arg);
void * server_udp_routine(void* arg);

int set_global_communicate(void);
int set_global_update(void);

int reset_update(void);
int reset_communicate(void);

int get_global_update(void);
int get_global_communicate(void);

void print_all_user(void);
void __init__(Image* t_surface, Image* t_elevation);

void quit_server(void);
void insert_texture(Image* toPut, int idx);
