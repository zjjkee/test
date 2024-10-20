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
#include <fcntl.h>
#include <pthread.h>
#include <fcntl.h>
#include <time.h>

#define BUFFER_SIZE 1024 // BUFFER_SIZE as per required for each segment 
#define WINDOW_SIZE 50
#define TIMEOUT_SEC 1 // timeout in seconds
#define TIMEOUT_USEC 0 // timeout in microseconds


//struct for data packets
struct packet {
	int sequence_no;
	int packet_size;
	char data[BUFFER_SIZE];
};

// Function to parse command-line arguments
void parse_arguments(int argc, char *argv[], int *port) {
    int opt;
    int use_getopt = 0; // Flag to check if getopt was used

    // Parse command-line arguments, option "p:" means -p should be followed by a value (port number)
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        use_getopt = 1; // Mark that -p option was used
        switch (opt) {
            case 'p': // If -p option is provided, read the port number
                *port = atoi(optarg); // Convert the string to an integer
                break;
            default: // If an unsupported option is provided
                fprintf(stderr, "Usage: %s -p <recv port> OR %s <recv port>\n", argv[0], argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    
    // If -p option is not used and a direct port number is provided as argument
    if (!use_getopt && argc == 2) {
        *port = atoi(argv[1]); // Directly use the provided argument as the port number
    }
    
    // Check if a valid port number was provided
    if (*port <= 0) {
        fprintf(stderr, "Error: Port number not provided or invalid.\n");
        fprintf(stderr, "Usage: %s -p <recv port> OR %s <recv port>\n", argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }
}



// variables for socket
int socket_fd; // listen on socket_fd
struct addrinfo serv_addr, *serv_info, *ptr; // server's address information
struct sockaddr_storage cli_addr; // client's address information
socklen_t cli_addr_len = sizeof (struct sockaddr_storage);
char ip_addr[INET6_ADDRSTRLEN];
int rv;

// variables for video file
int no_of_bytes = 0;
int out;
int file_size;
int remaining = 0;
int received = 0;

// variables for transferring data paackets and acks
int no_of_packets = WINDOW_SIZE; // window size 
struct packet temp_packet;
struct packet packets[WINDOW_SIZE];
int no_of_acks;
int acks[WINDOW_SIZE];
int temp_ack;


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


//  receive packets (with Timeouts)

int receive_with_timeout(int sockfd, struct packet *temp_packet, struct sockaddr_storage *cli_addr, socklen_t *cli_addr_len) {
    fd_set readfds;
    struct timeval timeout;
    
    // Initialize the file descriptor set
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    
    // Set timeout
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = TIMEOUT_USEC;

    // Wait for data to be available on the socket, with timeout
    int rv = select(sockfd + 1, &readfds, NULL, NULL, &timeout);

    if (rv == -1) {
        perror("UDP Server: select");
        exit(1);
    } else if (rv == 0) {
        // Timeout occurred, no data available
        return 0; // Indicates a timeout
    } else {
        // Data is available, proceed with recvfrom()
        if((no_of_bytes = recvfrom(sockfd, temp_packet, sizeof(struct packet), 0, (struct sockaddr *)cli_addr, cli_addr_len)) < 0) {
            perror("UDP Server: recvfrom");
            exit(1);
        }
        return 1; // Indicates successful reception
    }
}
void* receivePackets(void *vargp) {

    for (int i = 0; i < no_of_packets; i++) {
    RECEIVE:
        // Call the new function to receive with a timeout
        int result;
		result = receive_with_timeout(socket_fd, &temp_packet, &cli_addr, &cli_addr_len);
        
        if (result == 0) {
            // Timeout occurred, re-send the request or handle timeout
            printf("Timeout occurred. Retrying...\n");
            goto RECEIVE; // Retry receiving the packet
        }

        // Handle received packets and duplicate checks (existing logic)
        if (packets[temp_packet.sequence_no].packet_size != 0) { 
            packets[temp_packet.sequence_no] = temp_packet;
            temp_ack = temp_packet.sequence_no;
            acks[temp_ack] = 1;

            // Send ACK for the received packet
            if(sendto(socket_fd, &temp_ack, sizeof(int), 0, (struct sockaddr *)&cli_addr, cli_addr_len) < 0){
                perror("UDP Server: sendto");
                exit(1);
            }
            printf("Duplicate Ack Sent:%d\n", temp_ack);

            goto RECEIVE; // Continue receiving packets
        }

        // Handle last packet
        if (temp_packet.packet_size == -1) {
            printf("Last packet received\n");
            no_of_packets = temp_packet.sequence_no + 1;
        }

        // Process new packets
        if (no_of_bytes > 0) {
            printf("Packet Received: %d\n", temp_packet.sequence_no);
            packets[temp_packet.sequence_no] = temp_packet;
        }
    }
    return NULL;
}


// main function
int main(int argc, char *argv[]) {

	int port;
	parse_arguments(argc, argv, &port);
    if (port < 18000 || port > 18200) {
        fprintf(stderr, "Error: Port number must be between 18000 and 18200.\n");
        exit(EXIT_FAILURE);  // Exit the program with a failure code
    }
	char port_str[6]; // enough to store a 5 digitals port
	sprintf(port_str, "%d", port); // transfer int port to string

	memset(&serv_addr, 0, sizeof serv_addr); // ensure the struct is empty
	serv_addr.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	serv_addr.ai_socktype = SOCK_DGRAM; //UDP socket datagrams
	serv_addr.ai_flags = AI_PASSIVE; // fill in my IP

	if ((rv = getaddrinfo(NULL, port_str, &serv_addr, &serv_info)) != 0) {
		fprintf(stderr, "UDP Server: getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results in the linked list and bind to the first we can
	for(ptr = serv_info; ptr != NULL; ptr = ptr->ai_next) {
		if ((socket_fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) == -1) {
			perror("UDP Server: socket");
			continue;
		}

		// bind socket
		if (bind(socket_fd, ptr->ai_addr, ptr->ai_addrlen) == -1) {
			close(socket_fd);
			perror("UDP Server: bind");
			continue;
		}

		break;
	}

	if (ptr == NULL) {
		fprintf(stderr, "UDP Server: Failed to bind socket\n");
		return 2;
	}

	freeaddrinfo(serv_info); // all done with this structure

	printf("UDP Server: Waiting to recieve datagrams...\n");
    
	pthread_t thread_id; // create thread ID
	
	// time delay variables
	struct timespec time1, time2;
	time1.tv_sec = 0;
	time1.tv_nsec = 30000000L;  // 0.03 seconds

	FILE * out = fopen("output_video.mp4","wb"); // open the video file in write mode		
	
	// receiving the size of the video file from the client
	if ((no_of_bytes = recvfrom(socket_fd, &file_size, sizeof(off_t), 0, (struct sockaddr *)&cli_addr, &cli_addr_len)) < 0) {
		perror("UDP Server: recvfrom");
		exit(1);
	}
	printf("Size of Video File to be received: %d bytes\n", file_size);

	no_of_bytes = 1;
	remaining = file_size;

	while (remaining > 0 || (no_of_packets == WINDOW_SIZE)) {

		// reinitialize the arrays
		
		memset(packets, 0, sizeof(packets));
        	for (int i = 0; i < WINDOW_SIZE; i++) {
        		packets[i].packet_size = 0; 
        	}

        	for (int i = 0; i < WINDOW_SIZE; i++) {
        		acks[i] = 0; 
        	}
               
        	// server starts receiving packets i.e thread execution starts
		pthread_create(&thread_id, NULL, receivePackets, NULL);

        	// wait for packets to be received i.e the code sleeps for 0.03 seconds
        	nanosleep(&time1, &time2);

		no_of_acks = 0;

		// send acks for the packets received only
		RESEND_ACK:
		for (int i = 0; i < no_of_packets; i++) {
			temp_ack = packets[i].sequence_no;
			// if the ack has not been sent before
			if (acks[temp_ack] != 1) {
				// create acks for the packets received ONLY
				if (packets[i].packet_size != 0) {
					acks[temp_ack] = 1;

					// send acks to the client
					if(sendto(socket_fd, &temp_ack, sizeof(int), 0, (struct sockaddr *)&cli_addr, cli_addr_len) > 0) {
						no_of_acks++;
						printf("Ack sent: %d\n", temp_ack);
					}
				}
			}
		}

		// stop n wait
		// wait for acks to be sent and processed by the client
		nanosleep(&time1, &time2);
		nanosleep(&time1, &time2);

		// if all packets were not received
		if (no_of_acks < no_of_packets) {
			goto RESEND_ACK;
		}
                
		// 5 packets have been received i.e. the thread executes successfully
		pthread_join(thread_id, NULL);
                 
		// write packets to output file
		for (int i = 0; i < no_of_packets; i++) {
			// data is present in the packets and its not the last packet
			if (packets[i].packet_size != 0 && packets[i].packet_size != -1) {
				printf("Writing packet: %d\n", packets[i].sequence_no);
				fwrite(packets[i].data, 1, packets[i].packet_size, out);
				remaining = remaining - packets[i].packet_size;
				received = received + packets[i].packet_size;
			}
		}

		printf("Received data: %d bytes\nRemaining data: %d bytes\n", received, remaining);
		
		// repeat process for the next 5 packets
	}
	
	printf("\nUDP Server: Recieved video file from client %s\n", inet_ntop(cli_addr.ss_family, get_in_addr((struct sockaddr *)&cli_addr), ip_addr, sizeof ip_addr));
	printf("File transfer completed successfully!\n");
    	close(socket_fd); // close server socket
    	return 0;
}
