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

#include <stdlib.h>

#include <pthread.h>
#include <memory.h>

#include <mgi.h>
#include <gossip.h>

#include <fcntl.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <regex.h>

#include <sys/resource.h>

static void event_loop(EXTENDED_CLIENT_SLOT *client);

#define NCARMAX        256
#define BPATH         1024

#define NCHANNELS_MAX   24
#define BUFLENGTH     8192
#define IS_OK            0
#define NOT_OK           1
#define LOAD            -2

#define FAILURE         -3

#define READ             1
#define SCAN             2
#define WRITE            3
/* #define DEBUG */

typedef struct 
{
  char subchannel_name[NCARMAX];
  int fs_read;
  int fs_write;
  int io_index;
  int status;
} gossip_channel;

static int nbActiveChannels = 0;
static char last_channel[128];

static gossip_channel chan[NCHANNELS_MAX];

static int nb_glbsockets = 0;
static int chgmax        = FALSE;
static int maxlength;
static int maxsize       = 0;

static char *node_buffer;
static char *read_buffer;

static int readers_counter[NCHANNELS_MAX];
static int writers_counter[NCHANNELS_MAX];
static int blocked_readers[NCHANNELS_MAX];
static int blocked_writers[NCHANNELS_MAX];
static int occupied_counter[NCHANNELS_MAX];

static pthread_mutex_t mutr = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condr = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutw = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condw = PTHREAD_COND_INITIALIZER;


static char *liste[NCHANNELS_MAX];
static char lcl_liste[NCHANNELS_MAX][NCARMAX]; 
static char *def[NCHANNELS_MAX]; 
static char lcl_def[NCHANNELS_MAX][NCARMAX];
static char val[NCHANNELS_MAX][NCARMAX];
static char server_name[NCARMAX];
static int nb_channels = 0;

extern void c_ccard(char **argv, int argc, char **cle, char val[][NCARMAX],
		    char **def, int n, int *npos);


int write_record(int fclient, void *record, int size, int tokensize);
void *read_record(int fclient, void *records, int *length, int maxlength, int tokensize);
void send_ack_nack(int fclient, int status);
int get_ack_nack(int fserver);
int read_stream(int fd, char *ptr, int nbytes);
char *get_server_host(char *host_ip);
char *get_gossip_dir(int display);

/*********************************************************************/
int check_server(char *channel);
int store_all_channels_data();
void load_channels_data(int channels_nbr, int option);
int get_max_length();

int allocate_buffers( int chan_number );
int read_data_file( char *file_name, char *buffer, int size );
int get_file_size( char *file_name );
int store_channel_data( char *buffer, int nbytes, char *file_name );
int get_status_all( int fserver );
int get_active_channels();
int get_active_write_channels();
void cancel_read( char *channel );
char *get_channel_name( char *name );


char *get_last_channel();
/* char *readlink_malloc (const char *filename); */

static int MAX_BUFFER = 200000000;
int TOTAL_SIZE;
#define NODE_SIZE       1000000

#define SUCCESS         1

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct node
{
  char *rd;
  char *wr;
  int fs_read;
  int fs_write;
  char *data;
  struct node *next;
} node;

struct node *headptr[NCHANNELS_MAX], *nodeptr[NCHANNELS_MAX], *writeptr[NCHANNELS_MAX], *readptr[NCHANNELS_MAX];

int node_counter[NCHANNELS_MAX];

struct node * initialize( void );
void write_to_node(char *buffer, int cch, int size );
int insert_node( int cch );
void check_next_node( int cch );
void find_write_node( int cch );
int check_left_space( );
char * read_from_node( int cch, int length );
void freenodes( struct node *headptr, int cch );
void write_data( int cch );
void list_nodes( int cch );


/* main gossip Server function */

void main (int argc, char **argv)
{
  int i, j, npos;

  for (i = 0; i < NCHANNELS_MAX; i++)
    {
      for (j = 0; j < NCARMAX; j++)
	{
	  chan[i].subchannel_name[j] = '\0';
	}
      chan[i].fs_read = -1;
      chan[i].fs_write = -1;
      chan[i].io_index = -1;

      readers_counter[i] = 0;
      writers_counter[i] = 0;
      blocked_readers[i] = 0;
      blocked_writers[i] = 0;
      occupied_counter[i] = 0;

      headptr[i] = NULL;
      nodeptr[i] = NULL;
      writeptr[i] = NULL;
      readptr[i] = NULL;
      
    }
  
  strcpy(lcl_liste[0], "server.");
  liste[0] = lcl_liste[0];
  strcpy(lcl_def[0], "mgi");
  strcpy(val[0], "mgi");
  def[0] = (char *) lcl_def[0];

  strcpy(lcl_liste[1], "timeout.");
  liste[1] = lcl_liste[1];
  strcpy(lcl_def[1], "180");
  strcpy(val[1], "180");
  def[1] = (char *) lcl_def[1];



  npos = 0;
  c_ccard(argv, argc, (char **) liste, val, (char **) def, 2, &npos);
  
  strcpy(server_name, val[0]);

 
  nb_channels = atoi(val[3]);

  if(nb_channels > NCHANNELS_MAX)
  {
    printf("Error: The number of requested channels (%d) is greater than MAX allowed (%d)\n",                              nb_channels, NCHANNELS_MAX);
    exit( FAILURE );
  }

  
  /* load data files if there is any */
  /* load_channels_data(nb_channels, READ); */
  
  /* Check running server before lauching a new one */
  if(!check_server(server_name)) 
    {
      fprintf(stderr, "Server: exiting from current process\n");
      exit( FAILURE );
    }

  
  /** Launch server **/
  gossip_thread_server(server_name, (void *)&event_loop, atoi(val[1]), NULL);
}

/* check if there is a Server running      */
/* using IP and port saved in channel file */
int check_server(char *channel)
{
  int fserver = 0;
  char *buf;
  int status;
  
  fprintf(stderr, "Checking for running server on channel \"%s\"\n", channel);
  
  fserver = connect_to_channel_by_name(channel); 
  
  if(fserver > 0)
    {
      buf = (char *)malloc(128);
      if(get_server_host(channel) != NULL)
	{
	  fprintf(stderr, "******************************************************\n");
	  fprintf(stderr, "* There is a server running on \"%s\" *\n", get_server_host(channel));
	  fprintf(stderr, "******************************************************\n");
	}
      sprintf(buf, "NOOP");
      /* send command to stop running server */
      if(status = send_command_to_server(fserver, buf) != 0) 
	{
	  fprintf(stderr, "Unable to send command: \"%s\" to running server \n", buf);
	  close(fserver);
	  if(buf)
	    free(buf);
	  return status;
	}
      fprintf(stderr, "Command: \"%s\" has been sent to running server\n", buf);
      close(fserver);

      if(buf)
	free(buf);
      return status;
      
    }
  else
    {
      fprintf(stderr, "No gossip Server running on channel \"%s\"!!\n", channel);
      return status = -1;
    }
  
}

/* allocate fisrt node buffers */

int allocate_buffers( int cch )
{
  headptr[cch] = initialize();
  node_counter[cch] = 1;

  if ( !headptr[cch]->data )
    {
      fprintf(stderr, "Cannot allocate memory for channel: %d\n", cch);
      exit(FAILURE);
    }
  
   
  headptr[cch]->rd = headptr[cch]->data;
  headptr[cch]->wr = headptr[cch]->data;
  headptr[cch]->fs_read = 0;
  headptr[cch]->fs_write = 0;

  nodeptr[cch] = headptr[cch];
  writeptr[cch] = headptr[cch];
  readptr[cch] = headptr[cch];

  writeptr[cch]->rd = headptr[cch]->data;
  readptr[cch]->rd = headptr[cch]->data;
  writeptr[cch]->wr = headptr[cch]->data;
  readptr[cch]->wr = headptr[cch]->data;

  return (0);
}


