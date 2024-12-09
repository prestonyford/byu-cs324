#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>

#include "sockhelper.h"
#include "sbuf.h"

/* Recommended max object size */
#define MAX_OBJECT_SIZE 102400
#define BUF_SIZE 1024
#define NUM_THREADS 8
#define NUM_SLOTS 5

#define READ_REQUEST 1
#define SEND_REQUEST 2
#define READ_RESPONSE 3
#define SEND_RESPONSE 4

#define MAXEVENTS 64

struct request_info {
	int client_sfd;
	int server_sfd;
	int state;
	unsigned char buf[BUF_SIZE];
	int num_bytes_read_from_client;
	int num_bytes_to_write_to_server;
	int num_bytes_written_to_server;
	int num_bytes_read_from_server;
	int num_bytes_written_to_client;
};


static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";

int open_sfd(unsigned short);
void handle_new_clients(int, int);
void handle_client(struct request_info *, int);
int complete_request_received(char *);
void parse_request(char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("Missing port\n");
		exit(1);
	}
	unsigned short port = atoi(argv[1]);
	
	int efd;
	if ((efd = epoll_create1(0)) < 0) {
		perror("Error with epoll_create1");
		exit(EXIT_FAILURE);
	}

	int sfd = open_sfd(port);
	struct request_info *listener = malloc(sizeof(struct request_info));
	listener->client_sfd = sfd;
	
	struct epoll_event event;
	event.data.ptr = listener;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(EXIT_FAILURE);
	}

	struct epoll_event events[MAXEVENTS];
	while(1) {
		int n;
		if ((n = epoll_wait(efd, events, MAXEVENTS, 1000)) < 0) {
			perror("Epoll wait failed");
		}
		for (int i = 0; i < n; ++i) {
			struct request_info *active_client =
				(struct request_info *)(events[i].data.ptr);

			// Listening socket event; new client
			if (sfd == active_client->client_sfd) {
				handle_new_clients(sfd, efd);
			} else { // existing client event;
				handle_client(active_client, efd);
			}
		}
	}

	return 0;
}

int open_sfd(unsigned short port) {
	int sfd;
	if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Error creating socket");
		exit(EXIT_FAILURE);
	}
	// reusable port
	int optval = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
	// set listening file descriptor nonblocking
	if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		fprintf(stderr, "error setting socket option for listening socket\n");
		exit(1);
	}

	struct sockaddr_storage local_addr_ss;
	struct sockaddr *local_addr = (struct sockaddr *)&local_addr_ss;
	populate_sockaddr(local_addr, AF_INET, NULL, port);
	if (bind(sfd, local_addr, sizeof(struct sockaddr_storage)) < 0) {
		perror("Could not bind");
		exit(EXIT_FAILURE);
	}

	if (listen(sfd, 100) < 0) {
		perror("Could not listen");
		exit(EXIT_FAILURE);
	}

	return sfd;
}


void handle_new_clients(int sfd, int efd) {
	int cfd;
	struct sockaddr_storage remote_addr_ss;
	struct sockaddr *remote_addr = (struct sockaddr *)&remote_addr_ss;
	socklen_t addr_len = sizeof(struct sockaddr_storage);

	while ((cfd = accept(sfd, remote_addr, &addr_len)) >= 0) {
		// set client file descriptor nonblocking
		if (fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
			fprintf(stderr, "error setting socket option for client\n");
			exit(1);
		}

		printf("New client fd: %d\n", cfd);
		
		struct request_info *rinfo = malloc(sizeof(struct request_info));
		rinfo->client_sfd = cfd;
		rinfo->server_sfd = 0;
		rinfo->state = READ_REQUEST;
		rinfo->num_bytes_read_from_client = 0;
		rinfo->num_bytes_to_write_to_server = 0;
		rinfo->num_bytes_written_to_server = 0;
		rinfo->num_bytes_read_from_server = 0;
		rinfo->num_bytes_written_to_client = 0;

		// register the socket file descriptor for incoming events using
		// edge-triggered monitoring
		struct epoll_event event;
		event.data.ptr = rinfo;
		event.events = EPOLLIN | EPOLLET;
		if (epoll_ctl(efd, EPOLL_CTL_ADD, cfd, &event) < 0) {
			fprintf(stderr, "error adding event\n");
			exit(EXIT_FAILURE);
		}
	}

	// Accept failed
	if (errno == EAGAIN || errno == EWOULDBLOCK) {
		// No more clients pending
	} else {
		perror("Error in accept()");
	}
}

