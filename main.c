#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>


#define UNIT 2662
#define BLOCK_SIZE 640
#define PORTION 13*1024
#define PACKET 4*1024

#define BACKLOG 200

struct arguments{
	float rate;
	char* host;
	unsigned int port;
};

void read_input(int argc, char* argv[],struct arguments* args);
float parse_rate(void);
void parse_location(char* location, struct arguments* args);

void create_warehouse(struct arguments* args);
void worker(int write_descriptor, struct arguments* args);
void connector(int read_descriptor, struct arguments* args);

void warehouseman(int socket, int read_descriptor);

void registering( int sockfd, char * Host, in_port_t Port );
int connecting( int sockfd );
void handler( int sig, siginfo_t * info, void * data );
void disconnect_handler( void );

int main(int argc, char* argv[])
{
	struct arguments args = {};
	disconnect_handler();
	read_input(argc,argv,&args);
	printf("rate: %f\n",args.rate);
	printf("address: %s\n",args.host);
	printf("port: %u\n",args.port);
	create_warehouse(&args);
	return 0;
}


void create_warehouse(struct arguments* args)
{
	int pipe_descriptors[2]; //0 - READ, 1 - WRITE
	if(pipe(pipe_descriptors) == -1)
	{
		perror("Pipe creation failure\n");
		exit(EXIT_FAILURE);
	}
	pid_t p = fork();
	switch (p) {
		case -1:
			perror("fork failure");
			exit(2);
		case 0:
			if(close(pipe_descriptors[0]) == -1)
			{
				perror("Close read pipe descriptor failure");
				exit(EXIT_FAILURE);
			}
			worker(pipe_descriptors[1],args); // create worker
			break;
		default:
			if(close(pipe_descriptors[1]) == -1)
			{
				perror("Close write pipe descriptor failure");
				exit(EXIT_FAILURE);
			}
			connector(pipe_descriptors[0],args); // create connector
	}
}

void worker(int write_descriptor, struct arguments* args)
{
	char buffer[BLOCK_SIZE];
	float prod_time = BLOCK_SIZE/(args->rate*UNIT); // accuracy to micro seconds
	int sec = (int)prod_time;
	int micro = (int)((prod_time - (float)sec)*1000000);
	int nano = micro*1000;
	struct timespec production_time = { .tv_sec = sec, .tv_nsec = nano };

	printf("%d %d\n",sec,nano);

	while(1)
	{
		for(int i=65; i<=122; i++)
		{
			if(i>90 && i < 97) continue;
			memset(buffer,(char)i,BLOCK_SIZE*sizeof(char));
			nanosleep(&production_time,NULL);

			if(write(write_descriptor,&buffer,sizeof(char)*BLOCK_SIZE) == -1)
			{
				perror("Error while writing bytes");
				exit(EXIT_FAILURE);
			}
		}
	}

}


void connector(int read_descriptor, struct arguments* args)
{
	int sockfd, new_fd;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		exit(EXIT_FAILURE);
	}

	registering(sockfd,args->host,args->port);

	if (listen(sockfd, BACKLOG) == -1)
	{
		perror("listen");
		exit(EXIT_FAILURE);
	}

	while(1)
	{
		printf("Making new connection\n");
		new_fd = connecting(sockfd);

		if(new_fd == -1)
		{
			perror("Connection failed\n");
			exit(EXIT_FAILURE);
		}

		warehouseman(new_fd,read_descriptor);

		if( shutdown(new_fd,SHUT_RDWR) )
		{
			perror("");
			exit(2);
		}

	}

}

void warehouseman(int socket, int read_descriptor)
{
	char buffer[PORTION];
	int current_size;
	struct timespec delay = { .tv_sec = 0, .tv_nsec = 100 };
	char chunks[3][PACKET];
	char small_packet[1024];
	while(1)
	{
		if ( ioctl(read_descriptor, FIONREAD, &current_size) == -1 )
		{
			perror("Error in ioctl");
			exit(EXIT_FAILURE);
		}
		if ( current_size < PORTION )
		{
			nanosleep(&delay, NULL);
			continue;
		}
		break;
	}

	int bytes;
	bytes = read(read_descriptor,&buffer,sizeof(char)*PORTION);
	if(bytes == -1)
	{
		perror("Error while reading bytes");
		exit(EXIT_FAILURE);
	} else{
		printf("\nwarehouseman read: %d bytes\n",bytes);
	}

	for(int i=0; i<3;i++)
	{
		strncpy(chunks[i],buffer+(i*PACKET),PACKET);
	}
	strncpy(small_packet,buffer+PACKET+PACKET+PACKET,1024);

	printf("%s\n",buffer);


	for(int i=0; i<3;i++)
	{
		if(write(socket,chunks[i],sizeof(char)*PACKET) == -1)
		{
			perror("Error while writing bytes to socket");
			exit(EXIT_FAILURE);
		}
	}
	if(write(socket,small_packet,sizeof(char)*1024) == -1)
	{
		perror("Error while writing bytes to socket");
		exit(EXIT_FAILURE);
	}

}