int get_max_length()
{
  if(chgmax)
    {
      return maxlength;
    }
  else
    {
      return BUFLENGTH;
    }
}

/* store remaining channel's data in  files */
int store_all_channels_data()
{
  int i, nbytes;
  char *tmpbuf;

  if(get_active_write_channels() <= 0)
    {
      fprintf(stderr, "No pending data to be saved !\n");
      return 0;
    }
  
  for (i = 0; i < get_active_write_channels(); i++)
    {
      if (strlen(chan[i].subchannel_name) == 0)
	{
	  fprintf(stderr, "Error when saving data, channel name incorrect !!!\n");
	  continue;
	}
      
      if( readptr[i]->rd == writeptr[i]->wr )
	{
	  fprintf( stderr, "No pending data to be stored for channel: %s\n", chan[i].subchannel_name );
	  continue;
	}

      memcpy(&nbytes, readptr[i], sizeof(int));
      if ( chan[i].fs_write < 0 || nbytes <= 0 )
	{
	  continue; 
	}


     
      if( readptr[i]->rd != writeptr[i]->wr )
	{ 
	  /* get pending data from circular buffer */
	  tmpbuf = read_from_node( i, nbytes );

	  if( tmpbuf )
	    store_channel_data( tmpbuf, nbytes + 2 * sizeof(int), chan[i].subchannel_name );
	  
	  if( tmpbuf )
	    free( tmpbuf );
	  
	}
	     
    }

  return(1);
}

/* load data stored in channel files */
void load_channels_data(int channels_nbr, int option)
{
  int    *files_size; /* array to hold sotred data file's sizes */
  char   *tmpbuf, name_cpy[128];
  int    chan_no, size, nbytes;

#ifdef sgi 
  struct direct **namelist;
#else
  struct dirent **namelist; 
#endif
  int n, data_length;
 
  files_size = (int *)malloc( (channels_nbr)* sizeof(int) );

  if( files_size == NULL )
    {
      printf("cannot allocate memory size = \"%d\" for file_size\n", (channels_nbr)* sizeof(int));
      exit(FAILURE); 
    }
  chan_no = 0;
  size = 0;
  
#ifdef sgi
  fprintf(stderr, "Server:  running on SGI ***\n");
#endif

#ifdef AIX
  fprintf(stderr, "Server:  running on AIX ***\n");
#endif

#ifdef i386
  fprintf(stderr, "Server:  running on Linux ***\n");
#endif
  
  n = scandir(".", &namelist, 0, alphasort); /* read direcotry content */
  if (n < 0)
    perror("scandir");
  else 
    {
      while(n--) 
	{
	  
	  /* select only current channel gossip data files */
	  if(strstr(namelist[n]->d_name, "_gsave") && strstr(namelist[n]->d_name, get_gossip_dir(0))) 
	    {

	      printf("FILE FOUND: \"%s\"\n", namelist[n]->d_name);
	      strcpy (chan[chan_no].subchannel_name, namelist[n]->d_name);
	      memcpy (name_cpy, namelist[n]->d_name, strlen(namelist[n]->d_name));
	      name_cpy[strlen(namelist[n]->d_name)] = '\0';
	      
	      if(option == READ) /* load data in buffer */
		{
		  
		  if( (size = get_file_size(chan[chan_no].subchannel_name)) > 0)
		    {
		      tmpbuf = (char *)malloc(size + 4 * sizeof(int));
		      data_length = 0;

		      if(tmpbuf == NULL)
			{
			  printf("cannot allocate memory size = \"%d\"\n", size + 4 * sizeof(int));
			  break;
			}
		      
		      read_data_file(chan[chan_no].subchannel_name, tmpbuf, size);
		     
		      strcpy (chan[chan_no].subchannel_name, get_channel_name(name_cpy));
		      
		      printf("LOADING DATA: data file found for channel[%d]: %s\n", 
			     chan_no, chan[chan_no].subchannel_name);
		      
		      while(size > 0)
			{

			  /* processing data according to the protocol:
			     | length | data | length | */
			  memcpy(&nbytes, tmpbuf, sizeof(int));
			  
			  if(nbytes > get_max_length())
			    {
			      printf("Problem loading data file; check size option and endianess !!!\n");
			      break;
			    }

			  if( !headptr[chan_no] )
			    {
			      allocate_buffers( chan_no );
			      nbActiveChannels++;
			    }
			  
			  tmpbuf += sizeof(int);
			  write_to_node( tmpbuf , nbytes, chan_no );
			  tmpbuf += nbytes + sizeof(int);
			  size -= (nbytes + 2 * sizeof(int));
			  data_length += (nbytes + 2 * sizeof(int));
			}

		      if(nbytes <= get_max_length())
			{
			  chan[chan_no].fs_read = LOAD;
			
			}

		      tmpbuf -= data_length;

		      if(tmpbuf != NULL)
			{
			  free(tmpbuf);
			}

		    }

		}
	      else if(option == SCAN) /* for buffer allocation */
		{
		  if(size = get_file_size(chan[chan_no].subchannel_name) > 0)
		    files_size[chan_no + 1] = size - 2 * sizeof(int);
		  else
		    files_size[chan_no + 1] = 0;
		}
	      chan_no++;
	    }
	  
	  free(namelist[n]);
	}
      
      free(namelist);
     
    }
  
  if(files_size)
    free(files_size);

}

/* extract channel name from stored data file */
/* channel_name_gsave => channel_name */
char *get_channel_name(char *name)
{
  char nbuf[128] = "";
  char *delimiter = "_", *token;
  

  token = strtok(name, delimiter);
  
  if(token == NULL)
    return NULL;
  
  if(strcmp(token, get_gossip_dir(0)) != 0)
    {
      strncpy (nbuf, token, strlen(token));
      strncpy (nbuf + strlen(nbuf), "_", strlen("_"));
    }

  while((token = strtok(NULL, delimiter)) != NULL && (strcmp(token, "gsave") != 0))
    {
      strncpy (nbuf + strlen (nbuf), token, strlen(token));
      strncpy (nbuf + strlen(nbuf), "_", strlen("_"));
    }


  if(strrchr(nbuf, '_') && strlen(strrchr(nbuf, '_')) == 1 && strlen(nbuf) > 1)
    {
      nbuf[strlen(nbuf) - 1] = '\0';
    }

  strncpy(name, nbuf, strlen(nbuf));
  return name;
  /* return nbuf; */

}

/* get last active channel name */
char *get_last_channel()
{
  if(last_channel && strlen(last_channel) > 0)
    {
      fprintf( stderr, "*** get_last_channel(): %s\n", last_channel);
      return last_channel;
    }
  else
    {
      fprintf( stderr, "*** get_last_channel(): %s\n", "No active channel");
      return "No active channel";
    }
}


