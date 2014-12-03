/* RMNLIB - Library of useful routines for C and FORTRAN programming
 * Copyright (C) 1975-2000  Division de Recherche en Prevision Numerique
 *                          Environnement Canada
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <rpnmacros.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#include <pthread.h>

#include <sys/param.h>
#include <gossip.h>

#include <strings.h> 
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>

#include <dirent.h>

#include <fcntl.h>
#include <sys/param.h>


#define NCARMAX        256

#define READ             1

#define IS_OK            0
#define NOT_OK           1

static int ping_pid = 0;
static int exit_flag = 0;

/* define DEBUG */
int get_client_count();
void reset_timeout_counter();
int get_ping_interval();
extern int *load_channels_data(int option);
extern int store_all_channels_data();
char *get_server_host(char *host_ip);
char *get_server_name(char *host_ip);
extern int get_status_all(int fserver);
extern int get_active_channels();

extern void cancel_read(char *channel);

/*******************************/
static int endian_flag = 1;
static char *little_endian = (char *)&endian_flag;

static int swap_element(int nbr, int tokensize)
{
  if(*little_endian)
    {
      if(tokensize == FOUR_BYTES)
        swap_4(nbr);
      
      if(tokensize == EIGHT_BYTES)
        swap_8(nbr);
      
    }
  
  return nbr;
}

static void user_server(EXTENDED_CLIENT_SLOT *client)
{
  int buflen;
  int fclient = client->socket;
  
  char buf[128];
  char *oob;

  /* receive_oob(fclient); */

  while( (buflen = read(fclient, buf, sizeof buf)) > 0 )
    { 
      /* get next command, after succefull login */
      
      buf[buflen > 0 ? buflen : 0] = '\0';
      fprintf(stderr, "Server: command \"%s\" received\n", buf);

      /* execute command */
      if( strncmp(buf, "EXEC", 4 ) == 0  || strncmp(buf, "FORK", 4) == 0 )
	{
	  /* int client_no = get_client_count(); */
	  
	  client->command = buf;

	  client->user_function(client);

	  reset_timeout_counter();             /* reset TIMEOUT counter */
	 
	  continue;
	  
	}
      else if(strncmp(buf, "CKPT", 4) == 0)    /* set checkpoint flag */
	{
	  if(get_active_channels() > 0)
	    {
	      send_ack_nack(fclient, NOT_OK);
	      close(fclient);
	      continue;
	    }
	  
	  exit_flag = 1;
	  send_ack_nack(fclient, IS_OK);
	  close(fclient);
	  reset_timeout_counter();            /* reset TIMEOUT counter */
	  exit_from_client_thread(client);
	  
	}
      
      else if(strncmp(buf, "NOOP", 4) == 0)    /* null operation, reset client counter */
	{
	  send_ack_nack(fclient, IS_OK);
	  close(fclient);
	  reset_timeout_counter();            /* reset TIMEOUT counter */
	  exit_from_client_thread(client);
	}

      else if(strncmp(buf, "STATUS", 6) == 0)   /* get active channels */
	{
	  fprintf(stderr, "STATUS Command\n");
	  send_ack_nack(fclient, IS_OK);
	  get_status_all(fclient);
	  close(fclient);
	  reset_timeout_counter();            /* reset TIMEOUT counter */
	  exit_from_client_thread(client);
	}
      else if(strncmp(buf, "ABORT", 5) == 0)   /* emergency, kill server */
	{
	  fprintf(stderr, "Server process will be stopped !!!\n");
	  send_ack_nack(fclient, IS_OK);
	  close(fclient);
	  kill(ping_pid, 9);
	  exit(1);
	}

      /********* out-of-band data exchange */
      else if(strncmp(buf, "WRITE_OOB", 9) == 0)   /* write oob message */
	{
	  /* write to buffer */
	  int nbytes = 0;
	  send_ack_nack(fclient, IS_OK);
	  oob = (char *)malloc(6);
	  nbytes = read(fclient, oob, 6);

	  oob += nbytes;
	  *oob = '\0';
	  oob -= nbytes;
	  if(oob)
	    fprintf(stderr, "WRITE_OOB Command, message \"%s\" length = %d \n", oob, nbytes);

	  fprintf(stderr, "WRITE_OOB Command, strcmp(oob, \"ABCDEF\") = %d \n", strcmp(oob, "ABCDEF"));

	  close(fclient);
	}
      else if(strncmp(buf, "READ_OOB", 8) == 0)   /* read oob message */
	{
	  /* read from buffer*/
	  send_ack_nack(fclient, IS_OK);
	  
	  oob = (char *)malloc(10);
	  oob = "GHIJKL";

	  if(oob && strlen(oob) > 0)
	    {

	      if(write(fclient, oob, strlen(oob)) <= 0) 
		{
		  fprintf(stderr, "READ_OOB: Can't write oob\n");
		}
	      fprintf(stderr, "READ_OOB:  send oob = %s\n", oob);
	    }
	  else
	    fprintf(stderr, "READ_OOB Command, oob null \n");
	  close(fclient);
	}
      /********** out-of-band data exchange */
      else if( strncmp( buf, "END", 3 ) == 0 ) 
	{
	  char *channel;
	  /* int client_no; */
	  
	  channel = (char *)malloc( 1024 );

	  send_ack_nack( fclient, IS_OK );      /* send ACK to indicate that command is accepted */
	  
	  /* client_no = get_client_count(); */
	  
	  sscanf( buf, "END %s", channel );
	  
	  cancel_read( channel );
	  
	  reset_timeout_counter();            /* reset TIMEOUT counter */
	  
	  close( fclient );

	  if( channel )
	    free( channel );

	}
      else
	{ /* assumed to be a bum call */
	  printf("Bad Command: \"%s\"\n", buf);
	  send_ack_nack(fclient, NOT_OK);    /* send NACK to indicate that command is rejected */
	  reset_timeout_counter();           /* reset TIMEOUT counter */
	  continue;                          /* connection terminated, next client please */
	}
      
    }

}


