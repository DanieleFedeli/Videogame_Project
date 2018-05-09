CCOPTS= -Wall -O3 -g -std=gnu99 -Wstrict-prototypes
LIBS= -lglut -lGLU -lGL -lm -lpthread

CC=gcc
AR=ar


BINS=libso_game.a\
     so_game_client\
     so_game_server\


OBJS = vec3.o\
       linked_list.o\
       surface.o\
       image.o\
       handler.o\
       vehicle.o\
       world.o\
       world_viewer.o\
       so_game_protocol.o\
       so_game_server.o\
       so_game_client.o\
       

HEADERS=helpers.h\
	image.h\
	linked_list.h\
	so_game_protocol.h\
	surface.h\
	utils.h\
	vec3.h\
	vehicle.h\
	world.h\
	world_viewer.h\
	handler.h\



%.o:	%.c $(HEADERS)
	$(CC) $(CCOPTS) -c -o $@  $<

.phony: clean all


all:	$(BINS) 

libso_game.a: $(OBJS) 
	$(AR) -rcs $@ $^
	$(RM) $(OBJS)

so_game_client: so_game_client.c libso_game.a 
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)
	
so_game_server: so_game_server.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)

clean:
	rm -rf *.o *~  $(BINS) *.gch

clean-images:
	rm ./images/vehicle_*
