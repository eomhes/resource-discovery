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
#define MAX_PEERS 10
#define BUFSIZE 1024

typedef struct thread_opts {
	int sock;
	uint16_t udp_port;
	uint16_t tcp_port;
} thread_opts_t;

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

static int mcast_listen(thread_opts_t *opts)
{
	char buf[BUFSIZE];
	char info[BUFSIZE] = "server1";
	struct sockaddr_in addr;
	ssize_t rcount;
	socklen_t addr_len = sizeof(addr);
	int in_size;
	int sock = opts->sock;

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

static void *start_mcast_thread(void *data)
{
	thread_opts_t *opts = (thread_opts_t *) data;
	mcast_listen(opts);
	pthread_exit(NULL);
}

static void *
handle_client(void *arg)
{
	printf("created handle_clent\n");
    int buf_size, tcp_sock = (int) arg;
    char buff[BUFSIZE];
    char *tmp_buf, *big_buf = NULL, *buf = buff;
    size_t nread, nnread, sz = -1;

    while (1) {
        if ((nread = recv(tcp_sock, buf, sizeof(buff), 0)) < 1) {
            fprintf(stderr, "recv failed\n");
            break;
        }
		printf("buf: %s", buf);
        tmp_buf = buf;


        uint32_t *len = (uint32_t *) buf;
        int left = (*len) - nread;

        if (left > 0) {
            big_buf = malloc(*len);
            memcpy(big_buf, buf, nread);

            while ( left > 0) {
                if ((nnread = recv(tcp_sock, buf, 1024, 0)) < 1) {
                    fprintf(stderr, "recv large buffer failed\n");
                    close(tcp_sock);
                }
                memcpy(big_buf + nread, buf, nnread);
                nread += nnread;
                left -= nnread;
                //printf("left %d nread %d nnread %d\n", left, nread, nnread);
            }
            buf = big_buf;
            //sz = process_request(&buf, nread, buf_size);
        }
        else {
            //sz = process_request(&buf, nread, sizeof(buff));
        }

        //printf("total nread %d\n", nread);

        //printf("sz to send %d\n", sz);
        
        uint32_t *sz_ptr = (uint32_t *)buf;
        *sz_ptr = sz;


        int idx = sz, i = 0, nsend = 0;
        while (idx > 1) {
            if ((nsend = send(tcp_sock, buf + i, idx, 0)) < 1) {
                fprintf(stderr, "send failed\n");
                close(tcp_sock);
            }
            idx -= nsend;
            i += nsend;
            //printf("idx %d i %d\n", idx, i);
        }

        //printf("done sent %d\n", i);

        if (tmp_buf != buf) {
            fprintf(stderr, "buf changed, restoring it\n");
            buf = tmp_buf;
        }

        if (big_buf != NULL) {
            fprintf(stderr, "freeing big_buf\n");
            free(big_buf);
            big_buf = NULL;
        }
    }

    close(tcp_sock);
    pthread_exit(NULL);
}

static int
tcp_listen(const char *ip, const uint16_t port)
{
	char buf[BUFSIZE];
    int sock, cli_sock, opt;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    pthread_t thread_id;

    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        fprintf(stderr, "socket failed\n");
        return -1;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "setsockopt failed\n");
        return -1;
    }

    memset(&addr, 0, addr_len);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *) &addr, addr_len) < 0) {
        fprintf(stderr, "bind failed\n");
        close(sock);
        return -1;
    }

    if (listen(sock, MAX_PEERS) < 0) {
        fprintf(stderr, "listen failed\n");
        close(sock);
        return -1;
    }
	
	//while (1) {	
		if ((cli_sock = accept(sock, (struct sockaddr *) &addr, &addr_len)) < 0) {
			fprintf(stderr, "accept failed\n");
			close(sock);
			return -1;
		}
		else {
			printf("tcp connection accepted!!\n");
		}

	while (1) {	
		if (recv(cli_sock, buf, sizeof(buf), 0) < 1) {
			fprintf(stderr, "recv failed\n");
		}
		//else {
		//	printf("received %s\n", buf);
		//}
	}


	//printf("get tcp packet\n");
	
	/*
    while (1) {
        cli_sock = accept(sock, (struct sockaddr *) &addr, &addr_len);
		printf("get tcp packet\n");
        if (cli_sock < 0) {
            fprintf(stderr, "accept failed\n");
            break;
        }
        if (pthread_create(&thread_id, NULL, handle_client, 
            (void *) cli_sock) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            close(cli_sock);
			printf("created one more thread\n");
            break;
        }

        pthread_detach(thread_id);
    }*/
    return 0;
}

static void *
start_tcp_thread(void *data)
{
	thread_opts_t *opts = (thread_opts_t *) data;
	tcp_listen(NULL, opts->tcp_port);
	pthread_exit(NULL);
}

int main(int argc, char **argv)
{
	thread_opts_t opts;
	opts.udp_port = 51234;
	opts.tcp_port = 51234;
	opts.sock = create_mcast_sock(NULL, 51233);

	pthread_t mcast_thread;
	pthread_create(&mcast_thread, NULL, start_mcast_thread, &opts);

	start_tcp_thread(&opts);
	pthread_join(mcast_thread, NULL);

	return 0;
}

