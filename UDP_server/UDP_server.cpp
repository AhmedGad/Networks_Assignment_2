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
#include <vector>
#include <time.h>       /* time */

#define ECHOMAX 255     /* Longest string to echo */
#define MAX_WINDOW_SIZE 128
#define TIMEOUT_THRESHOLD (.01)

using namespace std;

struct packet {
	/* Header */
	uint16_t len;
	uint32_t seqno;
	/* Data */
	char data[1024];
};

/* Ack-only packets are only 8 bytes */
struct ack_packet {
	uint16_t len;
	uint32_t ackno;
};

int sock; /* Socket -- GLOBAL for signal handler */
int window_size; /* current growing window size */
struct sockaddr_in echoClntAddr; /* Address of datagram source */
char *DIR;
double ppl; /* probability fo packet loss*/
struct sockaddr_in echoServAddr; /* Server address */
unsigned short echoServPort; /* Server port */

time_t time_outs[MAX_WINDOW_SIZE];
packet filePackets[MAX_WINDOW_SIZE];
bool ackedPackets[MAX_WINDOW_SIZE];
unsigned int clntLen;
uint32_t base;
uint32_t nextPacketSeqNumber;
ifstream myFile;

void checkAckReceived() {
	ack_packet ackPacket;
	if (recvfrom(sock, &ackPacket, sizeof(ack_packet), MSG_DONTWAIT,
			(struct sockaddr *) &echoClntAddr, &clntLen) > 0) {
		if (ackPacket.ackno >= base) {
			ackedPackets[ackPacket.ackno % MAX_WINDOW_SIZE] = true;
		}
	}
}

void checkPacketLoss() {

	time_t currentTime;
	for (int i = 0; i < window_size && base + i < nextPacketSeqNumber; i++) {
		int currentIndex = (base + i) % (MAX_WINDOW_SIZE);
		if (!ackedPackets[currentIndex]) {
			time(&currentTime);

			if (difftime(currentTime,
					time_outs[currentIndex]) >= TIMEOUT_THRESHOLD) {
				sendto(sock, &(filePackets[currentIndex]), sizeof(packet), 0,
						(struct sockaddr *) &echoClntAddr,
						sizeof(echoClntAddr));

				// reset timer
				time(&currentTime);
				time_outs[currentIndex] = currentTime;
			}
		}
	}
}

void advanceWindow() {
	while (ackedPackets[base % MAX_WINDOW_SIZE]) {
		ackedPackets[base % (MAX_WINDOW_SIZE)] = false;
		base++;

		// send another packet
		if (myFile.good()) {
			packet currPacket;
			currPacket.len = myFile.read(currPacket.data,
					sizeof(currPacket.data)).gcount();
			currPacket.seqno = nextPacketSeqNumber;
			if (((double) rand() / RAND_MAX) > ppl) {
				sendto(sock, &currPacket, sizeof(packet), 0,
						(struct sockaddr *) &echoClntAddr,
						sizeof(echoClntAddr));
			}
			filePackets[nextPacketSeqNumber % MAX_WINDOW_SIZE] = currPacket;
			nextPacketSeqNumber++;
		}
	}
}

void selectiveRepeat() {

	// sending packets
	for (int index = 0; myFile.good() && index < window_size; index++) {
		packet currentPacket;
		currentPacket.len = myFile.read(currentPacket.data,
				sizeof(currentPacket.data)).gcount();
		currentPacket.seqno = nextPacketSeqNumber;
		if (((double) rand() / RAND_MAX) > ppl) {
			sendto(sock, &currentPacket, sizeof(currentPacket), 0,
					(struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr));
		}
		filePackets[nextPacketSeqNumber % MAX_WINDOW_SIZE] = (currentPacket);
		nextPacketSeqNumber++;
	}

	while (true) {
		advanceWindow();
		checkAckReceived();
		checkPacketLoss();

		// EOF reached
		if (!myFile.good() && base == nextPacketSeqNumber)
			break;
	}
}

void stopAndWait() {
	window_size = 1;
	selectiveRepeat();
}

void bufferData() {
	clntLen = sizeof(echoClntAddr);
	myFile.open(DIR, ifstream::in | ios::in | ios::binary);

	memset(ackedPackets, 0, sizeof(ackedPackets));
	for (int i = 0; i < MAX_WINDOW_SIZE; i++) {
		time_t initialTime;
		time(&initialTime);
		time_outs[i] = initialTime;
	}

	selectiveRepeat();
//	stopAndWait();

// Send last packet with data length =  0 to inform the client to close
	packet lastPacket;
	lastPacket.len = 0;
	lastPacket.seqno = nextPacketSeqNumber;
	sendto(sock, &lastPacket, sizeof(packet), 0,
			(struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr));
	myFile.close();
	exit(0);
}

void Handler() {
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

void initServer() {
	freopen("server.in", "r", stdin);

	scanf("%u", &echoServPort); // port no

	scanf("%d", &window_size); //initial window size

	unsigned int seed_rand;
	scanf("%u", &seed_rand); // random number generator seed value
	srand(seed_rand);

	scanf("%lf", &ppl); //probability of packet loss

	fclose(stdin);

	base = 0;
	window_size = min(MAX_WINDOW_SIZE, window_size);
	nextPacketSeqNumber = 0;
}

int main(int argc, char *argv[]) {

	initServer();

	/* Create socket for sending/receiving datagrams */
	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		printf("socket() failed\n");

	/* Set up the server address structure */
	memset(&echoServAddr, 0, sizeof(echoServAddr)); /* Zero out structure */
	echoServAddr.sin_family = AF_INET; /* Internet family */
	echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY ); /* Any incoming interface */
	echoServAddr.sin_port = htons(echoServPort); /* Port */

	/* Bind to the local address */
	if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
		printf("bind() failed");

	Handler();
}