void registering( int sockfd, char* Host, in_port_t Port )
{
	struct sockaddr_in my_address;
	my_address.sin_family = AF_INET;
	my_address.sin_port = htons(Port);

	int R = inet_aton(Host,&my_address.sin_addr);
	if( ! R ) {
		fprintf(stderr,"niepoprawny adres: %s\n",Host);
		exit(1);
	}

	if( bind(sockfd,(struct sockaddr *)&my_address,sizeof(my_address)) )
	{
		perror("Bind socket");
		exit(EXIT_FAILURE);
	}
}


void handler( int sig, siginfo_t * info, void * data )
{
	if ( info->si_signo == SIGPIPE )
	{
		printf("\nClient disconnected\n");
	}
}
void disconnect_handler( void )
{
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = handler;
	if( sigaction(SIGPIPE,&sa,NULL)==-1 )
	{
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
}

int connecting( int sockfd )
{
	struct sockaddr_in peer;
	socklen_t addr_len = sizeof(peer);

	int new_socket = accept(sockfd,(struct sockaddr *)&peer,&addr_len);
	if( new_socket == -1 ) {
		perror("");
	} else {
		printf("Connected with client\n");
	}
	return new_socket;
}


void read_input(int argc, char* argv[],struct arguments* args)
{
	if(argc < 3 || argc > 4)
	{
		fprintf(stderr, "Incorrect number of parameters\n");
		fprintf(stderr, "Usage: %s -p <float> [<addr:>]port \n", argv[0]);
		exit(EXIT_FAILURE);
	}

	opterr = 0;
	int c;
	while ((c = getopt (argc, argv, "p:")) != -1) {
		switch ( c ) {
			case 'p':{
				args->rate = parse_rate();
				break;
			}
			case '?':
				if ( optopt == 'p' )
				{
					fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				}
				else{
					fprintf(stderr, "Unknown option\n");
					fprintf(stderr, "Usage: %s -p <float> [<addr:>]port \n", argv[0]);
				}
				exit(EXIT_FAILURE);
			default:
				fprintf(stderr, "Unknown option\n");
				fprintf(stderr, "Usage: %s -p <float> [<addr:>]port \n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}


	if(optind != argc-1)
	{
		fprintf(stderr, "Expected argument after options\n");
		exit(EXIT_FAILURE);
	}
	char* location = argv[optind];
	parse_location(location, args);
}


float parse_rate(void)
{
	errno=0;
	char* endptr = NULL;
	float rate = strtof(optarg, &endptr);
	if( errno != 0 || *endptr != '\0')
	{
		perror("Incorrect parameter in -p option \n");
		exit(EXIT_FAILURE);
	}
	return rate;
}

void parse_location(char* location, struct arguments* args)
{
	printf("location: %s\n",location);
	errno=0;
	char* endptr = NULL;
	unsigned int port = strtoul(location, &endptr,0);
	if( errno != 0 )
	{
		perror("Failure in parsing location\n");
		exit(EXIT_FAILURE);
	}
	if( *endptr != '\0' )
	{
		char* check = strchr(location,':');
		if(check == NULL)
		{
			fprintf(stderr, "Incorrect location\n");
			exit(EXIT_FAILURE);
		}
		endptr = NULL;
		char* first_part = strtok(location, ":");
		errno = 0;
		char* second_part = strtok(NULL, ",");
		port = strtol(second_part, &endptr, 0);
		if( errno != 0 || *endptr != '\0')
		{
			perror("Incorrect location port\n");
			exit(EXIT_FAILURE);
		}
		args->host = first_part;

	} else{
		args->host = "127.0.0.1";
	}
	args->port = port;
}