/* treat client's commands: read, write, end */
/* read: get data from nodes and send it to the client */
static void event_loop(EXTENDED_CLIENT_SLOT *client)
{
  int fclient;
  char buf[128];
  int i, buflen, rlength;
  char subchannel[256];
  char mode[8];
  int cch;
  char part[8];

  fprintf( stderr, "*** EVENT LOOP ***, pid = %d, calling thread id = %lu ***\n", getpid(), pthread_self());
  fprintf(stderr, "client->socket = %d \n", client->socket);  
  fclient = client->socket;
  fprintf(stderr, "client->client_id = %d \n", client->client_id); 

#ifdef DEBUG 
   fprintf(stderr, "client_NO = %d \n", client_NO);
#endif  

  if(client->command != NULL)
    {
      sscanf(client->command, "%s %s %s", part, mode, subchannel);
      fprintf(stderr, "subchannel: %s, using mode: %s\n", subchannel, mode);
    }

  else
    {
      fprintf(stderr, "client->command is NULL\n");
    }

  send_ack_nack(fclient, IS_OK);

  /* start critical section */
  pthread_mutex_lock(&mutex);

  cch = -1;

  /* fprintf(stderr, "Number of Active Channels = %d\n", get_active_channels()); */
  fprintf(stderr, "Number of Active Channels = %d\n", nbActiveChannels);

  for (i = 0; i < nbActiveChannels; i++)
    {
      if (0 == strcmp(subchannel, chan[i].subchannel_name))
	{
	  cch = i;
	}
    }

#ifdef DEBUG 
  fprintf(stderr, "event_loop(), cch = %d\n", cch);
#endif
  
  if (cch == -1 )
    {
      if(nbActiveChannels <= get_client_count() && get_client_count() > 0)
	{
	  int i = 0;
	  for(i = 0; i < nbActiveChannels; i++)
	    {
	      if(strcmp(chan[cch].subchannel_name, subchannel) == 0)
		{
		  cch = i;
		  break;
		}
	    }

	  if(cch < 0)
	    {
	      nbActiveChannels++;
	      cch = nbActiveChannels - 1;
	      strcpy(chan[cch].subchannel_name, subchannel);
	      chan[cch].status = 0;
	    }

	}
      else
	{
	  fprintf(stderr, "Max Clients Number Reached, cannot go further, \nNumber of Active Channels = \"%d\" !\n", nbActiveChannels); 
	  close(fclient);
	  return;
	}

    }
  

  if (0 == strcmp(mode, "read"))
    {
#ifdef DEBUG
      fprintf(stderr, "event_loop(), open channel[%d]: %s, in mode %s\n", cch, chan[cch].subchannel_name, mode);
#endif
      if(chan[cch].fs_read >= -1)
	chan[cch].fs_read = fclient;
      else
	chan[cch].fs_read = LOAD;
      if(occupied_counter[cch] == 1500)
	occupied_counter[cch] = 0;
      
#ifdef DEBUG
      fprintf(stderr, "event_loop(), open read channel, chan[%d].fs_read = %d\n", cch, chan[cch].fs_read);
#endif
    }
  else if (0 == strcmp(mode, "write"))
    {
      fprintf(stderr, "event_loop(), open channel[%d]: %s, using mode: %s\n", cch, chan[cch].subchannel_name, mode);
      chan[cch].fs_write = fclient;
     
      if (chan[cch].io_index == -1)
	{
	  chan[cch].io_index = nb_glbsockets;
	  nb_glbsockets++;
	}
    }

  /* allocate channel buffers */
  if( !headptr[cch] )
    {
      allocate_buffers( cch );
      
      fprintf( stderr, "event_loop(), allocating buffers for channel: \"%s\",  headptr[%d] = %d\n", chan[cch].subchannel_name, cch, headptr[cch] );
    }
  else if( headptr[cch] )
    {
      fprintf(stderr, "event_loop(), buffers already allocated!, headptr[%d]--->\"%d\"\n", cch, headptr[cch] );
      
    }
  
  pthread_mutex_unlock(&mutex);
  /* end critical section */
  

  fprintf(stderr, "channel no = %d\n", cch);
  fprintf(stderr, "channel: %s\n", chan[cch].subchannel_name);
  fprintf(stderr, "mode: %s\n", mode);
  fprintf(stderr, "chan[%d].fs_read:  %d\n", cch, chan[cch].fs_read);
  fprintf(stderr, "chan[%d].fs_write: %d\n", cch, chan[cch].fs_write);
  fprintf(stderr, "chan[%d].io_index: %d\n", cch, chan[cch].io_index);

#ifdef DEBUG
  for ( i = 0; i < nbActiveChannels; i++ )
    {
      fprintf( stderr, "%d %d %d %d\n", i, chan[i].fs_read, chan[i].fs_write, chan[i].io_index );
    }
#endif
  
  rlength = 128;
  
  if ( 0 == strcmp( mode, "write" ) )
    {
      if( !read_buffer )
	{
	  /* read_buffer = ( char * )malloc( 1024000 * sizeof( char ) ) */;
	}
    }


  if( !buf )
    {
      fprintf( stderr, "Unable to allocate buffer to read client command !!! \n" );
      fprintf( stderr, "Server thread exiting: subchannel: %s\n", chan[cch].subchannel_name );
      
      close( fclient );
      fprintf(stderr, "Cannot allocate buffer, socket closed \n");
      decrement_client_count();
      exit(FAILURE);
    }

  else
    {
      
      while( (buflen = read(fclient, buf, rlength)) > 0 )
	{
	  /* get next command */
	  buf[buflen > 0 ? buflen : 0] = '\0';
	  
	  if(strncmp(buf, "END", 3) == 0) 
	    {
	      strcpy(last_channel, chan[cch].subchannel_name);
	      strncpy (last_channel + strlen(last_channel), ", command: END", strlen(", command: END"));
	      /* echo command to logfile */
	      printf("\"%s\" command received, channel: \"%s\"\n", buf, chan[cch].subchannel_name);  
	      
	      if( writeptr[cch] == readptr[cch] )
		{
		   chan[cch].fs_write = -1;
		   chan[cch].fs_read = -1;
		   chan[cch].io_index = -1;
		}
	
	      send_ack_nack(fclient, IS_OK);    /* send ACK to indicate that command is accepted */
	      reset_timeout_counter();          /* reset TIMEOUT counter */
	      set_exit_requested();
	      close(fclient);

	      break;
	    }
	  else if(strncmp(buf, "READ", 4) == 0 && get_client_count() >= 0) 
	    {
	      int nbytes = 0, tag;
	      char *write_buffer;
	      readers_counter[cch]++;
	      send_ack_nack(fclient, IS_OK);

	      strcpy(last_channel, chan[cch].subchannel_name);
	      strncpy (last_channel + strlen(last_channel), ", command: READ", strlen(", command: READ") - 1);
	      fprintf(stderr, "Begin READ Command using channel: \"%s\"\n", chan[cch].subchannel_name);
	       
	      if( chan[cch].fs_read == LOAD ) /* case load data from file */
		{
		  memcpy( &nbytes, readptr[cch]->rd, sizeof( int ) );  /* get first length tag */
		  fprintf(stderr, "READ Command using channel: \"%s\", loading data from file\n", chan[cch].subchannel_name);
		  
#ifdef DEBUG
		  fprintf(stderr, "gossip_server::event_loop(): READ: %d bytes have been loaded \n", nbytes);
#endif
		  write_buffer = read_from_node( cch, nbytes );
		
		  write_buffer += nbytes + sizeof(int);

		  memcpy( &tag, write_buffer, sizeof( int ) );  /* get second length tag */

		  if (tag != nbytes )
		    {
		      fprintf(stderr, "READ Command, Error reading data from file, length problem, tag1 = \"%d\", tag2 = \"%d\"\n", nbytes, tag);
		      send_ack_nack(fclient, NOT_OK);
		      continue;
		    }

		  write_buffer -= nbytes + sizeof(int);

		  /* send data to client */
		  write_record( fclient, ( unsigned char * )write_buffer, nbytes, 1 ); 
		      
		  send_ack_nack(fclient, IS_OK);
	      
		  if( writeptr[cch]->wr == readptr[cch]->rd )
		    chan[cch].fs_read = -1;

		  continue;
		} /* end case load data from file */
	      
#ifdef DEBUG
	      fprintf(stderr, "gossip_server::event_loop(): READ: No data file loaded \n");
#endif
	      
	      if(occupied_counter[cch] <= 0)  /*  Block until data becomes available */
		
		{
		  blocked_readers[cch]++;
		  
		  /*** begin READER critical section, wait for data to be written  ***/
		  pthread_mutex_lock(&mutr);
		  while(occupied_counter[cch] <= 0)
		    {
		      fprintf(stderr, "READ Command, no data available, \nshould wait for data to be written to channel[%d]: \"%s\"\n", cch, chan[cch].subchannel_name);
		     
		      pthread_cond_wait(&condr, &mutr);
		    }
		  pthread_mutex_unlock(&mutr);
		  /***************** end READER critical section *********************/
		  blocked_readers[cch]--;  /* At wakeup */
		  
		}
	      
	      
	      if( occupied_counter[cch] == 1500 )
		{
		  send_ack_nack( fclient, NOT_OK );
		  continue;
		}
	      occupied_counter[cch]--; /* Either data becomes available 
					  for reading or we were waiting */


	      if ( ! readptr[cch]->rd )
		fprintf( stderr, "READ Command using channel \"%s\" <readptr[%d]->rd is NULL>\n", chan[cch].subchannel_name, cch);
	      	      
	      /* read data length */
	      pthread_mutex_unlock( &mutex );
	      if ( readptr[cch]->rd >= readptr[cch]->data && readptr[cch]->rd != readptr[cch]->data + NODE_SIZE)
		{
		  /* extract first length tag */
		  memcpy( &nbytes, readptr[cch]->rd, sizeof( int ) );  
		  
		  fprintf( stderr, " channel[%d]: \"%s\", data length = \"%d\" \n", cch, chan[cch].subchannel_name, nbytes );

		}
	      pthread_mutex_unlock( &mutex );	      
	      fprintf( stderr, "READ Command using channel \"%s\"\n", chan[cch].subchannel_name);
	      

	      if( nbytes <= 0 )
		{
		  fprintf( stderr, "READ Command using channel \"%s\", Error reading data length: %d (wrong value)\n", chan[cch].subchannel_name, nbytes );
		  send_ack_nack( fclient, NOT_OK );
		  exit( FAILURE );
		}
	      /* read data from node(s) */
	      write_buffer = read_from_node( cch, nbytes );
	      
	      if( !write_buffer )
		{
		  fprintf( stderr, "READ Command using channel \"%s\", Error allocating data buffer \n", chan[cch].subchannel_name);
		  send_ack_nack( fclient, NOT_OK );
		  exit( FAILURE );
		}

	      write_buffer += sizeof( int );
	      

	      if(nbytes >= 30000000)
		fprintf( stderr, "READ Command using channel \"%s\", PROBLEMMMMMMMMMMMMMMM with data length\n", chan[cch].subchannel_name, nbytes);


	      /* send data with length nbytes to client.       */
	      /* data length will be verified in read_record() */
	      write_record( fclient, ( unsigned char * )write_buffer, nbytes, 1 );
	      
#ifdef DEBUG
	      fprintf(stderr, "event_loop(): READ: end[%d] - out[%d] = %d\n", cch, cch, end[cch] - out[cch] );
#endif	  
	      
	      
	      /* data has been read, write thread needs to be waken up */
	      pthread_mutex_lock(&mutw);
	      if( blocked_writers[cch] > 0 )
		{
		  fprintf( stderr, "READ Command, data has been read, \nwrite thread needs to be waken up!\n" );
		  readptr[cch]->fs_read = 0; /* read has been done, write thread can proceed */

		  pthread_cond_broadcast( &condw );
		}
	      pthread_mutex_unlock(&mutw);

	      /* data has been read, write thread needs to be waken up */
	      readers_counter[cch]--;
	      send_ack_nack( fclient, IS_OK ); /* send ACK to indicate that read */
	                                       /* command has been completed     */
	      reset_timeout_counter();
	      fprintf( stderr, "End READ Command for data length = \"%d\", using channel[%d]: \"%s\"\n", nbytes, cch, chan[cch].subchannel_name );

	    }

	  else if( strncmp(buf, "WRITE", 5 ) == 0 && get_client_count() >= 0 ) 
	    {
	      int nbytes;
	      
	      writers_counter[cch]++;
	      fprintf(stderr, "Begin WRITE using channel: \"%s\"\n", chan[cch].subchannel_name);
	      send_ack_nack(fclient, IS_OK);
	      
	      strcpy( last_channel, chan[cch].subchannel_name );
	      strncpy( last_channel + strlen(last_channel ), ", command: WRITE", strlen( ", command: WRITE" ) );
	      
	      nbytes = 0;

	      /* read data from socket */
	      /* read_buffer = ( char * )read_record( fclient, read_buffer, &nbytes, maxlength, 1 ); */
	      read_buffer = ( char * )read_record( fclient, NULL, &nbytes, maxlength, 1 );


	      if(nbytes >= 1000000000)
		fprintf( stderr, "WRITE PROBLEMMMMMMMMMMMMMMMMM wrong data length = \"%d bytes\"\n", nbytes );

	      if( !read_buffer)
		{
		  fprintf(stderr, "WRITE command, read_buffer is NULLL\n");
		  exit(FAILURE);
		}

	      fprintf( stderr, "WRITE Command received, data length = \"%d bytes\" \n", nbytes );
	      /***************** read records ***********************/
#ifdef DEBUG
	      fprintf( stderr, "WRITE command received \"%d bytes\"\n", nbytes );
#endif	  
	      
	      if( !read_buffer )
		{
#ifdef DEBUG
		  fprintf(stderr, "WRITE command, read_buffer is NULL\n");
#endif
		  send_ack_nack( fclient, NOT_OK );
		  continue;
		}
	      
	      /* copy data form linear to circular buffer */
	      /* lock/unlock write buffer access	  */    
	      pthread_mutex_lock(&mutex);
	      write_to_node( read_buffer, cch, nbytes );
	      pthread_mutex_unlock(&mutex);
	      

	      /* Either there is space, or we were waiting */
	      /* and then waken up when space becomes available after read data */
	      occupied_counter[cch]++ ;

	      /* reader thread needs to be waken up */
	      pthread_mutex_lock(&mutr);
	      if(blocked_readers[cch] > 0)
		{
		  fprintf(stderr, "WRITE, reader thread needs to be waken up !!\n");
		  pthread_cond_broadcast(&condr);
		}
	      pthread_mutex_unlock(&mutr);
	      /* reader thread needs to be waken up */
	  
	      writers_counter[cch]--;
	      send_ack_nack(fclient, IS_OK); /* send ACK to indicate that write */
	                                     /* command has been completed */
	      reset_timeout_counter();
	      
	  
	      readptr[cch]->fs_write = fclient;
	      fprintf(stderr, "End WRITE command using channel[%d]: \"%s\"\n", cch, chan[cch].subchannel_name );

	      if( read_buffer )
		free( read_buffer );
	    }

	  else
	    { /* assumed to be a bum call */
	      printf("Bad Command: \"%s\"\n", buf);
	      send_ack_nack(fclient, NOT_OK);  /* send NACK to indicate that command is rejected */
	      reset_timeout_counter();
	   
	      if(buf)
		{
		  fprintf(stderr, "Thread exiting, before free(buf), channel[%d]: \"%s\"\n", cch, chan[cch].subchannel_name );
		  free(buf);
		}

	      continue;                        /* connection terminated, process next client */
	    }


	} /* end while read/write from client  */
    }
 
  strcpy(last_channel, "No Active Channel");
 
  close(fclient);


  pthread_mutex_lock(&mutex);
  chan[cch].status += 1;

  fprintf(stderr, "Server: freeing channel[%d], status = %d\n", cch, chan[cch].status);

  if( !headptr[cch] )
    {
      fprintf(stderr, "Thread exiting, headptr[%d] is NULL\n", cch);
    }
  
  /* else if( headptr[cch] && chan[cch].status > 0 /\* && cch != 0 && cch != 1 *\/ ) */
  else if( headptr[cch] && chan[cch].status >= 2 )
    {
      /* free channel list nodes and buffers */
      freenodes( headptr[cch], cch );
      /* reset read and write flags */
      chan[cch].fs_read = -1;
      chan[cch].fs_write = -1;

      /* if ( cch == 0 || cch == 1) */
      if( headptr[cch] )
	{
	  fprintf(stderr, "Thread exiting, headptr[%d] NOT NULL\n", cch);
	  headptr[cch] = NULL;

	  if( !headptr[cch] )
	    fprintf(stderr, "Thread exiting, headptr[%d] NULL\n", cch);
	}

      fprintf(stderr, "Server: freeing channel[%d]\n", cch);
    } 

  /* else if( chan[cch].status < 0 ) */
  else if( chan[cch].status >= 2 )
    {
      headptr[cch] = NULL;
      fprintf(stderr, "Server: freeing channel[%d], status = %d\n", cch, chan[cch].status);
    }

  /* pthread_mutex_unlock(&mutex); */

  if( headptr[cch] )
    {
      fprintf(stderr, "Thread exiting, headptr[%d] NOT NULL, status = %d\n", cch, chan[cch].status);
    }
  decrement_client_count();

  fprintf(stderr, "Server thread exiting from active channel[%d]: \"%s\"\n", cch, chan[cch].subchannel_name);
  pthread_mutex_unlock(&mutex);

}

