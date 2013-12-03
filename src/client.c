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
#include <math.h>

#define NUM_SERVERS 2 
#define MAX_NET_HISTORY 10

#define LOW_TIMER 8
#define DEFAULT_TIMER 4
#define HIGH_TIMER 2

#define BUFSIZE 1024
#define MCAST_ADDR "239.192.1.100"

typedef struct network {
	double latency[MAX_NET_HISTORY];
	double bw[MAX_NET_HISTORY];
	int slot_occupied;
} network_info;

typedef struct server {
	char server_id[BUFSIZE];
	struct sockaddr_in addr;
	network_info net_perf;
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

static serverlist_info _servers[NUM_SERVERS];

int interval;

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
//TODO: Implement set_timer function(based on standard deviation of network performance)
//      FSM-based timer that it will set different timer values according to stability of
//      network performance such as latency and bandwidth  
static double get_deviation()
{
	int i, j, window;
	double mean[NUM_SERVERS];
	double deviation[NUM_SERVERS];
	double ind_mean, ind_deviation, total_mean, total_deviation;

	total_mean = 0.0;
	total_deviation = 0.0;

	for(i = 0; i < NUM_SERVERS; i++)
	{
		ind_mean = 0.0;
		ind_deviation = 0.0;

		if(interval == LOW_TIMER) {
			window = 5;
		}
		else if(interval == DEFAULT_TIMER) {
			window = 8;
		}
		else {
			window = 10;
		}

		for(j = MAX_NET_HISTORY - window; j < MAX_NET_HISTORY; j++)
		{
			ind_mean += _servers[i].s_info.net_perf.latency[j];
		}

		mean[i] = ind_mean / MAX_NET_HISTORY;
		
		for(j = MAX_NET_HISTORY - window; j < MAX_NET_HISTORY; j++)
		{
			ind_deviation += (_servers[i].s_info.net_perf.latency[j] - mean[i]) 
			                    * (_servers[i].s_info.net_perf.latency[j] - mean[i]);
		}
		deviation[i] = ind_deviation / MAX_NET_HISTORY;
	}

	for(i = 0; i < NUM_SERVERS; i++)
	{
		total_deviation += (deviation[i] * 100 / mean[i]);
	}

	printf("percentage: %lf\n", total_deviation / NUM_SERVERS);

	return (total_deviation / NUM_SERVERS);
}

static int set_timer(int current_seq)
{
	double percent_deviation;

	if (current_seq < MAX_NET_HISTORY) {
		return DEFAULT_TIMER;
	}
	else {
		percent_deviation = get_deviation();
		
		if(percent_deviation < 0.05) {
			if(interval == LOW_TIMER) {
				printf("Stay in LOW TIMER\n");
				
				return LOW_TIMER;
			}
			else if(interval == DEFAULT_TIMER) {
				printf("Go to LOW TIMER\n");

				return LOW_TIMER;
			}
			else {
				printf("Go to DEFAULT TIMER\n");

				return DEFAULT_TIMER;
			}
		}
		else {
				if(interval == LOW_TIMER) {
				printf("Go to DEFAULT TIMER\n");
				
				return DEFAULT_TIMER;
			}
			else if(interval == DEFAULT_TIMER) {
				printf("Go to HIGH TIMER\n");

				return HIGH_TIMER;
			}
			else {
				printf("Stay in HIGH TIMER\n");

				return HIGH_TIMER;
			}
		}
	}
}

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
		interval = set_timer(opts->message_id);
		opts->message_id++;
		sleep(interval);
	}
	return 0;
}

static void *start_discovery_request(void *opt)
{
	thread_opts_t *opts = (thread_opts_t *) opt;
	discovery_request_send(opts);
	pthread_exit(NULL);
}

