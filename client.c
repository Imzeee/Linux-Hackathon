#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORTION 13*1024
#define PACKET 4*1024
#define CONSUMPTION_UNIT 4435
#define DEGRADATION_UNIT 819
#define BIG_BLOCKS 30*1024


struct arguments{
	int capacity;
	float consumption_rate;
	float degradation_rate;
	char* host;
	unsigned int port;
};

long int Processed = 0;
long int Downgraded = 0;
char Logs_Buffer[10000];

void read_arguments(int argc, char* argv[],struct arguments* args);
float parse_rate(void);
int parse_capacity(void);
void parse_location(char* location, struct arguments* args);

int connecting(struct arguments* args);

void receive_packet_of_data(int socket,struct arguments* args, struct timespec* first_packet);
float processing(int bytes,float consumption_rate);
void downgrading(float processing_time, float degradation_rate);
int is_free_space(long int capacity); // 1->true

void start_receiving(struct arguments* args);

void generate_report(int socket);
void generate_report_a(long int delta);
void generate_report_b(long int delta);
void print_report(void);
long int calculate_delta(struct timespec* start,struct timespec* finish);



int main(int argc, char *argv[])
{
	struct arguments args = {};
	read_arguments(argc, argv, &args);
	start_receiving(&args);
	return 0;
}


void start_receiving(struct arguments* args) // start receiving packets from server
{
	long int warehouse_capacity = (args->capacity) * BIG_BLOCKS;
	int socket_fd;
	struct timespec first_packet,shut_down;
	errno=0;
	while (1)
	{
		if ( is_free_space(warehouse_capacity))
		{
			printf("(There is free space in warehouse)\n");
			socket_fd = connecting(args);
			generate_report(socket_fd);
			receive_packet_of_data(socket_fd, args,&first_packet);

			if (shutdown(socket_fd, SHUT_RDWR) == -1)
			{
				perror("Shutdown error\n");
				exit(EXIT_FAILURE);
			}

			if(clock_gettime(CLOCK_MONOTONIC , &shut_down) == -1)
			{
				perror("Error in clock_gettime\n");
				exit(EXIT_FAILURE);
			}

			long int delta = calculate_delta(&first_packet,&shut_down);
			generate_report_b(delta);
			printf("(Transmission ended)\n");
		} else {
			printf("(There is no space in warehouse)\n");
			print_report();
			return;
		}

	}
}

int connecting(struct arguments* args) // connect to server
{
	int socket_fd;
	struct sockaddr_in their_addr;
	errno=0;
	if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		exit(EXIT_FAILURE);
	}
	their_addr.sin_family = AF_INET;
	their_addr.sin_port = htons(args->port);
	int check = inet_aton(args->host,&their_addr.sin_addr);
	if(!check)
	{
		fprintf(stderr,"Incorrect address: %s\n",args->host);
		exit(EXIT_FAILURE);
	}

	if (connect(socket_fd, (struct sockaddr *)&their_addr, sizeof(struct sockaddr)) == -1)
	{
		perror("connect");
		exit(EXIT_FAILURE);
	}
	return socket_fd;
}


void receive_packet_of_data(int socket,struct arguments* args, struct timespec* first_packet)
{
	char buffer[PACKET];
	errno=0;
	struct timespec start,finish;
	if(clock_gettime(CLOCK_MONOTONIC , &start) == -1)
	{
		perror("Error in clock_gettime\n");
		exit(EXIT_FAILURE);
	}

	printf("(Waiting for packet...)\n");
	int bytes_read = read(socket,buffer,sizeof(char)*PACKET);
	if(bytes_read == -1)
	{
		perror("Error while reading bytes from producer\n");
		exit(EXIT_FAILURE);
	}

	if(clock_gettime(CLOCK_MONOTONIC , &finish) == -1)
	{
		perror("Error in clock_gettime\n");
		exit(EXIT_FAILURE);
	}

	long int delta = calculate_delta(&start,&finish);
	generate_report_a(delta);
	first_packet->tv_sec = finish.tv_sec;
	first_packet->tv_nsec = finish.tv_nsec;

	printf("(Bytes read: %d)\n",bytes_read);
	processing(bytes_read,args->consumption_rate);
	Processed += bytes_read;

	for(int i = 0; i < 3; i++)
	{
		bytes_read = read(socket,buffer,sizeof(char)*PACKET);
		if(bytes_read == -1)
		{
			perror("Error while reading bytes from producer\n");
			exit(EXIT_FAILURE);
		}

		printf("(Bytes read: %d)\n",bytes_read);

		float processing_time = processing(bytes_read,args->consumption_rate);
		Processed += bytes_read;
		downgrading(processing_time,args->degradation_rate);

	}
	printf("(Processed: %ld)\n",Processed);
	printf("(Downgraded: %ld)\n",Downgraded);

}

