.SUFFIXES:

.SUFFIXES : .ftn .f .c .o

SHELL = /bin/sh

CPP = /lib/cpp

FFLAGS =

CFLAGS = -D_REENTRANT -D_THREAD_SAFE -g

OPTIMIZ = -O 2

#INCLUDE = /users/dor/armn/lib/trunk/primitives/

LDFLAGS = pthread

default: absolu

client_timeout.o: client_timeout.c mgi.h

SRCS= gossip_server.c
SRCC= gossip_client.c
OBJET= gossip_thread_server.o client_timeout.o


client_timeout.c:
	svn cat svn\://mrbsvn/pub/trunk/primitives/client_timeout.c > client_timeout.c

mgi.h:
	svn cat svn\://mrbsvn/pub/trunk/primitives/mgi.h > mgi.h

include $(ARMNLIB)/include/makefile_suffix_rules.inc

obj: $(OBJET)

absolu: $(OBJET)
	r.build -o gserver_$(ARCH) -obj $(OBJET) -src $(SRCS) -arch $(ARCH) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn rmnlib-dev -conly

	r.build -o gossip_client_$(ARCH) -src $(SRCC) -arch $(ARCH) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn rmnbeta -conly

server: $(OBJET)
	r.build -o gserver_$(ARCH) -obj $(OBJET) -src $(SRCS) -arch $(ARCH) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn rmnlib-dev -conly

client: $(OBJET)
	r.build -o gossip_client_$(ARCH) -src $(SRCC) -arch $(ARCH) -abi $(ABI) -libsys $(LDFLAGS) -includes $(INCLUDE) -librmn rmnlib-dev -conly

clean:
#Faire le grand menage. On enleve tous les fichiers inutiles, les absolus et les .o 

	rm *.o gserver_$(ARCH) gossip_client_$(ARCH)

clean_all:
	rm *.o gserver_$(ARCH) gossip_client_$(ARCH) client_timeout.c mgi.h
