.SUFFIXES:

.SUFFIXES : .ftn .f .c .o

SHELL = /bin/sh

CPP = /lib/cpp

FFLAGS =

CFLAGS = -D_REENTRANT -D_THREAD_SAFE -g

OPTIMIZ = -O 2

LDFLAGS = pthread

LIBRMN = rmn_015.1

VER = 2.3

default: absolu

gossip_thread_server.o: mgi.h
SRCS = gossip_server.c
SRCC = gossip_client.c
OBJET = gossip_thread_server.o


#mgi.h:
#	svn cat svn\://mrbsvn/pub/trunk/primitives/mgi.h > mgi.h

include $(RPN_TEMPLATE_LIBS)/include/makefile_suffix_rules.inc

obj: $(OBJET)

absolu: $(OBJET)
	s.compile -o gserver_$(VER)-$(BASE_ARCH) -obj $(OBJET) -src $(SRCS) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn $(LIBRMN) -conly

	s.compile -o gclient_$(VER)-$(BASE_ARCH) -src $(SRCC) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn $(LIBRMN) -conly

server: $(OBJET)
	s.compile -o gserver_$(BASE_ARCH) -obj $(OBJET) -src $(SRCS) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn $(LIBRMN) -conly

client: $(OBJET)
	s.compile -o gclient_$(BASE_ARCH) -src $(SRCC) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn $(LIBRMN) -conly

clean:
#Faire le grand menage. On enleve tous les fichiers inutiles, les .o 

	rm *.o gserver_$(VER)-${BASE_ARCH} gclient_$(VER)-${BASE_ARCH}

clean_all:
#Faire le grand menage. On enleve tous les fichiers inutiles, les absolus et les .o
	rm *.o gserver_$(VER)-${BASE_ARCH} gclient_$(VER)-${BASE_ARCH} mgi.h mgi.LOG