/* free memory allocated for node */
void freenodes( struct node *head_node, int cch )
{
    struct node *temp;
    int counter = 0;

#ifdef DEBUG
    fprintf( stderr, "freenodes( ), Satrt ..., channel[%d]\n", cch);
#endif

    while( head_node ) 
      {
	if( head_node->next )
	  {
	    temp = head_node->next;

	    if( head_node->data )
	      {
		free( ( char * )head_node->data);
		head_node->data = NULL;
			      }

	    free( ( struct node * )head_node );
	    head_node = NULL;
	    head_node = temp;
	    counter++;
	  }
	else
	  {
	    if( head_node )
	      {

		if( head_node->data )
		  {
		    free( ( char * )head_node->data);
		    head_node->data = NULL;
		  }

		free( ( struct node * )head_node );
		head_node = NULL;

		counter++;
	      }
	    break;
	  }
	
      }

    /* chan[cch].status += 1; */
    nbActiveChannels--;

#ifdef DEBUG
    fprintf( stderr, "freenodes(), End, ..., channel[%d], freed node(s) counter = \"%d\"\n", cch, counter);
#endif

}


/* get sattus of all channels (write/read mode) */
/* send on socket a message with their number   */
/* name and mode of each of them                */ 
int get_status_all( int fserver )
{
  int cch = 0, nbytes, nodes = 0, channels;
  char buf[1024]; 

  fprintf( stderr, "get_status_all(), Start ...\n" );
  channels = get_active_channels();
  if( channels <= 0 )
    {
      nbytes = snprintf( buf, 1024, "No active channels" );
      if( write( fserver, buf, nbytes ) <= 0 ) 
	{
	  fprintf( stderr, "Can't write status\n" );
	}
      return cch;
    }
  
  if ( channels > 1 )
    nbytes = snprintf( buf, 1024, "There are %d active channels\n", channels );
  
  else if (channels == 1 )
    nbytes = snprintf( buf, 1024, "There is \"%d\" active channel\n", channels );

  if( write( fserver, buf, nbytes ) <= 0 ) 
    {
      fprintf( stderr, "Can't write status\n" );
    }
  
  
  for( cch = 0; cch < get_active_channels(); cch++ )
    {

      if( strlen( chan[cch].subchannel_name ) > 1 )
	{
	  nbytes = snprintf( buf, 1024, "\n%d. %s: ", (cch + 1), chan[cch].subchannel_name );


	  if( write( fserver, buf, nbytes ) <= 0 ) 
	    {
	      fprintf( stderr, "Can't write status\n" );
	    }

	  if(chan[cch].fs_read > 0)
	    {
	      nbytes = snprintf( buf, 1024, " -mode: READ" );
	      if( write( fserver, buf, nbytes) <= 0 ) 
		{
		  fprintf( stderr, "Can't send READ mode" );
		} 
	    }
	  
	  if( chan[cch].fs_write > 0 )
	    {
	      nbytes = snprintf( buf, 1024, " -mode: WRITE" );
	      if( write( fserver, buf, nbytes ) <= 0 ) 
		{
		  fprintf( stderr, "Can't send WRITE mode\n" );
		}
	    }
	 
	  nodes = get_nodes_number( cch );

	  fprintf( stderr, "Status, free space for channel[%d]: \"%d\"\n", cch, get_free_space( cch ) );

	  if( nodes > 0 && TOTAL_SIZE > 0 )
	    {
	      
	 
	      nbytes = snprintf( buf, 1024, ", using: \"%d\" node(s), free space: \"%d\"", nodes, get_free_space( cch ));
	      if( write( fserver, buf, nbytes ) <= 0 ) 
		{
		  fprintf( stderr, "Can't write space infos!!\n" );
		}
	    }


	  if( chan[cch].fs_write < 0 && chan[cch].fs_read < 0 )
	    {
	      nbytes = snprintf( buf, 1024, " Non active" );
	      if( write( fserver, buf, nbytes ) <= 0 ) 
		{
		  fprintf( stderr, "Can't send WRITE mode\n" );
		}
	    }
	}
    }

  return cch; 
}

