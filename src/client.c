#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>


#define NUM_PEERS 2
#define BUFSIZE 1024
#define MCAST_ADDR "239.192.1.100"


typedef struct server {
	//char server_id[BUFSIZE];
	char* server_id;
	struct sockaddr_in addr;
	//bool on;
	double latency;
	double bw;
	double avail_cpu;
} server_info;

typedef struct serverlist {
	server_info s_info;
	bool occupied;
} serverlist_info;
	
typedef struct thread_opts {
	int sock;
	int message_id;
	uint16_t udp_port;
} thread_opts_t;

serverlist_info _servers[NUM_PEERS];

static int create_sock(const char *udp_addr, const uint16_t port)
{
	int sock, optval = 1;
	int optlen = sizeof(optval);
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		fprintf(stderr, "socket failed\n");
		return -1;
	}
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, optlen);

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

/////////////////discovery request message send///////////////////////////////////////
static int discovery_request_send(thread_opts_t *opts)
{
	char message[50];
	struct sockaddr_in addr; //send to
	socklen_t addr_len = sizeof(addr);

	memset(&addr, 0, addr_len);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(opts->udp_port);
	addr.sin_addr.s_addr = inet_addr(MCAST_ADDR);
	
	while(1) {
		sprintf(message, "hello_%d", opts->message_id);
		if (sendto(opts->sock, message, 11, 0, (struct sockaddr*) &addr, addr_len) < 0) {
			fprintf(stderr, "sendtofailed\n");
			return -1;
		}
		opts->message_id++;
		sleep(5);
	}
	return 0;
}

static void *start_discovery_request(void *opt)
{
	thread_opts_t *opts = (thread_opts_t *) opt;
	discovery_request_send(opts);
	pthread_exit(NULL);
}

/////////////////discovery reply message receive///////////////////////////////////////
static void update_serverlist(char* buf, int id_size)
{
	int i;
	bool in = false;
	for (i = 0; i < NUM_PEERS; i++) {
		if (_servers[i].occupied == true) {
			if (strncmp(_servers[i].s_info.server_id, buf, id_size) == 0) {
				printf("this server is already in the list, %s\n", buf);
				in = true;
				break;
			}
		}
	}
	if (in == false) {
		for (i = 0; i < NUM_PEERS; i++) {
			if (_servers[i].occupied == false) {
				printf("this server joins newly, %s\n", buf);
				_servers[i].occupied = true;
				_servers[i].s_info.server_id = buf;
				break;
			}
		}
	}
	printf("update completed\n");
}

static int discovery_reply_recv(thread_opts_t *opts)
{
	char buf[BUFSIZE];
	struct sockaddr_in addr; //recv from
	ssize_t rcount;
	socklen_t addr_len = sizeof(addr);

	while(1) {
		rcount = recvfrom(opts->sock, buf, sizeof(buf), 0, (struct sockaddr*) &addr, &addr_len);
		update_serverlist(buf, 7);
	}
	return 0;
}

static void *start_reply_listen(void *opt)
{
	thread_opts_t *opts = (thread_opts_t *) opt;
	discovery_reply_recv(opts);
	pthread_exit(NULL);
}

int check_point(void)
{
	printf("heungsik is genius!!!\n");
	
	return 0;
}

static void init_peerlist()
{
	int i;
	for (i = 0; i < NUM_PEERS; i++) {
		_servers[i].occupied = false;
	}
}

int main(int argc, char *argv[])
{
	//serverlist_info servers[NUM_PEERS];
	init_peerlist();
	int sock = create_sock(NULL, 5555);
	thread_opts_t opts;
	opts.udp_port = 51234;
	opts.sock = sock;
	opts.message_id = 0;

	pthread_t discovery_request, discovery_reply;
	pthread_create(&discovery_request, NULL, start_discovery_request, &opts);
	pthread_create(&discovery_reply, NULL, start_reply_listen, &opts);

	check_point();
	pthread_join(discovery_request, NULL);
	pthread_join(discovery_reply, NULL);

	return 0;
}
	
