#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define MCAST_ADDR "239.192.1.100"
#define BUFSIZE 1024


static int create_mcast_sock(const char *udp_addr, const uint16_t port)
{
	int sock, optval = 1;
	int optlen = sizeof(optval);
	int result;
	
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	struct ip_mreq mreq;

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		fprintf(stderr, "socket failed\n");
		return -1;
	}

	result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, optlen);
	result = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &optval, optlen);
	result = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &optval, optlen);

	mreq.imr_multiaddr.s_addr = inet_addr(MCAST_ADDR);
	mreq.imr_interface.s_addr = INADDR_ANY;

	result = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
	if (result < 0) {
		fprintf(stderr, "setsockopt membership failed\n");
		close(sock);
		return -1;
	}

	memset(&addr, 0, addr_len);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(sock, (struct sockaddr*) &addr, addr_len) < 0) {
		fprintf(stderr, "bind failed\n");
		close(sock);
		return -1;
	}

	return sock;
}

static int mcast_listen(int msock)
{
	char buf[BUFSIZE];
	char info[BUFSIZE] = "server1";
	struct sockaddr_in addr;
	ssize_t rcount;
	socklen_t addr_len = sizeof(addr);
	int in_size;
	int sock = msock;

	in_size = 8;

	while (1) {
		rcount = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*) &addr, &addr_len);
		if (rcount <= 0) {
			fprintf(stderr, "recvfrom failed\n");
			close(sock);
			return -1;
		}

		if (strncmp(buf, "hello_", 6) == 0) {
			rcount = sendto(sock, info, in_size, 0, (struct sockaddr *) &addr, addr_len);
			printf("message is %s\n", buf);
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	int sock = create_mcast_sock(NULL, 51234);

	mcast_listen(sock);

	return 0;
}

