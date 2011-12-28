.SUFFIXES:

.SUFFIXES : .ftn .f .c .o

SHELL = /bin/sh

CPP = /lib/cpp

FFLAGS =

CFLAGS = -D_REENTRANT -D_THREAD_SAFE -g

OPTIMIZ = -O 2

LDFLAGS = pthread

LIBRMN = rmnbeta_011


default: absolu

gossip_thread_server.o: mgi.h
SRCS = gossip_server.c
SRCC = gossip_client.c
OBJET = gossip_thread_server.o


mgi.h:
	svn cat svn\://mrbsvn/pub/trunk/primitives/mgi.h > mgi.h

include $(ARMNLIB)/include/makefile_suffix_rules.inc

obj: $(OBJET)

absolu: $(OBJET)
	r.build -o gserver_$(BASE_ARCH) -obj $(OBJET) -src $(SRCS) -arch $(EC_ARCH) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn $(LIBRMN) -conly

	r.build -o gossip_client_$(BASE_ARCH) -src $(SRCC) -arch $(EC_ARCH) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn $(LIBRMN) -conly

server: $(OBJET)
	r.build -o gserver_$(BASE_ARCH) -obj $(OBJET) -src $(SRCS) -arch $(EC_ARCH) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn $(LIBRMN) -conly

client: $(OBJET)
	r.build -o gossip_client_$(BASE_ARCH) -src $(SRCC) -arch $(EC_ARCH) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn $(LIBRMN) -conly

clean:
#Faire le grand menage. On enleve tous les fichiers inutiles, les .o 

	rm *.o

clean_all:
#Faire le grand menage. On enleve tous les fichiers inutiles, les absolus et les .o
	rm *.o gserver_$(BASE_ARCH) gossip_client_$(BASE_ARCH) mgi.h mgi.LOG