int get_free_space( int cch )
{
  int free_space = 0, write_pos = 0, read_pos = 0;

  write_pos = get_node_position( writeptr[cch], cch );
  read_pos = get_node_position( readptr[cch], cch );

  fprintf(stderr, "get_free_space(%d), write node position = %d\n", cch, write_pos );
  fprintf(stderr, "get_free_space(%d), read node position = %d\n", cch, read_pos );

  if (write_pos == read_pos )
    {
      if ( writeptr[cch] == readptr[cch] )
	{/* data in one node */
	  if( writeptr[cch]->wr == readptr[cch]->rd )
	    { /* read and write pointers have the same position */
	      free_space += NODE_SIZE * get_nodes_number( cch );
	      
	    } 
	  
	  else
	    {/* read and write pointers have different positions */
	      free_space += NODE_SIZE * ( get_nodes_number( cch ) - 1 ) + writeptr[cch]->data + NODE_SIZE - writeptr[cch]->wr + readptr[cch]->rd - readptr[cch]->data;
	      
	    }
	}   
    }	

  else if ( write_pos > read_pos )
    {/* data in different nodes */
      free_space += writeptr[cch]->data + NODE_SIZE - writeptr[cch]->wr + NODE_SIZE * ( get_nodes_number( cch ) - (write_pos + read_pos ) ) + readptr[cch]->rd - readptr[cch]->data;
    }
  else if ( write_pos > read_pos )
    {/* data in different nodes */
      free_space += ( read_pos - write_pos ) * NODE_SIZE + writeptr[cch]->data + NODE_SIZE - writeptr[cch]->wr + readptr[cch]->rd - readptr[cch]->data;
    }

  fprintf(stderr, "get_free_space(), channel[%d], free space: \"%d\"\n", cch, free_space);
  return free_space;

}

/* identify all active channels (read/write mode) */
/* return their total number                     */
int get_active_channels()
{

  int cch = 0, counter = 0;
  
  if( nbActiveChannels <= 0 )
    {
      return counter = 0;
    }
  
  for(cch = 0; cch < nbActiveChannels; cch++)
    {
      if(chan[cch].fs_read > 0 || chan[cch].fs_write > 0)
	counter++;
    }
  
  return counter;
}

