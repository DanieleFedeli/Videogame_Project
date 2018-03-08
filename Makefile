CCOPTS= -Wall -g -std=gnu99 -Wstrict-prototypes
<<<<<<< HEAD
LIBS= -lglut -lGLU -lGL -lm -lpthread
=======
LIBS= -lglut -lGLU -lGL -lm -lpthread 
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724
CC=gcc
AR=ar


BINS=libso_game.a\
<<<<<<< HEAD
     so_game_client\
     so_game_server\
     test_packets_serialization 
=======
     so_game_server\
     so_game_client\
     test_packets_serialization\
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724

OBJS = vec3.o\
       linked_list.o\
       surface.o\
       image.o\
       vehicle.o\
       world.o\
       world_viewer.o\
       so_game_protocol.o\
       so_game_server.o\
<<<<<<< HEAD
       so_game_client.o

=======
       so_game_client.o\
	   
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724
HEADERS=helpers.h\
	image.h\
	linked_list.h\
	so_game_protocol.h\
	surface.h\
<<<<<<< HEAD
	utils.h\
=======
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724
	vec3.h\
	vehicle.h\
	world.h\
	world_viewer.h\
<<<<<<< HEAD
=======
	server_protocol.h\
	client_protocol.h\
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724

%.o:	%.c $(HEADERS)
	$(CC) $(CCOPTS) -c -o $@  $<

.phony: clean all

<<<<<<< HEAD

=======
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724
all:	$(BINS) 

libso_game.a: $(OBJS) 
	$(AR) -rcs $@ $^
	$(RM) $(OBJS)
<<<<<<< HEAD

so_game_server: so_game_server.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)

so_game_client: so_game_client.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)
=======
	
so_game_client: so_game_client.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)
	
so_game_server: so_game_server.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724

test_packets_serialization: test_packets_serialization.c libso_game.a  
	$(CC) $(CCOPTS) -Ofast -o $@ $^  $(LIBS)

clean:
<<<<<<< HEAD
	rm -rf *.o *~  $(BINS) *.gch
=======
	rm -rf *.o *~  $(BINS)
>>>>>>> 95e9c4673c828aa80f9f2ee8b6591352408ed724
