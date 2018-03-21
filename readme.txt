INTRODUCTION
------------

This project is a simple implementation of Go-Back-N protocol. The sender 
send a string user input to receiver using UDP packet to simulate the frames
of Go-Back-N. 

Before sending data, The sender send SYN packet to request connection. If 
received ACK from receiver, the sender continues to send a number of frames 
without receiving ACK. The receiver send ACK back when receiving packet with 
expected sequence number. After finishing sending, the sender send FIN packet 
to notify receiver the end of transfer. 


The packet is defined as:
typedef struct packet {
	uint8_t flg;
	uint32_t seq;
	uint32_t crc32;
	char data[1];
} packet_t;


 * The project is located at:
   https://github.com/uscwy/ee450project2
   
   sender.cpp    	Implementation of sender
   receiver.cpp     Implementation of receiver
   test.sh	  		A simple test script


REQUIREMENTS
------------

This project need Mininet enviroment to run.


MAINTAINER
------------

Yong Wang <yongw@usc.edu>