/* identify only write active channels */
/* return their number                 */
int get_active_write_channels()
{
  int cch = 0, counter = 0;
  
  if(nbActiveChannels <= 0)
    {
      return counter = 0;
    }
  
  for(cch = 0; cch < nbActiveChannels; cch++)
    {
      if(chan[cch].fs_write > 0 && chan[cch].fs_read < 0)
	counter++;
    }
  

  return counter;

}

/* get channel index using its request mode   */
/* (read/write) return channel index if found */
/* -1 else                                    */
int look_for_channel(char *channel, int mode)
{
  int cch;
  for(cch = 0; cch < nbActiveChannels; cch++)
    {
      if(chan[cch].fs_read > 0 && chan[cch].fs_write < 0 && (strcmp(chan[cch].subchannel_name, channel) == 0) && occupied_counter[cch] <= 0)
	return cch;
    }

  return -1;
}

/* cancel read request waiting for data            */
/* identify read channel using its socket          */
/* descriptor, get its index and release its mutex */
void cancel_read(char *channel)
{
  int cch;
  
  if((cch = look_for_channel(channel, READ)) >= 0)
    {
      occupied_counter[cch] = 1500;
      chan[cch].fs_read = -1;
      pthread_cond_broadcast(&condr);
    }
#ifdef DEBUT
  fprintf(stderr, "cancel_read(%s), cch[%d]\n", (char *)channel, cch);	
#endif
}


/* write data to current write node */
void write_to_node(char *buffer, int cch, int size )
{
  int lspace;
  
#ifdef DEBUG
  fprintf(stderr, "write_to_node(), start ..., channel: \"%s\"\n", chan[cch].subchannel_name);
#endif

  lspace = writeptr[cch]->data + NODE_SIZE - writeptr[cch]->wr;

  if( lspace <= 0)
    {
      find_write_node( cch );

    }

  if (lspace >= size + 2 * sizeof( int ))
    {
      memcpy(writeptr[cch]->wr, (char *)&size, sizeof( int ));

      writeptr[cch]->wr += sizeof( int );
      
      memcpy(writeptr[cch]->wr, buffer, size);
   
      writeptr[cch]->wr += size;

      memcpy(writeptr[cch]->wr, (char *)&size, sizeof( int ));

      writeptr[cch]->wr += sizeof( int );
    }

  else
    {
      memcpy(writeptr[cch]->wr, (char *)&size, sizeof( int ) );

      writeptr[cch]->wr += sizeof( int );

      memcpy(writeptr[cch]->wr, buffer, (lspace - sizeof( int )) );

      writeptr[cch]->wr += (lspace - sizeof( int ));

      if( lspace == NODE_SIZE )
	{
	  headptr[cch]->rd = headptr[cch]->data;
	}

      buffer += (lspace - sizeof( int ));


      if( !buffer )
	fprintf(stderr, "write_to_node(), channel: \"%s\", data buffer NULL, exiting\n", chan[cch].subchannel_name);

      check_next_node( cch );
      
      lspace = size + 2 * sizeof(int) - lspace;

      if ( NODE_SIZE > lspace )
	{
	  memcpy(writeptr[cch]->wr, buffer, lspace - sizeof(int));

	  buffer += (lspace - sizeof(int));

	  writeptr[cch]->wr += (lspace - sizeof(int));

	  memcpy(writeptr[cch]->wr, (char *)&size, sizeof( int ) );

	  writeptr[cch]->wr += sizeof( int );

	}

      else
	{
	  memcpy(writeptr[cch]->wr, buffer, NODE_SIZE);

	  writeptr[cch]->wr += NODE_SIZE;
	  buffer += NODE_SIZE;
	  
	  lspace  -= NODE_SIZE;

	  while( lspace > NODE_SIZE)
	    {
	      check_next_node( cch );

	      memcpy(writeptr[cch]->wr, buffer, NODE_SIZE);
	      writeptr[cch]->wr += NODE_SIZE;

	      buffer += NODE_SIZE;

	      lspace  -= NODE_SIZE;

	    }

	  if ( writeptr[cch]->wr - writeptr[cch]->data ==  NODE_SIZE )
	    {
	      if(lspace > 0 )
		check_next_node( cch );

	    }
	  if(lspace > 0 )
	    {
	      memcpy(writeptr[cch]->wr, buffer, lspace - sizeof( int ));
	      writeptr[cch]->wr += lspace - sizeof( int );	   
	      memcpy(writeptr[cch]->wr, (char *)&size, sizeof( int ) );
	      writeptr[cch]->wr += sizeof( int );	

	    }

	}

    }
#ifdef DEBUG 
  fprintf(stderr, "write_to_node(), END ..., channel: \"%s\"\n", chan[cch].subchannel_name);
#endif

}

/* check next node to current write      */
/* node for available space, else either */
/* insert a new one or restart from head */    
void check_next_node( int cch )
{
  struct node *new_node;

#ifdef DEBUG   
  fprintf(stderr, "check_next_node(), start, ..., channel: \"%s\"\n", chan[cch].subchannel_name);
#endif

  if ( writeptr[cch]->next )
    {
     
      new_node = writeptr[cch]->next;

      if( new_node->wr == new_node->rd )	     
	/* node start, no data */
	{
	  writeptr[cch] = new_node;
	  writeptr[cch]->wr = new_node->data;
	  writeptr[cch]->rd = new_node->data;
	}

      if( new_node->wr == new_node->rd && new_node->wr == new_node->data )	     
	/* node start, no data */
	{
	  writeptr[cch] = new_node;
	  writeptr[cch]->wr = new_node->data;
	  writeptr[cch]->rd = new_node->data;
	}

      else if( new_node->wr == new_node->rd && new_node->wr == new_node->data + NODE_SIZE )	     
	/* data read */
	{
	  writeptr[cch] = new_node;
	  writeptr[cch]->wr -= NODE_SIZE;
	  writeptr[cch]->rd -= NODE_SIZE;	
	}

      else
	{
	  /* All nodes occupied, insert a new one */
	  if ( check_left_space( ) )
	    { 
	      if( !insert_node(cch) )
		{
		  fprintf(stderr, "check_next_node(), cch[%d], cannot insert a new node, exiting !!!!\n", cch);
		  exit(FAILURE);
		}

	    }
	  else
	    { /* max space reached in write mode */
	      /* cannot go further		 */
	      fprintf(stderr, "Cannot add more nodes, no more space can be allocated, exiting!!!\n");
	      exit(FAILURE);
	    }
	}
    }

  else if(headptr[cch]->wr == headptr[cch]->rd && headptr[cch]->wr == headptr[cch]->data )
    { /* node list start */

      writeptr[cch] = headptr[cch];
      writeptr[cch]->wr = headptr[cch]->data;
      writeptr[cch]->rd = headptr[cch]->data;
    }

  else if(headptr[cch]->wr == headptr[cch]->rd && headptr[cch]->wr == headptr[cch]->data + NODE_SIZE )
    { /* data read from first node */

      writeptr[cch] = headptr[cch];
      writeptr[cch]->wr = headptr[cch]->data;
      writeptr[cch]->rd = headptr[cch]->data;
    }

  else if(headptr[cch]->wr == headptr[cch]->data && headptr[cch]->rd == headptr[cch]->data + NODE_SIZE )
    { /* data read from first node */
     
      writeptr[cch] = headptr[cch];
      writeptr[cch]->wr = headptr[cch]->data;
      writeptr[cch]->rd = headptr[cch]->data;
    }
  else
    {
      find_write_node( cch );
    }

#ifdef DEBUG 
  fprintf(stderr, "check_next_node(), End, ..., channel: \"%s\"\n", chan[cch].subchannel_name );
#endif

}

