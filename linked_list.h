#pragma once
#include <netinet/in.h>

typedef struct ListItem {
  struct ListItem* prev;
  struct ListItem* next;
  struct sockaddr addr;				//Struttura sockaddr_in che consente l'aggiornamento via UDP
}ListItem;

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
