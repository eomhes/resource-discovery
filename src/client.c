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
#include <sys/time.h>

#define NUM_PEERS 2
#define BUFSIZE 1024
#define MCAST_ADDR "239.192.1.100"

typedef struct server {
	char server_id[BUFSIZE];
	struct sockaddr_in addr;
	double latency;
	double bw;
	double avail_cpu;
} server_info;

typedef struct serverlist {
	server_info s_info;
	int tcp_sock;
	bool occupied;
} serverlist_info;
	
typedef struct thread_opts {
	int mcast_sock;
	uint16_t mcast_port;
	uint16_t tcp_port;
	int message_id;
} thread_opts_t;

static serverlist_info _servers[NUM_PEERS];

static double
sub_timeval(const struct timeval *t1, const struct timeval *t2)
{
    long int long_diff = (t2->tv_sec * 1000000 + t2->tv_usec) - 
        (t1->tv_sec * 1000000 + t1->tv_usec);
    double diff = (double)long_diff/1000000;
    return diff;
}

static int mcast_create_sock(const char *udp_addr, const uint16_t port)
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

static int tcp_connect(struct sockaddr_in *addr,  uint16_t port)
{
	int sock;

	socklen_t addr_len = sizeof(*addr);
	((struct sockaddr_in*) addr)->sin_port = htons(port);

	if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) <0) {
		fprintf(stderr, "socket failed\n");
		return -1;
	}

	if (connect(sock, (struct sockaddr *) addr, addr_len) < 0) {
		fprintf(stderr, "connect failed\n");
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
	addr.sin_port = htons(opts->mcast_port);
	addr.sin_addr.s_addr = inet_addr(MCAST_ADDR);
	
	while(1) {
		sprintf(message, "hello_%d", opts->message_id);
		if (sendto(opts->mcast_sock, message, 11, 0, (struct sockaddr*) &addr, addr_len) < 0) {
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

/////////////discovery reply message receive and measure network performance///////////////
static void update_serverlist(char* buf, struct sockaddr_in *addr, thread_opts_t *opts)
{
	struct timeval t1, t2, t3;

	char tmp[BUFSIZE*65];
	int id_size = 7;
	int i;
	int nsend;
	bool in = false;
	for (i = 0; i < NUM_PEERS; i++) {
		if (_servers[i].occupied == true) {
			if (strncmp(_servers[i].s_info.server_id, buf, id_size) == 0) {
				printf("This server is already in the list, %s\n", buf);
				gettimeofday(&t1, NULL);
				_servers[i].tcp_sock = tcp_connect(addr, opts->tcp_port);
				gettimeofday(&t2, NULL);
				if ((nsend = write(_servers[i].tcp_sock, tmp, sizeof(tmp))) < 0) {
					fprintf(stderr, "write failed\n");
				}
				gettimeofday(&t3, NULL);
				double latency = sub_timeval(&t1, &t2);
				double diff = sub_timeval(&t2, &t3);
				printf("latency: %f\n", latency);
				printf("bandwidth: %f MB/s\n", (double)sizeof(tmp)/(diff * BUFSIZE * BUFSIZE) );
				in = true;
				break;
			}
		}
	}
	if (in == false) {
		for (i = 0; i < NUM_PEERS; i++) {
			if (_servers[i].occupied == false) {
				printf("This server joins newly, %s\n", buf);
				strncpy(_servers[i].s_info.server_id, buf, id_size);
				gettimeofday(&t1, NULL);
				_servers[i].tcp_sock = tcp_connect(addr, opts->tcp_port);
				gettimeofday(&t2, NULL);
				if ((nsend = write(_servers[i].tcp_sock, tmp, sizeof(tmp))) < 0) {
					fprintf(stderr, "write failed\n");
				}
				gettimeofday(&t3, NULL);
				double latency = sub_timeval(&t1, &t2);
				double diff = sub_timeval(&t2, &t3);
				printf("latency: %f\n", latency);
				printf("bandwidth: %f MB/s\n", (double)sizeof(tmp)/(diff * BUFSIZE * BUFSIZE));
				_servers[i].occupied = true;
				_servers[i].s_info.addr = *addr;
				break;
			}
		}
	}
}

static int discovery_reply_recv(thread_opts_t *opts)
{
	char buf[BUFSIZE];
	struct sockaddr_in addr; //recv from
	ssize_t rcount;
	socklen_t addr_len = sizeof(addr);

	while(1) {
		rcount = recvfrom(opts->mcast_sock, buf, sizeof(buf), 0, (struct sockaddr*) &addr, &addr_len);
		update_serverlist(buf, &addr, opts);
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
	printf("start multicast resource discovery process!!!\n");
	return 0;
}

static void init_peerlist()
{
	int i;
	for (i = 0; i < NUM_PEERS; i++) {
		_servers[i].tcp_sock = -1;
		_servers[i].occupied = false;
	}
}

int main(int argc, char *argv[])
{
	init_peerlist();
	thread_opts_t opts;
	opts.mcast_sock = mcast_create_sock(NULL, 5555);
	opts.mcast_port = 51233;
	opts.tcp_port = 51234;
	opts.message_id = 0;

	pthread_t discovery_request, discovery_reply;
	pthread_create(&discovery_request, NULL, start_discovery_request, &opts);
	pthread_create(&discovery_reply, NULL, start_reply_listen, &opts);

	check_point();
	pthread_join(discovery_request, NULL);
	pthread_join(discovery_reply, NULL);

	return 0;
}
	