/* return list nodes number */
int get_nodes_number( int cch )
{
  struct node *new_node;
  int counter;
  new_node = headptr[cch];

  counter = 0; 	
  while ( new_node )
    {
      counter++;
      new_node = new_node->next;

      
    }
#ifdef DEBUG 
  fprintf(stderr, "get_nodes_number(), channel[%d], counter = \"%d\"\n", cch, counter);
#endif

  return counter;	
}

/* return a specefic node */
/* postion in the list    */
int get_node_position( struct node *elt, int cch  )
{
  struct node *new_node;
  int counter;
  new_node = headptr[cch];

  counter = 0; 	
  while ( new_node )
    {
      counter++;
      if( new_node == elt)
	break;
      new_node = new_node->next;
    }
  return counter;

}

/* browse list nodes */
void list_nodes(int cch)
{
  struct node *new_node;
  new_node = headptr[cch];

  while ( new_node )
    {
      fprintf(stderr, "list_nodes(), channel: \"%s\", new_node->data: %d \n", chan[cch].subchannel_name, new_node->data );

      if( new_node->next && new_node != new_node->next )
	{
	  new_node = new_node->next; 
	}
      else
	break;
    }

}

/* find current write node starting from  */
/* head node by checking that the node is */
/* free from data. Set write node to the  */
/* free node available                    */
void find_write_node( int cch )
{
  int lspace, found, counter;
  struct node *new_node;

#ifdef DEBUG 
  fprintf(stderr, "find_write_node(), Start, ..., channel: \"%s\"\n", chan[cch].subchannel_name);
#endif

  new_node = headptr[cch];

  found = 0;
  counter = 0;

  while ( new_node )
    {
      if( new_node->wr == new_node->rd && new_node->wr == new_node->data )
	{ /* node start, no data */
	  
	  if ( writeptr[cch] != new_node )
	    {
	      writeptr[cch] = new_node;
	      writeptr[cch]->data = new_node->data;
	      writeptr[cch]->rd = writeptr[cch]->data;
	      writeptr[cch]->wr = writeptr[cch]->data;
	  
	      writeptr[cch]->fs_read = 0;
	      writeptr[cch]->fs_write = 0;

	    }
	  else
	    {
	      writeptr[cch]->rd = writeptr[cch]->data;
	      writeptr[cch]->wr = writeptr[cch]->data;
	      writeptr[cch]->fs_read = 0;
	      writeptr[cch]->fs_write = 0;

	    }
	  found = 1;
	  break;
	}

      else if( new_node->wr == new_node->rd && new_node->wr == new_node->data + NODE_SIZE )
	{ /* data read, node can be reused */

	 
	  if ( writeptr[cch] != new_node )
	    {
	      if( new_node->next )
		{
		  writeptr[cch]->next = new_node->next;
		 
		}
	      writeptr[cch] = new_node;
	      writeptr[cch]->data = new_node->data;
	      writeptr[cch]->rd = writeptr[cch]->data;
	      writeptr[cch]->wr = writeptr[cch]->data;
	      writeptr[cch]->fs_read = 0;
	      writeptr[cch]->fs_write = 0;

	    }
	  else
	    {
	      writeptr[cch]->rd = writeptr[cch]->data;
	      writeptr[cch]->wr = writeptr[cch]->data;
	      writeptr[cch]->fs_read = 0;
	      writeptr[cch]->fs_write = 0;

	    }
	  found = 1;

	  break;
	}


      else if( new_node->wr > new_node->rd && new_node->wr < new_node->data + NODE_SIZE)	
	{ /* node partially occupied */
	 
	  lspace = NODE_SIZE + (new_node->data) - new_node->wr;

	  if( lspace > 0)
	    {
	      if ( writeptr[cch] != new_node )
		{
		  writeptr[cch] = new_node;
		  writeptr[cch]->data = new_node->data;
		  writeptr[cch]->rd = writeptr[cch]->data;
		  writeptr[cch]->wr = writeptr[cch]->data;
		  writeptr[cch]->fs_read = 0;
		  writeptr[cch]->fs_write = 0;
		}
	      found = 1;
	      break;
	    }
	}

            
      new_node = new_node->next;
      counter++;

    }

  if ( !found )
    {
      if ( check_left_space( ) )
	{

	  if( !insert_node(cch) )
	    {
	      fprintf(stderr, "find_write_node(), cch[%d], cannot insert a new node, exiting !!!!\n", cch);
	      exit(FAILURE);
	    }
	}
      else
	{
	  fprintf(stderr, "Cannot add more nodes, no more space left!!!\n");
	  exit( FAILURE );		  
	}
    }

#ifdef DEBUG 
  fprintf( stderr, "find_write_node(), End, ..., channel: \"%s\"\n", chan[cch].subchannel_name );
#endif
}

/* check left space before inserting     */
/* a new node, return 0 if the max space */
/* is reached                            */
int check_left_space( )
{
  fprintf( stderr, "check_left_space(), TOTAL_SIZE ============= %d\n", TOTAL_SIZE);
  
  return TOTAL_SIZE <= MAX_BUFFER ? 1 : 0;
}

/* insert a new node in the list */
/* after the current write node  */
int insert_node( int cch )
{
  struct node *new_node;

#ifdef DEBUG   
  fprintf( stderr, "insert_node(), Start, ..., channel: \"%s\"\n", chan[cch].subchannel_name );
#endif

  new_node = initialize();
 
  if( !new_node )
    {
      fprintf( stderr, "insert_node(), new_node is NULL, exiting !!!\n" );
      return 0;
    }
  
  if( !new_node->data )
    {
      fprintf( stderr, "insert_node(), new_node->data is NULL, exiting !!!\n" );
      return 0;
    }

  if ( writeptr[cch] == nodeptr[cch] ) /* current write node is the tail */
    {				      /* new node becomes tail */
                                         
      new_node->next = nodeptr[cch]->next;
      nodeptr[cch]->next = new_node;
      nodeptr[cch] = new_node;
      writeptr[cch] = new_node;
      
    }
  else /* write node is an intermediate one */
    {
      new_node->next = writeptr[cch]->next;
      writeptr[cch]->next = new_node;
      writeptr[cch] = new_node;
    }

  writeptr[cch]->data = new_node->data;
  writeptr[cch]->rd = writeptr[cch]->data;
  writeptr[cch]->wr = writeptr[cch]->data;
  writeptr[cch]->fs_read  = 0;
  writeptr[cch]->fs_write = 0;
  node_counter[cch] += 1;
  
#ifdef DEBUG 
  fprintf(stderr, "insert_node(), channel: \"%s\", nodes number = %d\n", chan[cch].subchannel_name, node_counter[cch] );
#endif
  
  return 1;
}

/* return current read node */
/* position in the list     */
int get_read_node_number( int cch )
{
  int node_nbr;
  struct node *new_node;
  node_nbr = 1;

  new_node = headptr[cch];

  while ( new_node )
    {
      if( new_node->data == readptr[cch]->data) 
	{
	  return node_nbr;
	}
      new_node = new_node->next;
      node_nbr++;
    }

  return node_nbr;

}

/* return current write node */
/* position in the list      */
int get_write_node_number( int cch )
{
  int node_nbr;
  struct node *new_node;
  node_nbr = 1;

  new_node = headptr[cch];

  while ( new_node )
    {
      if( new_node->data == writeptr[cch]->data) 
	{
	  return node_nbr;
	}
      new_node = new_node->next;
      node_nbr++;
    }

  return node_nbr;
}