void gossip_thread_server(char *LOGFILE, void (*event_loop)(EXTENDED_CLIENT_SLOT *), int TIMEOUT, void *data)
{
  /* the first child is the actual server */
  
  int fclient;
  char *buf;
  int buflen;

  char *Auth_token = get_broker_Authorization();
  /* int chgbuflen = FALSE; */
  unsigned int Bauth_token = 0;

  int fserver;
  int server_port;
  int ipaddr;
  int b0, b1, b2, b3;
  int myuid = getuid();
  int client_uid, client_pid, client_auth;
  int PING_INTERVAL;
  
  unsigned char buffer[16];
  
  fd_set rfds;
  struct timeval tv = {0};


  /*   the second child will act as a watchdog and PINGs its parent periodically */
  /*   it will exit when parent has exited and therefore no longer accepts connections */
  
  /* miscellaneous initializations + get Authorization token */
  
  set_timeout_counter( TIMEOUT/get_ping_interval() ); /* set ping number */
  PING_INTERVAL = get_ping_interval();                /* get ping interval */
  
  if(Auth_token == NULL) 
    {
      fprintf(stderr, "Authorization token failure \n");
      exit(1);
    }

  sscanf( Auth_token, "%u", &Bauth_token );
  
  /*   get a socket */
  fserver = get_sock_net();
   
  /*   set buffer sizes for socket */
  set_sock_opt( fserver );
  
  /*   bind to a free port, get port number */
  server_port = bind_sock_to_port( fserver );
  
  /*   parent writes host:port and exits after disconnecting stdin, stdout, and stderr */
  ipaddr = get_own_ip_address();  /* get own IP address as 32 bit integer */

  b0 = ipaddr >> 24; /* split IP address */
  b1 = ipaddr >> 16;
  b2 = ipaddr >> 8;
  b3 = ipaddr;
  b0 &= 255;
  b1 &= 255;
  b2 &= 255;
  b3 &= 255;

  buf = (char *)malloc(1024);
  /* host_name = (char *)malloc(128); */

  if ( !buf )
    {
      fprintf(stderr, "channel buffer is NULL !!!, exiting\n");
      exit(1);
    }
 
  snprintf(buf, 1023, "%d.%d.%d.%d:%d", b0, b1, b2, b3, server_port);
  
  /* fprintf(stderr, "Launching Server at host : \"%s\" \n",  buf); */
  
  set_host_and_port(LOGFILE, buf);  /* write to channel info file */
                                    /* write as A.B.C.D:port */
    
  fprintf(stderr, "Launching Server at host: \"%s\" using port: \"%d\"\n", get_server_name(buf), server_port );
  
  sprintf(buf, "%s.LOG", LOGFILE);
  
  fclose(stdout); /* close and reopen STDOUT  */
  fclose(stderr); /* close and reopen STDERR */
  
  freopen(buf, "w", stdout);  
  freopen(buf, "w", stderr);
  
  if( fork() > 0 ) 
    {
      fprintf(stderr, "Exiting from parent process !!!\n");
      exit(0);  /* parent exits now */
    }
  
  setpgrp(); /* get into another process group */
  
  /* set NO BUFFERING for STDOUT and STDERR */
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  client_auth = 0;
  client_pid = 0;
  client_uid = 0;
  
    
  listen(fserver, 32767);

  /* #ifdef NEVERTRUE */

  /* pinging part */
  if( (ping_pid = fork()) == 0 )
    {
      int watchdog_client;
      int ping_ord = 0;
      char *pingbuf;

      pingbuf = (char *)malloc(128);
      close(fserver);
            
      sleep(PING_INTERVAL);
      
      while(watchdog_client = connect_to_localport(server_port))
	{
	  /* send PING command with ping counter */

	  sprintf(pingbuf, "PING %d", ping_ord++);
	
	  if(watchdog_client > 0)
	    {
	      write(watchdog_client, pingbuf, strlen(pingbuf));
	      close(watchdog_client);
	    }
	  else
	    {
	      fprintf(stderr, "End of Pinging Time, Server not responding !!!\n");
	      exit(0);
	    }
	  sleep(PING_INTERVAL);
	  
	}
      

      if(pingbuf != NULL)
	free(pingbuf);

      exit(0);
    }
  else if (ping_pid < 0)
    {
      fprintf(stderr, "Error: cannot fork server process, pid = %d\n", ping_pid);
    }
  /* #endif */


  /* fd_set rfds; */
  /* struct timeval tv; */

  tv.tv_sec = 5;
  tv.tv_usec = 0;
 
  /* back to first child, loop on connection requests */
  while( fclient = accept_from_sock(fserver) )
    {
      /* timeout */

      FD_ZERO(&rfds);
      FD_SET(fclient, &rfds);

      if (select(fclient+1, &rfds, NULL, NULL, &tv))
	{
	  buflen = read(fclient, buf, NCARMAX); /* get LOGIN command */
	  
	}
      else
	{
	  exit(1);
	}
     
      buf[buflen > 0 ? buflen : 0] = '\0';
      
      if(get_active_channels() <= 0 && exit_flag)
	{
	  fprintf(stderr, "Exiting from server, active channels: %d\n", get_active_channels());
	  store_all_channels_data();
	  close(fclient);
	  fprintf(stderr, "Server EXITING !\n");
	  
	  if(ping_pid > 0)
	    { 
	      kill(ping_pid, 9);
	    }
	  
	  exit(1);
	}

      if(strncmp(buf, "PING", 4) == 0) /* PING ? */
	{
	  /* echo to logfile */
	  fprintf(stderr, "Server: PING %d (%d)\n",                                                                              TIMEOUT/get_ping_interval() - get_timeout_counter(),                                                  get_timeout_counter());
	  close(fclient);
	  if(get_timeout_counter() <= 0 || ( ( get_active_channels() <= 0) && exit_flag ) )
	    {
	      /* Before exiting save any pending data buffer(s) 
		 to corresponding subchannel file(s) */
	      store_all_channels_data();

	      /**********NEW*********/
	      /* if(get_last_channel()) */
	      /**********NEW*********/
	      fprintf(stderr, "TIMEOUT detected, EXITING \n");
	      
	      if(ping_pid > 0)
		{ 
		  kill(ping_pid, 9);
		}

	      exit(1);
	    }

	  decrement_timeout_counter();
	  continue;    /* go service next connection request */
	}
      
      /* validate login command, check client uid against server uid,
       verify that authorization token is the right one,
       register client pid as connected if connection accepted */
      



      if(strncmp(buf, "LOGIN", 5) == 0) 
	{
	  client_uid = -1 ;
	  client_pid = -1 ;
	  

	  /* sscanf(buf, "LOGIN %d %d %u", &client_uid, &client_pid, &client_auth); */
	  sscanf(buf, "LOGIN %d %d %u", &client_uid, &client_pid, &client_auth);

	  
	  /* reject connection if not my uid 
	     or bad authorization token or not
	     a LOGIN command */

	  if( client_uid != myuid || md5_ssh( buffer ) )
	    { 
	      printf("Client Authentication FAILED\n");
	      continue;
	    }


	  else /*  LOGIN accepted, process next command  */
	    {
	      send_ack_nack(fclient, IS_OK); /* Positive ACK, LOGIN  accepted  */
	      /* printf("SSH Digest: %x\n", buffer); */  
    
	      /*  start a new thread to handle user request  */
	      start_client_thread_2((void *)&user_server, client_uid, client_pid, fclient, buf, data , (void *)event_loop);

	      continue;
	    }
	}

      else
	{
	  fprintf(stderr, "LOGIN command expected: %s\n", buf);
	  send_ack_nack(fclient, NOT_OK);  /*  Negative ACK, LOGIN rejected  */
	}
      
    } /* end while accept is successful */

  printf("Accept from socket failed, exiting !!!, fserver = %d\n", fserver);

  if(ping_pid > 0)
    {
      kill(ping_pid, 9);
    }

  if(buf)
    free(buf);
  
  exit(1);
}
