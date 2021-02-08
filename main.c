#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <limits.h>


#define UNIT 2662
#define BLOCK_SIZE 640
#define PORTION 13*1024
#define PACKET 4*1024
#define KB 1024

#define MAX_CLIENTS 200
#define BACKLOG 32
#define TIMEOUT 3 * 60 * 1000 //timeout on poll in miliseconds

struct arguments{
	float rate;
	char* host;
	unsigned int port;
};

int Current_clients = 0;
int Pipe_descriptors[2]; //0 - READ, 1 - WRITE, made global to be able to generate warehouse report
int Generated = 0;
int Spend = 0;


void read_arguments(int argc, char* argv[],struct arguments* args);
float parse_rate(void);
void parse_location(char* location, struct arguments* args);

void create_warehouse(struct arguments* args);
void worker(int write_descriptor, struct arguments* args);
void connector(int read_descriptor, struct arguments* args);
int warehouseman(int socket, int read_descriptor);

void registering( int sockfd, char * Host, in_port_t Port );

void handler( int sig, siginfo_t * info, void * data );
void disconnect_handler( void );

void set_timer();
void handle_alarm( int sig, siginfo_t * info, void * data );
void alarm_sig( void );

void disconnect_report(struct sockaddr_in* peer, int wasted_bytes);



/*
	Przykladowe parametry dla ktorych uruchamialem programy ktore sie wykonaly :) :

 	./server-p 3.0 8888
	Jednoczesnie w osobnych terminalach:
 	./client -c 1 -p 1 -d 1 8888
	./client -c 2 -p 2 -d 2 8888
	./client -c 3 -p 3 -d 1 8888
	./client -c 2 -p 1 -d 1 8888
 	./client -c 2 -p 2 -d 1 8888

 */


int main(int argc, char* argv[])
{
	struct arguments args = {};
	read_arguments(argc,argv,&args);
	create_warehouse(&args);
	return 0;
}



void create_warehouse(struct arguments* args)
{
	if(pipe(Pipe_descriptors) == -1)
	{
		perror("Pipe creation failure\n");
		exit(EXIT_FAILURE);
	}
	pid_t p = fork();
	switch (p) {
		case -1:
			perror("fork failure");
			exit(EXIT_FAILURE);
		case 0:
			if(close(Pipe_descriptors[0]) == -1)
			{
				perror("Close read pipe descriptor failure");
				exit(EXIT_FAILURE);
			}
			worker(Pipe_descriptors[1],args); // create worker
			break;
		default:
			if(close(Pipe_descriptors[1]) == -1)
			{
				perror("Close write pipe descriptor failure");
				exit(EXIT_FAILURE);
			}
			disconnect_handler(); // set signals handling only for parent
			alarm_sig();
			set_timer();

			connector(Pipe_descriptors[0],args); // create connector
	}
}

void worker(int write_descriptor, struct arguments* args) // produces bytes and stores them in warehouse
{
	char buffer[BLOCK_SIZE];
	float prod_time = BLOCK_SIZE/(args->rate*UNIT); // accuracy to micro seconds
	int sec = (int)prod_time;
	int micro = (int)((prod_time - (float)sec)*1000000);
	int nano = micro*1000;
	struct timespec production_time = { .tv_sec = sec, .tv_nsec = nano };
	//printf("%d %d\n",sec,nano);

	while(1)
	{
		for(int i=65; i<=122; i++)
		{
			if(i>90 && i < 97) continue;
			if(memset(buffer,(char)i,BLOCK_SIZE*sizeof(char)) == NULL)
			{
				perror("Memset error in worker\n");
				exit(EXIT_FAILURE);
			}
			if(nanosleep(&production_time,NULL) == -1)
			{
				perror("Sleep in worker error\n");
				exit(EXIT_FAILURE);
			}
			if(write(write_descriptor,&buffer,sizeof(char)*BLOCK_SIZE) == -1)
			{
				perror("Error while writing bytes (worker)");
				exit(EXIT_FAILURE);
			}
		}
	}

}