void handle_client(struct request_info *rinfo, int efd) {
	printf("Handling client with\n\tfd: %d\n\tstate: %d\n", rinfo->client_sfd, rinfo->state);

	switch (rinfo->state) {
		case READ_REQUEST:
			ssize_t nread;
			int total_read = 0;

			while ((nread = recv(rinfo->client_sfd, rinfo->buf + total_read, BUF_SIZE - total_read - 1, 0)) > 0) {
				total_read += nread;
				rinfo->buf[total_read] = '\0';
				
				if (complete_request_received((char *)rinfo->buf)) {
					break;
				}
			}
			if (nread == 0) {
				close(rinfo->client_sfd);
				free(rinfo);
				return;
			} else if (nread < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
				perror("Error in recv()");
				close(rinfo->client_sfd);
				free(rinfo);
				return;
			}

			print_bytes(rinfo->buf, total_read);
			char method[16], hostname[64], port[8], path[64];
			parse_request((char *)rinfo->buf, method, hostname, port, path);
			printf("Method: %s\nHostname: %s\nPort: %s\nPath: %s\n", method, hostname, port, path);

			// Create request to remote
			char request[1024];
			ssize_t request_size = sprintf(request, 
				"%s %s HTTP/1.0\r\n"
				"Host: %s\r\n"
				"User-Agent: %s\r\n"
				"Connection: close\r\n"
				"Proxy-Connection: close\r\n\r\n"
			, method, path, hostname, user_agent_hdr);

			request[request_size] = '\0';
			print_bytes((unsigned char *)request, request_size);
			
			// Connect to remote
			struct addrinfo hints, *res;
			memset(&hints, 0, sizeof(struct addrinfo));
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			int gai_result;
			if ((gai_result = getaddrinfo(hostname, port, &hints, &res)) != 0) {
				printf("getaddrinfo failed: %s\n", gai_strerror(gai_result));
			}

			struct sockaddr_storage remote_addr_ss;
			struct sockaddr *remote_addr = (struct sockaddr *)&remote_addr_ss;

			struct addrinfo *rp;
			int serverfd = -1;
			for (rp = res; rp != NULL; rp = rp->ai_next) {
				serverfd = socket(rp->ai_family, rp->ai_socktype, 0);
				if (serverfd < 0) {
					perror("Socket failed");
					close(serverfd);
					continue;
				}
				memcpy(remote_addr, rp->ai_addr, sizeof(struct sockaddr_storage));
				
				if (connect(serverfd, remote_addr, rp->ai_addrlen) >= 0) {
					break;
				}

				perror("Connect failed");
				close(serverfd);
			}
			if (serverfd == -1) {
				fprintf(stderr, "Failed to create and connect to server.\n");
				freeaddrinfo(res);
				return;
			}
			freeaddrinfo(res);
			rinfo->server_sfd = serverfd;

			// Configure remote socket as nonblocking
			if (fcntl(rinfo->server_sfd, F_SETFL, fcntl(rinfo->server_sfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
				fprintf(stderr, "error setting socket option for listening socket\n");
				exit(1);
			}

			// Remove from epoll
			epoll_ctl(efd, EPOLL_CTL_DEL, rinfo->client_sfd, NULL);

			// Register server with epoll for writing
			struct epoll_event event;
			event.data.ptr = rinfo;
			event.events = EPOLLOUT | EPOLLET;
			if (epoll_ctl(efd, EPOLL_CTL_ADD, rinfo->server_sfd, &event) < 0) {
				fprintf(stderr, "error adding event\n");
				exit(EXIT_FAILURE);
			}
			rinfo->state = SEND_REQUEST;
			
			return;
		case SEND_REQUEST:
			break;
		case READ_RESPONSE:
			break;
		case SEND_RESPONSE:
			break;
	}
}

int complete_request_received(char *request) {
	return strstr(request, "\r\n\r\n") != NULL;
}

void parse_request(char *request, char *method,
	char *hostname, char *port, char *path
) {
	char *headers_start = strstr(request, "\r\n") + 2;

	char *method_start = request;
	char *method_end = strstr(request, " ");
	memcpy(method, request, method_end - method_start);
	method[method_end - method_start] = '\0';

	char *hostname_start = strstr(request, "://") + 3;

	char *port_start;
	char *path_start;
	if ((port_start = strstr(hostname_start, ":")) != NULL && port_start < headers_start) {
		port_start += 1;
		char *port_end = strstr(port_start, "/");
		memcpy(port, port_start, port_end - port_start);
		port[port_end - port_start] = '\0';

		char *hostname_end = strstr(hostname_start, ":");
		memcpy(hostname, hostname_start, hostname_end - hostname_start);
		hostname[hostname_end - hostname_start] = '\0';

		path_start = port_end;
	} else {
		memcpy(port, "80", 3);

		char *hostname_end = strstr(hostname_start, "/");
		memcpy(hostname, hostname_start, hostname_end - hostname_start);
		hostname[hostname_end - hostname_start] = '\0';

		path_start = hostname_end;
	}
	char *path_end = strstr(path_start, " ");
	memcpy(path, path_start, path_end - path_start);
	path[path_end - path_start] = '\0';
}

void test_parser() {
	int i;
	char method[16], hostname[64], port[8], path[64];

	char *reqs[] = {
		"GET http://www.example.com/index.html HTTP/1.0\r\n"
		"Host: www.example.com\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html?foo=1&bar=2 HTTP/1.0\r\n"
		"Host: www.example.com:8080\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://localhost:1234/home.html HTTP/1.0\r\n"
		"Host: localhost:1234\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html HTTP/1.0\r\n",

		NULL
	};
	
	for (i = 0; reqs[i] != NULL; i++) {
		printf("Testing %s\n", reqs[i]);
		if (complete_request_received(reqs[i])) {
			printf("REQUEST COMPLETE\n");
			parse_request(reqs[i], method, hostname, port, path);
			printf("METHOD: %s\n", method);
			printf("HOSTNAME: %s\n", hostname);
			printf("PORT: %s\n", port);
			printf("PATH: %s\n", path);
		} else {
			printf("REQUEST INCOMPLETE\n");
		}
	}
}

void print_bytes(unsigned char *bytes, int byteslen) {
	int i, j, byteslen_adjusted;

	if (byteslen % 8) {
		byteslen_adjusted = ((byteslen / 8) + 1) * 8;
	} else {
		byteslen_adjusted = byteslen;
	}
	for (i = 0; i < byteslen_adjusted + 1; i++) {
		if (!(i % 8)) {
			if (i > 0) {
				for (j = i - 8; j < i; j++) {
					if (j >= byteslen_adjusted) {
						printf("  ");
					} else if (j >= byteslen) {
						printf("  ");
					} else if (bytes[j] >= '!' && bytes[j] <= '~') {
						printf(" %c", bytes[j]);
					} else {
						printf(" .");
					}
				}
			}
			if (i < byteslen_adjusted) {
				printf("\n%02X: ", i);
			}
		} else if (!(i % 4)) {
			printf(" ");
		}
		if (i >= byteslen_adjusted) {
			continue;
		} else if (i >= byteslen) {
			printf("   ");
		} else {
			printf("%02X ", bytes[i]);
		}
	}
	printf("\n");
	fflush(stdout);
}
