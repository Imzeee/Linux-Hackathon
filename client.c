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
#include <netdb.h>

#define PORTION 13*1024


struct arguments{
	int capacity;
	float consumption_rate;
	float degradation_rate;
	char* host;
	unsigned int port;
};

void read_input(int argc, char* argv[],struct arguments* args);
float parse_rate(void);
int parse_capacity(void);
void parse_location(char* location, struct arguments* args);

void receive_data(int socket);


int main(int argc, char *argv[])
{
	struct arguments args = {};
	read_input(argc,argv,&args);
	printf("consumption_rate: %f\n",args.consumption_rate);
	printf("degradation_rate: %f\n",args.degradation_rate);
	printf("capacity: %d\n",args.capacity);
	printf("address: %s\n",args.host);
	printf("port: %u\n",args.port);

	int socket_fd;
	struct sockaddr_in their_addr;
	if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}
	their_addr.sin_family = AF_INET;
	their_addr.sin_port = htons(args.port);
	int R = inet_aton(args.host,&their_addr.sin_addr);
	if( ! R ) {
		fprintf(stderr,"niepoprawny adres: %s\n",args.host);
		exit(1);
	}

	if (connect(socket_fd, (struct sockaddr *)&their_addr, sizeof(struct sockaddr)) == -1) {
		perror("connect");
		exit(1);
	}

	receive_data(socket_fd);

	if( shutdown(socket_fd,SHUT_RDWR) ) {
		perror("");
		exit(2);
	}

	return 0;
}


void receive_data(int socket)
{
	char buffer[PORTION];
	if(read(socket,buffer,sizeof(char)*PORTION) == -1)
	{
		perror("Error while reading bytes from producer\n");
		exit(EXIT_FAILURE);
	}
	printf("\n%s\n",buffer);
}

void read_input(int argc, char* argv[],struct arguments* args)
{
	if(argc < 5 || argc > 8)
	{
		fprintf(stderr, "Incorrect number of parameters\n");
		fprintf(stderr, "Usage: %s -c <int> -p <float> -d <float> [<addr:>]port \n", argv[0]);
		exit(EXIT_FAILURE);
	}

	opterr = 0;
	int c;
	while ((c = getopt (argc, argv, "c:p:d:")) != -1) {
		switch ( c ) {
			case 'p':{
				args->consumption_rate = parse_rate();
				break;
			}
			case 'c':{
				args->capacity = parse_capacity();
				break;
			}
			case 'd':{
				args->degradation_rate = parse_rate();
				break;
			}

			case '?':
				if ( optopt == 'c' || optopt == 'p' || optopt == 'd' )
				{
					fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				}
				else{
					fprintf(stderr, "Unknown option\n");
					fprintf(stderr, "Usage: %s -c <int> -p <float> -d <float> [<addr:>]port \n", argv[0]);
				}
				exit(EXIT_FAILURE);
			default:
				fprintf(stderr, "Unknown option\n");
				fprintf(stderr, "Usage: %s -c <int> -p <float> -d <float> [<addr:>]port \n", argv[0]);
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
		fprintf(stderr, "Incorrect parameter\n");
		exit(EXIT_FAILURE);
	}
	return rate;
}

int parse_capacity(void)
{
	errno=0;
	char* endptr = NULL;
	int capacity = (int)strtol(optarg, &endptr,0);
	if( errno != 0 || *endptr != '\0')
	{
		fprintf(stderr, "Incorrect parameter\n");
		exit(EXIT_FAILURE);
	}
	return capacity;
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
			fprintf(stderr, "Incorrect parameter\n");
			exit(EXIT_FAILURE);
		}
		args->host = first_part;

	} else{
		args->host = "127.0.0.1";
	}
	args->port = port;
}