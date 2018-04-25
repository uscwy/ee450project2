INTRODUCTION
------------

This project is a simple implementation of Go-Back-N protocol. The sender 
send a string user input to receiver using UDP packet to simulate the frames
of Go-Back-N. 

Before sending data, The sender sends SYN packet to request connection. After
connection establised, the sender continues to send a number of frames 
without receiving ACK. The receiver will send ACK back for every packet 
received. After finishing sending, the sender send FIN packet to close the 
connection. 


The packet is defined as:
typedef struct packet {
	uint8_t flg;
	uint32_t seq;
	uint32_t crc32;
	char data[1];
} packet_t;


 * The project is located at:
   https://github.com/uscwy/ee450project2
   
   sender.cpp    Implementation of sender
   receiver.cpp  Implementation of receiver
   plot.pdf      Report document
   test.sh       Test script


REQUIREMENTS
------------

This project need Mininet to simulate network latency and packet loss.


MAINTAINER
------------

Yong Wang <yongw@usc.edu>

