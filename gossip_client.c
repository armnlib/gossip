#include <rpnmacros.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <pthread.h>

#include <fcntl.h>
#include <sys/param.h>
#include <gossip.h>

#include <stdlib.h>

#define null 0

#define STATUS 1
#define CKTP   2
#define NOOP   3
#define ABORT  4

void display_options(char *prog);

void main(int argc, char **argv)
{
  int option, ier;
  char reply[1024];

  if(argc < 2)
    {
      display_options( argv[0] );
    }

  option = atoi(argv[1]);
  ier = 0;

  if( option == STATUS )
      {
      ier = get_status( reply );
      if( *reply )
        {
        fprintf( stderr, "%s\n", reply );
        fprintf( stderr, "\"STATUS\" command sent, return code = %d\n", ier );
        if( ier < 0)
           exit(133);
        else if( strstr(reply, "No active channels" ) )
          {
           exit(1);
          }
        else if( strstr(reply, "There is/are" ) )
          {
           exit(0);
          }
        }
     }
  else if(option == CKTP)
    {
      ier = send_command("CKPT");

      fprintf(stderr, " \"CKPT\" command sent, return code = %d\n", ier);
      if( ier < 0)
        exit(133);
    }
  else if(option == NOOP)
    {
      ier = send_command("NOOP");
      fprintf(stderr, "\"NOOP\" command sent, return code = %d\n", ier);
      if( ier < 0)
         exit(133);
    }
  else if(option == ABORT)
    {
      ier = send_command("ABORT");
      fprintf(stderr, " \"ABORT\" command sent, return code = %d\n", ier);
      if( ier < 0)
        exit(133);
    }
  else if(option == 0 && argc > 2)
    {
      ier = send_command(argv[2]);
      fprintf(stderr, " \"%s\" command sent, return code = %d\n", argv[2], ier);
      if( ier < 0)
        exit(133);
    }
  else
    {
      fprintf(stderr, "---> Unknown option: \"%s\" !\n", argv[1]);
      display_options(argv[0]);
    }
  exit(ier);
}

void display_options(char *prog)
{
  fprintf(stderr, "Usage: %s option\n", prog);
  fprintf(stderr, "option = 1 => send STATUS command\n");
  fprintf(stderr, "option = 2 => send CKTP command\n");
  fprintf(stderr, "option = 3 => send NOOP command\n");
  fprintf(stderr, "option = 4 => send ABORT command\n");
  fprintf(stderr, "Usage: %s 0 'arbitrary command string'\n", prog);
  exit(133);
}