static void update_serverinfo(int num_server, double latency, double bandwidth, int m_seq)
{
	int i, j;  
	i = num_server;

	if (m_seq < MAX_NET_HISTORY) {
		_servers[i].s_info.net_perf.latency[m_seq % MAX_NET_HISTORY] = latency;
		_servers[i].s_info.net_perf.bw[m_seq % MAX_NET_HISTORY] = bandwidth;
	}
	else {
		for (j = 1; j < MAX_NET_HISTORY; j++) {
			_servers[i].s_info.net_perf.latency[j-1] = _servers[i].s_info.net_perf.latency[j];
		}
		_servers[i].s_info.net_perf.latency[MAX_NET_HISTORY-1] = latency;
		_servers[i].s_info.net_perf.bw[MAX_NET_HISTORY-1] = bandwidth;
	} 
	//printf("new inserted slot and check number: %d\n", _servers[i].s_info.net_perf.slot_occupied); 
	_servers[i].s_info.net_perf.slot_occupied++;
}

/////////////discovery reply message receive and measure network performance///////////////
static void check_server(char* buf, struct sockaddr_in *addr, thread_opts_t *opts)
{
	struct timeval t1, t2, t3;

	char tmp[BUFSIZE * 65];
	int id_size = 7;
	int i, nsend, check_seq;
	bool in = false;

	for (i = 0; i < NUM_SERVERS; i++) {
		if (_servers[i].occupied == true) {
			if (strncmp(_servers[i].s_info.server_id, buf, id_size) == 0) {
				//printf("This server is already in the list, %s\n", buf);
				gettimeofday(&t1, NULL);
				_servers[i].tcp_sock = tcp_connect(addr, opts->tcp_port);
				gettimeofday(&t2, NULL);
				if ((nsend = write(_servers[i].tcp_sock, tmp, sizeof(tmp))) < 0) {
					fprintf(stderr, "write failed\n");
				}
				gettimeofday(&t3, NULL);
				double latency = sub_timeval(&t1, &t2);
				double bw = (double)sizeof(tmp)/(sub_timeval(&t2, &t3) * BUFSIZE * BUFSIZE);
				check_seq = _servers[i].s_info.net_perf.slot_occupied;
				update_serverinfo(i, latency, bw, check_seq);
				//printf("latency: %f\n", latency);
				//printf("bandwidth: %f MB/s\n", bw);
				in = true;
				break;
			}
		}
	}
	if (in == false) {
		for (i = 0; i < NUM_SERVERS; i++) {
			if (_servers[i].occupied == false) {
				//printf("This server joins newly, %s\n", buf);
				strncpy(_servers[i].s_info.server_id, buf, id_size);
				gettimeofday(&t1, NULL);
				_servers[i].tcp_sock = tcp_connect(addr, opts->tcp_port);
				gettimeofday(&t2, NULL);
				if ((nsend = write(_servers[i].tcp_sock, tmp, sizeof(tmp))) < 0) {
					fprintf(stderr, "write failed\n");
				}
				gettimeofday(&t3, NULL);
				double latency = sub_timeval(&t1, &t2);
				double bw = (double)sizeof(tmp)/(sub_timeval(&t2, &t3) * BUFSIZE * BUFSIZE);
				check_seq = _servers[i].s_info.net_perf.slot_occupied;
				update_serverinfo(i, latency, bw, check_seq);
				//printf("latency: %f\n", latency);
				//printf("bandwidth: %f MB/s\n", bw);
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
		check_server(buf, &addr, opts);
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

static void init_serverlist()
{
	int i;
	for (i = 0; i < NUM_SERVERS; i++) {
		_servers[i].tcp_sock = -1;
		_servers[i].occupied = false;
		_servers[i].s_info.net_perf.slot_occupied = 0;
	}
}

int main(int argc, char *argv[])
{
	//_interval = atoi(argv[1]);
	init_serverlist();
	thread_opts_t opts;
	opts.mcast_sock = mcast_create_sock(NULL, 5555);
	opts.mcast_port = 51233;
	opts.tcp_port = 51234;
	opts.message_id = 0;

	pthread_t discovery_request, discovery_reply;
	pthread_create(&discovery_request, NULL, start_discovery_request, &opts);
	pthread_create(&discovery_reply, NULL, start_reply_listen, &opts);
	init_serverlist();
	//check_point();
	pthread_join(discovery_request, NULL);
	pthread_join(discovery_reply, NULL);

	return 0;
}
	
