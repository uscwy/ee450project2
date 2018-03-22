#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <zlib.h>
#include <iostream>
#include <string>
#include <cmath>

#define SRV_PORT		"18200"
#define SRV_NAME		"10.0.0.2"
#define MAX_STR_LEN		(1024-1)
#define BUFLEN			1024
#define WND_SIZE		5
/*RTT and TIMEOUT are in millisecond*/
#define RTT				(100)   
#define TIMEOUT			(200)

char buf[BUFLEN];

int sockfd;
struct addrinfo *res = NULL;
unsigned int last_ack, wnd_min; /*the last received ack*/

#define ACK_FLG (1<<3)
#define SYN_FLG (1<<2)
#define FIN_FLG (1<<1)

/*GBNUDP packet structure*/
#pragma pack(1) 
typedef struct packet {
	uint8_t flg;
	uint32_t seq;
	uint32_t crc32;
	char data[1];
} packet_t;
#pragma pack()

#define PACKET_LEN	(sizeof(struct packet))
#define HEADER_LEN	(PACKET_LEN-1)

using namespace std;

void cleanup_exit(const char *errmsg) {

	if(sockfd >= 0) {
		close(sockfd);
	}

	if(res != NULL) {
		freeaddrinfo(res);
	}

	if(errmsg != NULL) {
		perror(errmsg);
		exit(EXIT_FAILURE);
	}
	return;
}
/*caculate and return CRC for packet*/
uint32_t pkg_crc32(packet_t *pkg)
{
	uLong crc = crc32(0L, Z_NULL, 0);

	crc = crc32(crc, (Bytef *)&(pkg->flg), sizeof(pkg->flg));
	crc = crc32(crc, (Bytef *)&(pkg->seq), sizeof(pkg->seq));
	crc = crc32(crc, (Bytef *)&(pkg->data[0]), sizeof(pkg->data));
	//cout << "crc is " << crc << endl;
	return (uint32_t)crc;
}
/*check CRC, return 0 if success*/
int crc_check(packet_t *pkg)
{
	if(ntohl(pkg->crc32) != pkg_crc32(pkg))
	{
		cout << "Packet discarded due to corruption" << endl;
		return -1;
	}
	return 0;
}
/*send data to receiver*/
void GBNUDP_send(const char *data)
{
	int sockfd, ret, connected;
	struct addrinfo hints;
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);

	struct timespec spec;
	unsigned long waittime, total;
	unsigned long last_send, last_ack, wnd_max;
	packet_t *pkg;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET; /*only try ipv4 address*/
	hints.ai_socktype = SOCK_DGRAM;
	/*fill address struct*/
	if((ret = getaddrinfo(SRV_NAME, SRV_PORT, &hints, &res)) != 0) {
		cerr << "getaddrinfo:" << gai_strerror(ret) << endl;
		cleanup_exit("getaddrinfo");
	}
	
	/*create a socket fd*/
	if((sockfd = socket(res->ai_family, res->ai_socktype, 
			res->ai_protocol)) < 0) {
		cleanup_exit("create socket fail");
	}
	pkg = (packet_t *)&buf[0];
	memset(pkg, 0, HEADER_LEN);

	/*inital sleep time*/
	spec.tv_sec = 0;
	spec.tv_nsec = 20 * pow(10,6);

	/*inital sliding window control*/
	last_send = 0;
	last_ack = 0;
	waittime = 0;
	connected = 0;
	
	total = strlen(data);
	/*send packets until receive ack of the last packet.*/
	while(last_ack < total)
	{
		/*first SYN packet*/
		if(connected == 0)
		{
			/*for syn, change windows size to 1*/
			wnd_max = 1;
		}
		else /*normal packet*/
		{
			wnd_max = last_ack + WND_SIZE;
			if(wnd_max > total) wnd_max = total;
			
		}
		/*send packets in window*/
		while(last_send < wnd_max)
		{
			if(connected == 1) 
			{
				pkg->flg = 0;
				pkg->data[0] = data[last_send]; 
				pkg->seq = htonl(last_send+1); 
				cout << "Sending character \"" << pkg->data[0] << "\"" << endl;
			}
			else
			{
				pkg->flg = SYN_FLG;
				pkg->seq = 0;
				cout << "Establishing a connection to receiver... (sending SYN)" << endl;
			}
			pkg->crc32 = htonl(pkg_crc32(pkg));
			ret = sendto(sockfd, pkg, PACKET_LEN, 0, res->ai_addr, res->ai_addrlen);
			if(ret == PACKET_LEN)
			{
				last_send++;
			}
		}

		/*recv ACK*/
		ret = recvfrom(sockfd, pkg, BUFLEN, MSG_DONTWAIT, &addr, &addrlen);
		if(ret == PACKET_LEN && crc_check(pkg) == 0 && (pkg->flg & ACK_FLG) >0)
		{
			cout << "ACK Received with SEQ# " << ntohl(pkg->seq) << endl;
			if(ntohl(pkg->seq) > last_ack)
			{
				last_ack = ntohl(pkg->seq);
				/*reset timer*/
				waittime = 0;
			}
			else if(ntohl(pkg->seq) == 0) /*first ACK */
			{
				connected = 1;
				last_send = 0;
			}
		}
		else if(ret < 0)
		{
			/*no packet, sleep some ticks*/
			nanosleep(&spec, NULL);
			/*update timer*/
			waittime = waittime + (spec.tv_nsec/pow(10,6));
			/*check the timer. if timeout, resend the whole window*/
			if(waittime >= TIMEOUT)
			{
				cout << "Window timed-out…"  << endl;
				last_send = last_ack;
				waittime = 0;
			}
		}
	}
	
	/*FIN packet*/
	pkg->flg = FIN_FLG;
	pkg->seq = htonl(last_ack + 1);
	pkg->data[0] = 0;
	pkg->crc32 = htonl(pkg_crc32(pkg));
	bool acked = false;
	waittime = 0;
	int retry = 0;
	/*try to close connection*/
	while(!acked)
	{
		/*send FIN packet*/
		cout << "Terminating connection… (sending FIN)" << endl;
		if((ret = sendto(sockfd, pkg, PACKET_LEN, 0,
						res->ai_addr, res->ai_addrlen)) == -1) {
			cerr << "send failed" << endl;
		}
		waittime = 0;
		retry++;
		ret = -1;
		/*recv ACK*/
		while(waittime < RTT && ret < 0)
		{
			nanosleep(&spec, NULL);
			waittime += spec.tv_nsec/pow(10,6);
			
			ret = recvfrom(sockfd, pkg, BUFLEN, MSG_DONTWAIT, &addr, &addrlen);
		}
		if(ret == PACKET_LEN && crc_check(pkg) == 0)
		{
			cout << "ACK Received with SEQ# " << ntohl(pkg->seq) << endl;
			acked = true;
			break;
		}
		if(retry >= 25) /* 25RTT = 5seconds*/
		{
			cout << "Wait ACK of FIN timeout" << endl;
			break;
		}
	}

	cout << "Done!" << endl;

	return;
}

int main(int argc, char **argv) 
{
	struct timeval tv1,tv2;
	unsigned long t;
	string str;
	/*get number from shell parameter*/
	if(argc > 1) {
		str = argv[1];
	}
	else
	{
		cout << "Please provide a string with at least 10 characters: " << endl; 
		cin >> str;
	}
	if(str.length() < 10)
	{
		cout << "String is too short." << endl;
		return 0;
	}
	/*check the number if it is too big*/
	if(str.length() > MAX_STR_LEN) {
		cout << "String is too long." << endl;
		return 0;
	}
		
	/*record current time*/
    gettimeofday(&tv1, NULL);

	GBNUDP_send(str.c_str());
	
	gettimeofday(&tv2, NULL);
	t = tv2.tv_usec + ((tv2.tv_sec - tv1.tv_sec) * pow(10,6)) - tv1.tv_usec;
	cout << "Total transfer time: " << t << "(us)" << endl;

	cleanup_exit(NULL);
	return 0;
}

