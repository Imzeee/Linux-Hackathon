#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/ioctl.h>

#define UNIT 2662
#define BLOCK_SIZE 640
#define PORTION 13*1024

struct arguments{
	float rate;
	char* address;
	unsigned int port;
};

void read_input(int argc, char* argv[],struct arguments* args);
float parse_rate(void);
void parse_location(char* location, struct arguments* args);

void create_warehouse(struct arguments* args);
void worker(int write_descriptor, struct arguments* args);
void connector(int read_descriptor, struct arguments* args);

int main(int argc, char* argv[])
{
	struct arguments args = {};
	read_input(argc,argv,&args);
	printf("rate: %f\n",args.rate);
	printf("address: %s\n",args.address);
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

	

	while(1)
	{
		for(int i=65; i<=122; i++)
		{
			if(i>90 && i < 97) continue;
			memset(buffer,(char)i,BLOCK_SIZE*sizeof(char));
			if(write(write_descriptor,&buffer,sizeof(char)*BLOCK_SIZE) == -1)
			{
				perror("Error while writing bytes");
				exit(EXIT_FAILURE);
			}
			//sleep(1);
		}
	}

}


void connector(int read_descriptor, struct arguments* args)
{
	char buffer[PORTION];
	for(int i=0; i<52;i++)
	{
		//sleep(2);
		if(read(read_descriptor,&buffer,sizeof(char)*PORTION) == -1)
		{
			perror("Error while reading bytes");
			exit(EXIT_FAILURE);
		}
		int size;
		ioctl(read_descriptor,FIONREAD,&size);
		printf("%s\n",buffer);
		printf("DUUUUUUUUUUUUUUUUUUUUUUUUPA - %d %d\n",i,size);
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