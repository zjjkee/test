# Define compiler and compiler flags
CC = gcc
CFLAGS = -Wall -g

# Define target executable names
SENDFILE = send
RECVFILE = recv

# Define source files
SENDFILE_SRC = udp_client.c
RECVFILE_SRC = udp_server.c

# Default target: Compile both sendfile and recvfile
all: $(SENDFILE) $(RECVFILE)

# Rule to compile sendfile
$(SENDFILE): $(SENDFILE_SRC)
	$(CC) $(CFLAGS) -o $(SENDFILE) $(SENDFILE_SRC)

# Rule to compile recvfile
$(RECVFILE): $(RECVFILE_SRC)
	$(CC) $(CFLAGS) -o $(RECVFILE) $(RECVFILE_SRC)

# Clean up compiled files
clean:
	rm -f $(SENDFILE) $(RECVFILE)

# Phony targets to avoid conflicts with files of the same name
.PHONY: all clean
