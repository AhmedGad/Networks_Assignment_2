#include <iostream>
#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), bind, and connect() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() and getpid() */
#include <fcntl.h>      /* for fcntl() */
#include <sys/file.h>   /* for O_NONBLOCK and FASYNC */
#include <signal.h>     /* for signal() and SIGALRM */
#include <errno.h>      /* for errno */
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <stdio.h>

#define ECHOMAX 255     /* Longest string to echo */

int sock; /* Socket -- GLOBAL for signal handler */
double prob_datagram_loss; /* Probability of datagram loss */
double rnd_seed; /* Random generator seed value. */
int max_CWND; /* Maximum sending sliding-window size - in datagram units */
struct sockaddr_in echoClntAddr; /* Address of datagram source */
char *DIR;

using namespace std;

struct packet {
	/* Header */
	uint16_t cksum; /* Optional bonus part */
	uint16_t len;
	uint32_t seqno;
	/* Data */
	char data[256];
};

/* Ack-only packets are only 8 bytes */
struct ack_packet {
	uint16_t cksum; /* Optional bonus part */
	uint16_t len;
	uint32_t ackno;
};

void bufferData() {
	unsigned int clntLen = sizeof(echoClntAddr);
	ifstream myFile(DIR, ifstream::in | ios::in | ios::binary);
	packet currentPacket;

	int curSequenceNumber = 0;
	while (myFile.good()) {
		currentPacket.len = myFile.read(currentPacket.data,
				sizeof(currentPacket.data)).gcount();
		currentPacket.seqno = (curSequenceNumber++);
		sendto(sock, &currentPacket, sizeof(currentPacket), 0,
				(struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr));

		// -------------Ack ay kalam
		ack_packet ackPacket;
		recvfrom(sock, &ackPacket, sizeof(ack_packet), 0,
				(struct sockaddr *) &echoClntAddr, &clntLen);
		// -------------
	}

	// Send last packet with data length =  0 to inform the client to close
	currentPacket.len = 0;
	sendto(sock, &currentPacket, sizeof(currentPacket), 0,
			(struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr));
	myFile.close();
}

void SIGIOHandler() {
	unsigned int clntLen; /* Address length */
	int recvMsgSize; /* Size of datagram */
	char echoBuffer[ECHOMAX]; /* Datagram buffer */

	/* Set the size of the in-out parameter */
	clntLen = sizeof(echoClntAddr);

	if ((recvMsgSize = recvfrom(sock, echoBuffer, ECHOMAX, 0,
			(struct sockaddr *) &echoClntAddr, &clntLen)) < 0) {
		printf("Invalid input file name\n");
	} else {
		printf("Handling client %s\n", inet_ntoa(echoClntAddr.sin_addr));
		echoBuffer[recvMsgSize] = '\0';
		DIR = echoBuffer;
		bufferData();
	}
}

int main(int argc, char *argv[]) {
	struct sockaddr_in echoServAddr; /* Server address */
	unsigned short echoServPort; /* Server port */

	/* Test for correct number of parameters */
	if (argc != 2) {
		fprintf(stderr, "Usage:  %s <SERVER PORT>\n", argv[0]);
		exit(1);
	}

	echoServPort = atoi(argv[1]); /* First arg:  local port */

	/* Create socket for sending/receiving datagrams */
	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		printf("socket() failed");

	/* Set up the server address structure */
	memset(&echoServAddr, 0, sizeof(echoServAddr)); /* Zero out structure */
	echoServAddr.sin_family = AF_INET; /* Internet family */
	echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY ); /* Any incoming interface */
	echoServAddr.sin_port = htons(echoServPort); /* Port */

	/* Bind to the local address */
	if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
		printf("bind() failed");

	SIGIOHandler();
}
