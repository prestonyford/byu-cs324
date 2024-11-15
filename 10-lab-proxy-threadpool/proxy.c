#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "sockhelper.h"

/* Recommended max object size */
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";

int open_sfd(unsigned short);
void handle_client(int);
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
	// test_parser();
	unsigned short port = atoi(argv[1]);
	int sfd = open_sfd(port);

	while(1) {
		struct sockaddr_storage remote_addr_ss;
		struct sockaddr *remote_addr = (struct sockaddr *)&remote_addr_ss;
		socklen_t addr_len = sizeof(struct sockaddr_storage);

		int connfd;
		if ((connfd = accept(sfd, remote_addr, &addr_len)) < 0) {
			perror("accept() failed");
		}
		handle_client(connfd);
	}

	// printf("%s\n", user_agent_hdr);
	return 0;
}

int open_sfd(unsigned short port) {
	int sfd;
	if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Error creating socket");
		exit(EXIT_FAILURE);
	}
	int optval = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

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

void handle_client(int sockfd) {
	size_t BUF_SIZE = 1024;
	unsigned char buf[BUF_SIZE];
	ssize_t nread;
	int total_read = 0;

	while ((nread = recv(sockfd, buf + total_read, BUF_SIZE - total_read - 1, 0)) > 0) {
		total_read += nread;
		buf[total_read] = '\0';
		
		if (complete_request_received((char *)buf)) {
			break;
		}
	}

	// print_bytes(buf, total_read);

	buf[total_read] = '\0';
	char method[16], hostname[64], port[8], path[64];
	parse_request((char *)buf, method, hostname, port, path);
	// printf("Method: %s\nHostname: %s\nPort: %s\nPath: %s\n", method, hostname, port, path);

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
	printf("%s\n", request);

	close(sockfd);
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
	char *hostname_end = strstr(hostname_start, "/");
	memcpy(hostname, hostname_start, hostname_end - hostname_start);
	hostname[hostname_end - hostname_start] = '\0';

	char *port_start;
	char *path_start;
	if ((port_start = strstr(hostname_start, ":")) != NULL && port_start < headers_start) {
		port_start += 1;
		char *port_end = strstr(port_start, "/");
		memcpy(port, port_start, port_end - port_start);
		port[port_end - port_start] = '\0';

		path_start = port_end;
	} else {
		memcpy(port, "80", 3);

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
