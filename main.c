#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

struct arguments{
	float rate;
	char* address;
	unsigned int port;
};


void read_input(int argc, char* argv[],struct arguments* args);
float parse_rate(void);
void parse_location(char* location, struct arguments* args);

void create_warehouse();

int main(int argc, char* argv[])
{
	struct arguments args = {};
	read_input(argc,argv,&args);
	printf("rate: %f\n",args.rate);
	printf("address: %s\n",args.address);
	printf("port: %u\n",args.port);

	create_warehouse();

	return 0;
}


void create_warehouse()
{

	pid_t p = fork();
	switch (p) {
		case -1:
			perror("");
			exit(2);
		case 0:
			printf("Hello from child\n");
			break;
		default:
			printf("Hello from parent\n");
	}
}


void read_input(int argc, char* argv[],struct arguments* args)
{
	if(argc != 4)
	{
		fprintf(stderr, "Incorrect number of parameters");
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
		perror("Incorrect parameter in -p option ");
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
		args->address = first_part;

	} else{
		args->address = "localhost";
	}
	args->port = port;
}