/* read data from list nodes using */
/* the current read node pointer   */
/* return data buffer containing:  */
/* | length | data | length |      */
char * read_from_node( int cch, int length )
{
  int size_back = 0;
  int space, counter, restant;

#ifdef DEBUG 
  fprintf( stderr, "read_from_node(),  Start, ..., channel: \"%s\"\n", chan[cch].subchannel_name );
#endif

  if( !node_buffer || length > maxsize)
    {
      maxsize = length;
      fprintf( stderr, "gossip_server::read_from_node() maxsize ================== %d\n", maxsize );
      /* node_buffer = NULL; */
      node_buffer = (char *)malloc( maxsize + 2 * sizeof(int) );
    }

  if( node_buffer == NULL )
    {
      fprintf( stderr, "Unable to allocate memory for read buffer with size = %d\n", length );
      return NULL;
    }
   
  if( !readptr[cch] )
    {
      fprintf( stderr, "read_from_node(),  readptr[cch] NULL, for channel: \"%s\"\n", chan[cch].subchannel_name );
      return NULL;
    }
  
  if( readptr[cch]->rd == readptr[cch]->data && length + 2 * sizeof(int) <= NODE_SIZE) 
    {/* read from begining of node */
      memcpy( node_buffer, readptr[cch]->rd, length + 2 * sizeof(int) );
      readptr[cch]->rd += length + 2 * sizeof(int);
      
      if( readptr[cch]->rd == writeptr[cch]->wr )
	{
	  readptr[cch]->rd = readptr[cch]->data;
	  writeptr[cch]->wr = writeptr[cch]->data;
	} 
      
    } 
  
  else if ( NODE_SIZE + readptr[cch]->data - readptr[cch]->rd >= ( length + 2 * sizeof(int) ) )
    { /* all data is in the same node */
      
      memcpy( node_buffer, readptr[cch]->rd, length + 2 * sizeof(int) );

      readptr[cch]->rd += length + 2 * sizeof(int);
      
      if( readptr[cch]->rd == writeptr[cch]->wr )
	{
	  readptr[cch]->rd = readptr[cch]->data;
	  writeptr[cch]->wr = writeptr[cch]->data;
	} 
    }	
  else /* read from several nodes */
    {
      /* read first part of data from the cureent read node */
      int rspace = NODE_SIZE + readptr[cch]->data - readptr[cch]->rd;

      memcpy( node_buffer, readptr[cch]->rd, rspace );

      node_buffer += rspace;
      size_back += rspace;
      readptr[cch]->rd = readptr[cch]->data;
      
      readptr[cch]->fs_write = 0;
      readptr[cch]->fs_read = 0;
      
      /* if end of list nodes restart from the head node */
      if( readptr[cch] == nodeptr[cch] && nodeptr[cch] != headptr[cch])
	{
	  readptr[cch] = headptr[cch];
	  
	  if(readptr[cch]->rd == readptr[cch]->data + NODE_SIZE)
	    {
	      writeptr[cch] = headptr[cch];
	      
	    } 
	  
	} 
      else
	{

	  if( !readptr[cch]->next )
	    {	
	      fprintf(stderr, "read_from_node(), channel: \"%s\", readptr[%d]->next NULL, exiting !!!\n", chan[cch].subchannel_name, cch);


	      exit(FAILURE);
	    }

	  /* else continue in next node */
	  readptr[cch] = readptr[cch]->next;

	}
      restant = (length + 2*sizeof(int) - rspace ) - NODE_SIZE;

      if (restant <= 0)
	{/* all remaining data is in the current read node */
	  memcpy( node_buffer, readptr[cch]->rd, (length + 2 * sizeof(int) - rspace) );
	  readptr[cch]->rd += (length + 2 * sizeof(int) - rspace);
	 
	  /* if end of node list point to the head node */
	  if( readptr[cch] == nodeptr[cch] && readptr[cch]->rd == readptr[cch]->wr )	
	    {
	      readptr[cch] = headptr[cch];
	      writeptr[cch] = headptr[cch];
	      readptr[cch]->wr = headptr[cch]->data;
	      writeptr[cch]->wr = headptr[cch]->data;
	      
	    }
	}
      else /* remaining data is in multiple nodes */
	{
	  /* read all current read node */
	  memcpy( node_buffer, readptr[cch]->rd,  NODE_SIZE);
	  
	  node_buffer += NODE_SIZE;
	  size_back += NODE_SIZE;  
	  
	  readptr[cch]->rd += NODE_SIZE;
	  if( readptr[cch]->rd == readptr[cch]->wr )
	    {
	      readptr[cch]->rd = readptr[cch]->data;
	      readptr[cch]->wr = readptr[cch]->data;
	    }

	  /* recompute remaining data length */
	  space = (length + 2 * sizeof(int) - rspace ) - NODE_SIZE;
	  counter = 1;

	  if( readptr[cch]->next )
	    {
	      readptr[cch] = readptr[cch]->next;
	    } 
	  else
	    {
	      readptr[cch] = headptr[cch];
	    }
	  /* if remaining data is in more than one node */
	  while(space >= NODE_SIZE)
	    {
	      memcpy( node_buffer, readptr[cch]->rd, NODE_SIZE );
	      space -=  NODE_SIZE;
	      /* reset read node offset to the begining of node */
	      readptr[cch]->rd = readptr[cch]->data;
	      readptr[cch]->wr = readptr[cch]->data;

	      if( readptr[cch]->next )
		{
		  readptr[cch] = readptr[cch]->next;
		}
	      else
		{
		  readptr[cch] = headptr[cch];
		}
	      
	      node_buffer += NODE_SIZE;
	      size_back += NODE_SIZE;
	      counter++;
	    } /* end while loop */
	  

	  if( space > 0)
	    { /* read remaing data less than node length */
	      
	      memcpy( node_buffer, readptr[cch]->rd, space );
	      readptr[cch]->rd += space;
	      
	      if( readptr[cch]->rd == writeptr[cch]->wr )
		{
		  readptr[cch]->rd = readptr[cch]->data;
		  writeptr[cch]->wr = writeptr[cch]->data;
		 		  
		  if( readptr[cch] == nodeptr[cch] || !check_remaining_nodes( cch ))
		    {/* if all data has been read, or the current node */
		     /* is the list tail node, restart from the list head node */
		      readptr[cch] = headptr[cch];
		      writeptr[cch] = headptr[cch]; 
		      writeptr[cch]->wr = writeptr[cch]->data;
		      readptr[cch]->rd = readptr[cch]->data;
		   
		    } 
		}
	      
	    }

	  
	}/* end read from multiple nodes */
    }
   
  node_buffer -= size_back;

#ifdef DEBUG   
  fprintf(stderr, "read_from_node(), End, ..., channel: \"%s\"\n", chan[cch].subchannel_name);
#endif

  return node_buffer; 
} 

/* starting with current write node */
/* check data on following nodes    */
int check_remaining_nodes( int cch )
{
  int result;
  struct node *new_node;
  new_node = writeptr[cch];
  result = 0;

  while ( new_node )
    {
      if(new_node->next)
	{
	  new_node = new_node->next; 
	
	  if(new_node->wr != new_node->data)
	    result = 1;
	}
      else
	return result;
    }
  return result;
}

/* allocate node and data buffer */
struct node * initialize( void )
{
  struct node *newnode;

  newnode = ( (struct node *) malloc(sizeof( struct node ) ));

  if( !newnode )
    {
      fprintf(stderr, "Cannot create allocate memory for a new node data structure !!!\n");
      return NULL;
    }

  newnode->next = NULL;

  newnode->data = (char *)malloc(NODE_SIZE);

  if( !newnode->data )
    {
      fprintf(stderr, "Cannot create allocate memory for a new node data !!!\n");
      return NULL;
    }

  /* increment node's total space allocated */
  TOTAL_SIZE += NODE_SIZE;
  return newnode;
}
