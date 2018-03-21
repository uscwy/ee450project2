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
#include <sys/epoll.h>
#include <syscall.h>
#include <iostream>
#include <cmath>

#define PORT			"18200"
#define MAX_STR_LEN		(1024-1)
#define BUFLEN			1024
#define WND_SIZE		5
/*RTT and TIMEOUT are in microsecond*/
#define RTT				(100)   
#define TIMEOUT			(200)

char buf[BUFLEN];
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

/*link list to store received characters*/

using namespace std;

int sockfd = -1, efd= -1;
struct addrinfo hints, *addr = NULL;

/*clean all resouces and exit*/
void cleanup(const char *errmsg) {
	
	/*print error message*/
	if(errmsg != NULL)
		perror(errmsg);
	/*clean up resources*/
	if(sockfd >=0 )
	{
		close(sockfd);
		sockfd = -1;
	}
	if(efd >=0 )
	{
		close(efd);
		efd = -1;
	}	
	if(addr != NULL) {
		freeaddrinfo(addr);
		addr = NULL;
	}
}
/*caculate and return CRC for packet*/
uint32_t crc32(packet_t *pkg)
{
	return 0;
}
/*check CRC, return 0 if success*/
int crc_check(packet_t *pkg)
{
	return 0;
}
/*send ack to sender*/
int send_ack(int s, packet_t *pkg, struct sockaddr *dst, socklen_t len, unsigned long exp)
{
	packet_t ack;
	
	ack.seq = pkg->seq;
	ack.flg = pkg->flg | ACK_FLG;
	ack.data[0] = 0;
	ack.crc32 = htonl(crc32(&ack));
	cout << "Sending ACK with SEQ # " << ntohl(ack.seq);
	if((pkg->flg & FIN_FLG) > 0)
	{
		cout << endl;
	}
	else
	{
		cout << ", expecting SEQ # " << exp << endl;
	}
	return sendto(s, &ack, PACKET_LEN, 0, dst, len);
}
/*
return received string to caller
the memory is dynamically allocated, caller need to free it after use.
*/
char* GBNUDP_receive() {
	struct sockaddr sender;
	socklen_t addrlen;
	int ret;
	char *str = NULL;
	struct epoll_event ev;
	char ipstr[32];
	bool fin = false;
	unsigned long seq_exp;
	packet_t *pkg;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;
	
	if((ret = getaddrinfo(NULL, PORT, &hints, &addr)) != 0) {
		cleanup("getaddrinfo");
		return NULL;
	}
	/*creat socket descriptor*/
	if((sockfd = socket(addr->ai_family, addr->ai_socktype, 
			addr->ai_protocol)) < 0)
	{
		cleanup("socket");
		return NULL;
	}
	/*reserve port on server*/
	if(bind(sockfd, addr->ai_addr, addr->ai_addrlen) == -1) {
		cleanup("bind");
		return NULL;
	}
	
	/*use epoll to monitor sockets*/
	if((efd = epoll_create(1)) < 0) {
		cleanup("epoll_create");
		return NULL;
	}
	
	ev.events = EPOLLIN;
	ev.data.fd = sockfd;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
		cleanup("epoll_ctl");
		return NULL;
	}

	addrlen = sizeof(sender);
	/*alloc memory for received characters*/
	str = (char *)malloc(MAX_STR_LEN+1);
	memset(str, 0, MAX_STR_LEN+1);
	
	/*inital receiver control*/
	seq_exp = 0;
	pkg = (packet_t *)&buf[0];
	memset(pkg, 0, PACKET_LEN);
	
	cout << "Receiver is running and ready to " 
	"receive connections on port "PORT"..."
	<< endl;

	/*start receiving data until finished*/
	while(!fin)
	{
		/*prevent buffer overflow*/
		if(seq_exp >= MAX_STR_LEN)
			break; 
		/*block here to wait packets from sender*/
		epoll_wait(efd, &ev, 1, -1);

		ret = recvfrom(sockfd, pkg, BUFLEN, MSG_DONTWAIT, &sender, &addrlen);
		if(ret < (int)PACKET_LEN)
		{
			cout << "Drop truncated packet (length =" << ret << ")" << endl;
			continue;
		}
		else if(crc_check(pkg) < 0)
		{
			/*drop packets with wrong CRC*/
			cout << "Drop packet (wrong CRC)" << endl;
			continue;
		}
		/*SYN packet, we only accept SYN before receiving normal data(seq_exp <=1)*/
		else if((pkg->flg & SYN_FLG) > 0 && seq_exp <= 1)
		{
			struct sockaddr_in *s = (struct sockaddr_in *)&sender;
			inet_ntop(sender.sa_family, &(s->sin_addr), ipstr, sizeof(ipstr));
			cout << "Connection request received from " ;
			cout << "<" << ipstr << ":" << ntohs(s->sin_port) << ">" << endl;
			seq_exp = 1;
			send_ack(sockfd, pkg, &sender, addrlen, seq_exp);
		}
		/*FIN packet*/
		else if((pkg->flg & FIN_FLG) > 0)
		{
			cout << "Sender is terminating with FIN…" << endl;
			send_ack(sockfd, pkg, &sender, addrlen, seq_exp);
			/*there is a situation that ACK of FIN lost, sender need to handle it by itself*/
			fin = true;
			break;
		}
		/*normal data*/
		else if(ntohl(pkg->seq) == seq_exp)
		{
			/*store data and update expected sequence*/
			str[seq_exp - 1] = pkg->data[0];
			cout << "Received character \"" << pkg->data[0] << "\"" << endl;
			seq_exp++;
			send_ack(sockfd, pkg, &sender, addrlen, seq_exp);
		}
	}
	/*close socket and free resources*/
	cleanup(NULL);
	
	return str;
}
int main()
{
	while(1)
	{
		char *str = GBNUDP_receive();
		if(str != NULL)
		{
			cout << "Reception Complete: \"" << str << "\"" << endl;
			free(str);
		}
	}
}
