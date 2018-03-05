#pragma once

#include <netinet/in.h>
#include "image.h"

typedef struct ListItem {
  struct ListItem* prev;
  struct ListItem* next;
  int 	idx;					//L'idx è anche il fd della socket tcp
  float rotational_force;		//Read from vehicleUpdatePacket
  float translational_force;	//Read from VehicleUpdatePacket
  float x;						//Read from client
  float y;						//Read from client
  float theta;					//Read from client
  Image * vehicle_texture;		//Read from ImagePacket
  struct sockaddr_in addr;		//Struttura sockaddr_in che consente l'aggiornamento via UDP
} ListItem;

typedef struct ListHead {
  ListItem* first;
  ListItem* last;
  int size;
} ListHead;

void List_init(ListHead* head);
ListItem* List_find	  (ListHead* head, ListItem* item);
ListItem* List_find_id(ListHead* head, int idx);
ListItem* List_insert (ListHead* head, ListItem* previous, ListItem* item);
ListItem* List_detach (ListHead* head, ListItem* item);