void connector(int read_descriptor, struct arguments* args) // handle connections
{
	struct pollfd clients_descriptors[MAX_CLIENTS];
	int sockfd, new_fd;
	memset(clients_descriptors, 0 , sizeof(clients_descriptors));
	int number_of_fds = 1;
	int status, current_size;
	struct sockaddr_in peer_addresses[MAX_CLIENTS];
	errno=0;

	sigset_t blockSet, prevMask;
	if(sigemptyset(&blockSet) == -1)
	{
		perror("sigemptyset error\n");
		exit(EXIT_FAILURE);
	}

	if(sigaddset(&blockSet, SIGALRM) == -1)
	{
		perror("sigaddset error\n");
		exit(EXIT_FAILURE);
	}

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		exit(EXIT_FAILURE);
	}

	registering(sockfd,args->host,args->port); // configure socket

	if (listen(sockfd, BACKLOG) == -1)
	{
		perror("listen");
		exit(EXIT_FAILURE);
	}

	clients_descriptors[0].fd = sockfd;
	clients_descriptors[0].events = POLLIN;

	while(1)
	{
		if (sigprocmask(SIG_BLOCK, &blockSet, &prevMask) == -1) // block poll being interrupted by SIGALRM (causes crash)
		{
			perror("sigprocmask error\n");
			exit(EXIT_FAILURE);
		}
		printf("Listening...\n");
		status = poll(clients_descriptors, number_of_fds, TIMEOUT);
		if (sigprocmask(SIG_SETMASK, &prevMask, NULL) == -1) // unblock SIGALRM
		{
			perror("sigprocmask2 error\n");
			exit(EXIT_FAILURE);
		}

		if (status < 0)
		{
			perror("Call to poll() failed\n");
			exit(EXIT_FAILURE);
		}
		if (status == 0)
		{
			printf("Poll() timeout\n");
			exit(EXIT_FAILURE);
		}

		current_size = number_of_fds;
		for(int i=0; i<current_size; i++)
		{
			if(clients_descriptors[i].revents == 0) continue;
			if(clients_descriptors[i].revents != POLLIN && clients_descriptors[i].revents != POLLOUT)
			{
				printf("Error in revents = %d\n", clients_descriptors[i].revents);
				exit(EXIT_FAILURE);
			}
			if(clients_descriptors[i].fd == sockfd )
			{
				while(1)
				{
					struct sockaddr_in peer;
					socklen_t addr_len = sizeof(peer);

					new_fd = accept(sockfd,(struct sockaddr *)&peer,&addr_len);
					if (new_fd < 0)
					{
						if (errno != EWOULDBLOCK)
						{
							perror("Accept failed\n");
							exit(EXIT_FAILURE);
						}
						break;
					}

					Current_clients++;
					printf("(New connection with client: %s (port %d))\n",
							inet_ntoa(peer.sin_addr),ntohs(peer.sin_port));
					clients_descriptors[number_of_fds].fd = new_fd;
					peer_addresses[number_of_fds] = peer;
					clients_descriptors[number_of_fds].events = POLLOUT;
					number_of_fds++;

				}
			} else{

				int wasted_bytes = 0;
				int sent_bytes = warehouseman(clients_descriptors[i].fd, read_descriptor);
				printf("(Bytes sent to client: %d)\n", sent_bytes);
				if(sent_bytes != PORTION) wasted_bytes = (PORTION - sent_bytes);
				Spend+=sent_bytes;

				if( shutdown(clients_descriptors[i].fd,SHUT_RDWR) == -1 ) // close connection after sending bytes
				{
					perror("Shutdown error");
					exit(EXIT_FAILURE);
				}

				Current_clients--;

				disconnect_report(&peer_addresses[i], wasted_bytes);

				clients_descriptors[i].fd = -1;
				number_of_fds--;
			}
		}

	}

}

int warehouseman(int socket, int read_descriptor) // waiting for portion and sending to client
{
	int sent_bytes = 0;
	errno=0;
	char buffer[PORTION];
	int current_occupied;
	struct timespec delay = { .tv_sec = 0, .tv_nsec = 100 };
	char chunks[3][PACKET];
	char small_packet[1024];
	while(1)
	{
		if ( ioctl(read_descriptor, FIONREAD, &current_occupied) == -1 )
		{
			perror("Error in ioctl");
			exit(EXIT_FAILURE);
		}
		if ( current_occupied < PORTION )
		{
			nanosleep(&delay, NULL);
			continue;
		}
		Generated += current_occupied;
		break;
	}

	int bytes;
	bytes = read(read_descriptor,&buffer,sizeof(char)*PORTION);
	if(bytes == -1)
	{
		perror("Error while reading bytes");
		exit(EXIT_FAILURE);
	}

	for(int i=0; i<3;i++)
	{
		if(strncpy(chunks[i],buffer+(i*PACKET),PACKET) == NULL)
		{
			perror("strncpy error\n");
			exit(EXIT_FAILURE);
		}
	}

	if(strncpy(small_packet,buffer+(3*PACKET),KB) == NULL)
	{
		perror("strncpy error\n");
		exit(EXIT_FAILURE);
	}

	int written;
	for(int i=0; i<3;i++)
	{
		written = write(socket,chunks[i],sizeof(char)*PACKET);
		if(written == -1)
		{
			printf("Sending bytes to client interrupted\n");
			return sent_bytes;
		}
		sent_bytes += written;
	}
	written = write(socket,small_packet,sizeof(char)*KB);
	if(written == -1)
	{
		printf("Sending bytes to client interrupted\n");
		return sent_bytes;
	}
	sent_bytes += written;

	return sent_bytes;

}