float processing(int bytes,float consumption_rate)
{
	float processing_time = (float)bytes/(consumption_rate*CONSUMPTION_UNIT); // accuracy to micro seconds
	int sec = (int)processing_time;
	int micro = (int)((processing_time - (float)sec)*1000000);
	int nano = micro*1000;
	struct timespec processing = { .tv_sec = sec, .tv_nsec = nano };
	//printf("Processing time: %d %d\n",sec,nano);
	nanosleep(&processing,NULL);
	return processing_time;

}

void downgrading(float processing_time, float degradation_rate)
{
	long int downgrade_bytes = (long int)(processing_time * degradation_rate * DEGRADATION_UNIT);
	Downgraded += downgrade_bytes;
}


int is_free_space(long int capacity)
{
	long int occupied_space = Processed - Downgraded;
	long int free_space = capacity - occupied_space;
	if(free_space >= PORTION) return 1;
	return 0;
}

void read_arguments(int argc, char* argv[],struct arguments* args)
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

void generate_report(int socket)
{
	static int counter = 1;
	struct sockaddr_in my_addr;
	socklen_t addr_len = sizeof(my_addr);
	errno=0;

	if(getsockname(socket,(struct sockaddr *)&my_addr,&addr_len) == -1)
	{
		perror("getsockname error\n");
		exit(EXIT_FAILURE);
	}

	sprintf(Logs_Buffer+strlen(Logs_Buffer),"\n---BLOCK::%d---\n",counter);
	sprintf(Logs_Buffer+strlen(Logs_Buffer),"Consument: PID(%d) %s (port %d)\n",getpid(),
			inet_ntoa(my_addr.sin_addr),ntohs(my_addr.sin_port));

	counter++;
}

void generate_report_a(long int delta)
{
	long int seconds = delta/1000000000;
	long int nanoseconds = delta - (seconds*1000000000);
	sprintf(Logs_Buffer+strlen(Logs_Buffer),"Delay(connection-->firs_packet): %ld sec %ld nano\n",
			seconds,nanoseconds);
}

void generate_report_b(long int delta)
{
	long int seconds = delta/1000000000;
	long int nanoseconds = delta - (seconds*1000000000);
	sprintf(Logs_Buffer+strlen(Logs_Buffer),"Delta(first_package-->disconnection) %ld sec %ld nano\n",
			seconds,nanoseconds);
}

void print_report(void)
{
	struct timespec tp;
	errno=0;
	if(clock_gettime(CLOCK_REALTIME , &tp) == -1)
	{
		perror("Error in clock_gettime\n");
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"\n-----END REPORT-----\n");
	fprintf(stderr,"TS: %ld-sec %ld-nano\n",tp.tv_sec,tp.tv_nsec);
	fprintf(stderr,"%s\n",Logs_Buffer);
}

long int calculate_delta(struct timespec* start,struct timespec* finish)
{
	long int start_nanoseconds = (start->tv_sec * 1000000000) + start->tv_nsec;
	long int finish_nanoseconds = (finish->tv_sec * 1000000000) + finish->tv_nsec;
	long int delta = finish_nanoseconds - start_nanoseconds;
	return delta;
}
