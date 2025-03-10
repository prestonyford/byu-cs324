// Replace PUT_USERID_HERE with your actual BYU CS user id, which you can find
// by running `id -u` on a CS lab machine.
#define USERID 1823731471

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>

#include "sockhelper.h"

int verbose = 0;

void print_bytes(unsigned char *bytes, int byteslen);

int main(int argc, char *argv[]) {
	if (argc < 5) {
		printf("Usage: <server> <port> <level> <seed>\n");
		return 1;
	}
	char *server = argv[1];
	char *port = argv[2];
	unsigned char level = atoi(argv[3]);
	unsigned short seed = atoi(argv[4]);

	// printf("Server: %s\n", server);
	// printf("Port: %s\n", port);
	// printf("Level: %d\n", level);
	// printf("Seed: %d\n", seed);

	size_t request_size = 8;
	unsigned char initial_request[8];
	initial_request[0] = 0x0;
	initial_request[1] = level;
	unsigned int userID_n = htonl(USERID);
	unsigned short seed_n = htons(seed);
	memcpy(&initial_request[2], &userID_n, 4);
	memcpy(&initial_request[6], &seed_n, 2);

	// print_bytes(initial_request, 8);

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM; // UDP socket
	struct addrinfo *result;

	int status = getaddrinfo(server, port, &hints, &result);
	if (status != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		exit(EXIT_FAILURE);
	}

	int sfd;
	int addr_fam;
	socklen_t addr_len;

	struct sockaddr_storage local_addr_ss;
	struct sockaddr *local_addr = (struct sockaddr *)&local_addr_ss;
	struct sockaddr_storage remote_addr_ss;
	struct sockaddr *remote_addr = (struct sockaddr *)&remote_addr_ss;

	char local_ip[INET6_ADDRSTRLEN];
	unsigned short local_port;
	char remote_ip[INET6_ADDRSTRLEN];
	unsigned short remote_port;

	struct addrinfo *rp;
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, 0);
		if (sfd < 0) {
			continue; // Try the next address
		}
		socklen_t local_addr_len = sizeof(struct sockaddr_storage);
		getsockname(sfd, local_addr, &local_addr_len);
		parse_sockaddr(local_addr, local_ip, &local_port);

		addr_fam = rp->ai_family;
		addr_len = rp->ai_addrlen;
		memcpy(remote_addr, rp->ai_addr, sizeof(struct sockaddr_storage));
		parse_sockaddr(remote_addr, remote_ip, &remote_port);
	}

	unsigned char allchunks[1024];
	ssize_t total_received = 0;

	unsigned char *request = initial_request;
	unsigned char chunklen;
	unsigned char opcode;
	unsigned short opparam;
	unsigned int nonce;
	do {
		// print_bytes(request, request_size);
		ssize_t nwritten = sendto(sfd, request, request_size, 0, remote_addr, addr_len);
		if (nwritten < 0) {
			perror("send");
			exit(EXIT_FAILURE);
		}

		unsigned char buf[256];
		ssize_t nreceived = recvfrom(sfd, buf, sizeof(buf), 0, remote_addr, &addr_len);
		// print_bytes(buf, nreceived);

		// chunklen, chunk, opcode, opparam, nonce
		chunklen = buf[0];
		if(chunklen == 0) {
			break;
		} else if(chunklen > 127) {
			printf("Error: %x\n", chunklen);
			exit(1);
		}

		unsigned char chunk[chunklen];
		memcpy(chunk, &buf[1], chunklen);
		opcode = buf[chunklen + 1];
		memcpy(&opparam, &buf[chunklen + 2], 2);
		opparam = ntohs(opparam);
		memcpy(&nonce, &buf[chunklen + 4], 4);
		nonce = ntohl(nonce);

		memcpy(&allchunks[total_received], chunk, chunklen);
		total_received += chunklen;

		// printf("%x\n", chunklen);
		// printf("%s\n", chunk);
		// printf("%x\n", opcode);
		// printf("%x\n", opparam);
		// printf("%x\n", nonce);

		if (opcode == 1) {
			remote_port = opparam;
			populate_sockaddr(remote_addr, addr_fam, remote_ip, remote_port); 
		} else if (opcode == 2) {
			local_port = opparam;
			populate_sockaddr(local_addr, addr_fam, NULL, local_port);
			close(sfd);
			sfd = socket(addr_fam, SOCK_DGRAM, 0);
			if (bind(sfd, local_addr, sizeof(struct sockaddr_storage)) < 0) {
				perror("bind()");
			}
		} else if (opcode == 3) {
			unsigned short m = opparam;
			nonce = 0;
			unsigned char buf4[1];
			buf4[0] = 0;
			for (int i = 0; i < m; ++i) {
				struct sockaddr_storage m_addr_ss;
				struct sockaddr *m_addr = (struct sockaddr *)&m_addr_ss;
				socklen_t m_addr_len = sizeof(m_addr_ss);
				// populate_sockaddr(m_addr, addr_fam, remote_ip, local_port);
				recvfrom(sfd, buf4, 1, 0, m_addr, &m_addr_len);

				char ip[INET6_ADDRSTRLEN];
				unsigned short p;

				parse_sockaddr(m_addr, ip, &p);
				nonce += p;
			}
			//++nonce;
		} else if (opcode == 4) {
			remote_port = opparam;
			populate_sockaddr(remote_addr, addr_fam, remote_ip, remote_port); 

			struct addrinfo p4_hints;
			memset(&p4_hints, 0, sizeof(p4_hints));
			addr_fam = addr_fam == AF_INET ? AF_INET6 : AF_INET;
			p4_hints.ai_family = addr_fam;
			p4_hints.ai_socktype = SOCK_DGRAM;
			struct addrinfo *p4_result;

			char port_str[6];
			sprintf(port_str, "%d", remote_port);
			int p4_status = getaddrinfo(server, port_str, &p4_hints, &p4_result);
			if (p4_status != 0) {
				fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
				exit(EXIT_FAILURE);
			}

			struct addrinfo *p4_rp;
			int new_sfd;
			for (p4_rp = p4_result; p4_rp != NULL; p4_rp = p4_rp->ai_next) {
				new_sfd = socket(p4_rp->ai_family, p4_rp->ai_socktype, 0);
				if (new_sfd < 0) {
					continue; // Try the next address
				}
				close(sfd);
				sfd = new_sfd;
				socklen_t local_addr_len = sizeof(struct sockaddr_storage);
				getsockname(sfd, local_addr, &local_addr_len);
				parse_sockaddr(local_addr, local_ip, &local_port);

				addr_fam = p4_rp->ai_family;
				addr_len = p4_rp->ai_addrlen;
				memcpy(remote_addr, p4_rp->ai_addr, sizeof(struct sockaddr_storage));
				parse_sockaddr(remote_addr, remote_ip, &remote_port);
			}
		}

		

		memset(request, 0, request_size);
		request_size = sizeof(nonce);
		int nextnonce = htonl(nonce + 1);
		memcpy(request, &nextnonce, request_size);

	} while (chunklen > 0);

	allchunks[total_received] = '\0';
	printf("%s\n", allchunks);
	
	// printf("Response received: %ld bytes\n", nreceived);
	// print_bytes(buf, nreceived);
	// printf("\n");

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