void registering( int sockfd, char* Host, in_port_t Port ) // socket configuration
{
	int on = 1;
	errno=0;
	int status = setsockopt(sockfd, SOL_SOCKET,  SO_REUSEADDR,(char *)&on, sizeof(on)); //Allow socket descriptor to be reusable
	if (status < 0)
	{
		perror("setsockopt failure\n");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	status = ioctl(sockfd, FIONBIO, (char *)&on); // Set socket to be nonblocking
	if (status < 0)
	{
		perror("ioctl failure (registering)\n");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in my_address;
	my_address.sin_family = AF_INET;
	my_address.sin_port = htons(Port);

	int check = inet_aton(Host,&my_address.sin_addr);
	if(!check)
	{
		fprintf(stderr,"Incorrect address: %s\n",Host);
		exit(EXIT_FAILURE);
	}

	if( bind(sockfd,(struct sockaddr *)&my_address,sizeof(my_address)) )
	{
		perror("Bind socket");
		exit(EXIT_FAILURE);
	}
}



void handler(int sig, siginfo_t* info, void* data)
{
	if ( info->si_signo == SIGPIPE )
	{
		printf("\nClient disconnected\n");
	}
}

void disconnect_handler(void)
{
	struct sigaction sa;
	errno=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = handler;
	if( sigaction(SIGPIPE,&sa,NULL)==-1 )
	{
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
}



void read_arguments(int argc, char* argv[],struct arguments* args)
{
	if(argc < 3 || argc > 4)
	{
		fprintf(stderr, "Incorrect number of parameters\n");
		fprintf(stderr, "Usage: %s -p <float> [<addr:>]port \n", argv[0]);
		exit(EXIT_FAILURE);
	}

	opterr = 0;
	errno=0;
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

void set_timer()
{
	errno=0;
	struct timeval it_value = {.tv_sec = 5, .tv_usec = 0};
	struct timeval it_interval = {.tv_sec = 5, .tv_usec = 0};
	struct itimerval new_value ={.it_value = it_value, .it_interval = it_interval};
	if(setitimer(ITIMER_REAL , &new_value ,NULL ) == -1)
	{
		perror("settimer");
		exit(EXIT_FAILURE);
	}
}

void handle_alarm( int sig, siginfo_t * info, void * data )
{
	struct timespec tp;
	errno=0;

	int time =  clock_gettime(CLOCK_REALTIME , &tp );
	if(time == -1)
	{
		perror("Error in clock_gettime\n");
		exit(EXIT_FAILURE);
	}

	int current_occupied;
	if ( ioctl(Pipe_descriptors[0], FIONREAD, &current_occupied) == -1 )
	{
		perror("Error in ioctl");
		exit(EXIT_FAILURE);
	}
	int pipe_capacity = 16*PIPE_BUF;
	float procent = (float)current_occupied/(float)pipe_capacity;
	procent*=100;

	if ( info->si_signo == SIGALRM )
	{
		fprintf(stderr,"\n-----------\nINTERVAL REPORT:\n");
		fprintf(stderr,"TS: %ld-sec %ld-nano\n",tp.tv_sec,tp.tv_nsec);
		fprintf(stderr,"Number of accepted clients: %d\n",Current_clients);
		fprintf(stderr,"Magazine: %dB/%dB (%d %c)\n",current_occupied,pipe_capacity,(int)procent,'%');
		fprintf(stderr,"Flow: %dB(generated - spend)\n",Generated-Spend);
		fprintf(stderr,"-----------\n");
	}
	Generated=0;
	Spend=0;
}
void alarm_sig( void )
{
	struct sigaction sa;
	errno=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = handle_alarm;
	if( sigaction(SIGALRM,&sa,NULL)==-1 )
	{
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
}

void disconnect_report(struct sockaddr_in* peer, int wasted)
{
	fprintf(stderr,"\n-----------\nDISCONNECT REPORT:\n");
	fprintf(stderr,"Connection closed with client: %s (port %d)\n",
			inet_ntoa(peer->sin_addr),ntohs(peer->sin_port));
	fprintf(stderr,"Bytes wasted: %d\n",wasted);
	fprintf(stderr,"-----------\n");
}