#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>


#define BUFFER_SIZE 1024 // BUFFER_SIZE as per required for each segment
#define WINDOW_SIZE 50


//struct for data packets
struct packet {
	int sequence_no;
	int packet_size;
	char data[BUFFER_SIZE];
};



// variables for socket
int socket_fd;
struct addrinfo serv_addr, *serv_info, *ptr; // server's address information
struct sockaddr_storage server_addr;
socklen_t server_addr_len = sizeof (struct sockaddr_storage);
int rv;

// variables for video file
int data;
int no_of_bytes;
int in;
struct stat file_stat;
int fd;
off_t file_size;

// variables for transferring data paackets and acks
struct packet packets[WINDOW_SIZE]; // window size is 5
int temp_seq_no = 1;
int no_of_acks;
int temp_ack;
int acks[WINDOW_SIZE];
int no_of_packets = WINDOW_SIZE;

// parse the command line
void parse_arguments(int argc, char *argv[], char **host, int *port, char **dir, char **file) {
    int opt;
    while ((opt = getopt(argc, argv, "r:f:")) != -1) {
        switch (opt) {
            case 'r':
                *host = strtok(optarg, ":");
                *port = atoi(strtok(NULL, ":"));
                break;
            case 'f':
                *dir = strtok(optarg, "/");
                *file = strtok(NULL, "/");
                break;
            default:
                fprintf(stderr, "Usage: %s -r <recv host>:<recv port> -f <subdir>/<filename>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (*host == NULL || *port == 0 || *dir == NULL || *file == NULL) {
        fprintf(stderr, "Missing required arguments.\n");
        exit(EXIT_FAILURE);
    }
}


// thread to receive acks (runs in parallel with the main program)

void* receiveAcks(void* vargp) {

	// receive 5 acks 
	for (int i = 0; i < no_of_packets; i++) {
		
	    RECEIVE:
		if((no_of_bytes = recvfrom(socket_fd, &temp_ack, sizeof(int), 0, (struct sockaddr*) &server_addr, &server_addr_len)) < 0) {
			perror("UDP Client: recvfrom");
			exit(1);
		} 
		
		// in case of duplicate ack
		if (acks[temp_ack] == 1) {
			// receive ack again until a unique ack is received
			goto RECEIVE; 
		}

		// in case of unique ack
		printf("Ack Received: %d\n", temp_ack);
		// reorder acks according to the packet's sequence number
		// make the value 1 in the acks[] array, where array position is the value of ack received (i.e. the sequence number of the packet acknowledged by the server)
		acks[temp_ack] = 1;
		no_of_acks++;

	}
    	return NULL;
}


// main function
int main(int argc, char* argv[]) {
    char *host = NULL, *dir = NULL, *file = NULL;
    int port;
    parse_arguments(argc, argv, &host, &port, &dir, &file);
    if (port < 18000 || port > 18200) {
        fprintf(stderr, "Error: Port number must be between 18000 and 18200.\n");
        exit(EXIT_FAILURE);  // Exit the program with a failure code
    }
	char port_str[6]; // enough to store a 5 digitals port
	sprintf(port_str, "%d", port); // transfer int port to string
	char file_path[150];
    snprintf(file_path, sizeof(file_path), "%s/%s", dir, file);



	memset(&serv_addr, 0, sizeof serv_addr); // ensure the struct is empty
	serv_addr.ai_family = AF_UNSPEC;
	serv_addr.ai_socktype = SOCK_DGRAM; // UDP socket datagrams

	if ((rv = getaddrinfo(host, port_str, &serv_addr, &serv_info)) != 0) {
		fprintf(stderr, "UDP Client: getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and make a socket
	for(ptr = serv_info; ptr != NULL; ptr = ptr->ai_next) {
		if ((socket_fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) == -1) {
			perror("UDP Client: socket");
			continue;
		}
		
		break;
	}

	if (ptr == NULL) {
		fprintf(stderr, "UDP Client: Failed to create socket\n");
		return 2;
	}

	pthread_t thread_id; // create thread ID
        
    	// time delay variables
	struct timespec time1, time2;
	time1.tv_sec = 0;
	time1.tv_nsec = 300000000L;

	FILE * in = fopen(file_path,"rb"); // open the video file in read mode
	
	// if the file is not readable
	if (in == NULL) {
		perror("Error in opening the video file.\n");
		return 0;
	}
	
	// size of the video file
	fd = fileno(in);
	fstat(fd, &file_stat);
	file_size = file_stat.st_size;
	printf("Size of Video File: %d bytes\n",(int) file_size);

	// sending the size of the video file to the server
	FILESIZE:
	if(sendto(socket_fd, &file_size, sizeof(off_t), 0, ptr->ai_addr, ptr->ai_addrlen) < 0) {
	// resend the file size
		goto FILESIZE;
	}

	data = 1;

	// while data left to be read
	while (data > 0) {

		// make packets
		temp_seq_no = 0;
		for (int i = 0; i < no_of_packets; i++) {
            		// data
			data = fread(packets[i].data, 1, BUFFER_SIZE, in);
            		// sequence number
			packets[i].sequence_no = temp_seq_no;
          		// packet size
			packets[i].packet_size = data;
			temp_seq_no++;

			// last packet to be sent i.e. eof
           		if (data == 0){ 
                      		printf("End of file reached.\n");
                     		// Setting a condition for last packet
                      		packets[i].packet_size = -1; 
                      		// Decrementing the remaining loops 
                      		no_of_packets = i + 1; 
                      		break; 
            		}
		}

		// send 5 packets 
		for (int i = 0; i < no_of_packets; i++) {
			printf("Sending packet %d\n", packets[i].sequence_no);
			if(sendto(socket_fd, &packets[i], sizeof(struct packet), 0, ptr->ai_addr, ptr->ai_addrlen) < 0) {
				perror("UDP Client: sendto");
				exit(1);
			}            
		}

        	// reinitialize the array
        	for (int i = 0; i < no_of_packets; i++) { 
        		acks[i] = 0;
        	}

		no_of_acks = 0;

		// client starts receiving acks i.e. the thread execution starts
		pthread_create(&thread_id, NULL, receiveAcks, NULL);
                   
		// wait for acks to be received i.e. the code sleeps for 0.03 seconds
		nanosleep(&time1, &time2);

		// selective repeat 
		
		// send those packets ONLY whose acks have not been received
		RESEND:
		for (int i = 0; i < no_of_packets; i++) {

			// if the ack has not been received
			if (acks[i] == 0) {

				// sending that packet whose ack was not received 
                		printf("Sending missing packet: %d\n",packets[i].sequence_no);
				if(sendto(socket_fd, &packets[i], sizeof(struct packet), 0, ptr->ai_addr, ptr->ai_addrlen) < 0) {
					perror("UDP Client: sendto");
					exit(1);
				}
			}
		}

		// resend the packets again whose acks have not been received
		if (no_of_acks != no_of_packets) {
            	// wait for acks of the packets that were resent
            		nanosleep(&time1, &time2);
			goto RESEND;
		}

		// 5 acks have been received i.e. the thread executes successfully
		pthread_join(thread_id, NULL);

		// repeat process until the eof is not reached 
	}
	freeaddrinfo(serv_info); // all done with this structure	
    printf("[completed]\n");
	
	close(socket_fd); // close the socket
	return 0;
}
