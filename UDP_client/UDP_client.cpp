#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <iostream>
#include <vector>
#include <fstream>

#define MAX_FILE_NAME_LEN         255
#define TIMEOUT_SECS    5

using namespace std;

struct packet {
	/* Header */
	uint16_t cksum; /* Optional bonus part */
	uint16_t len;
	uint32_t seqno;
	/* Data */
	char data[500]; /* Not always 500 bytes, can be less */
};

/* Ack-only packets are only 8 bytes */
struct ack_packet {
	uint16_t cksum; /* Optional bonus part */
	uint16_t len;
	uint32_t ackno;
};

void DieWithError(char *errorMessage) {
	perror(errorMessage);
	exit(1);
}

void CatchAlarm(int ignored) /* Handler for SIGALRM */
{
}

int main(int argc, char *argv[]) {
	int sock;
	struct sockaddr_in echoServAddr;
	struct sockaddr_in fromAddr;
	unsigned short echoServPort;
	unsigned int fromSize;
	char *servIP;
	char *FileName; /* String to send to echo server */
	char echoBuffer[MAX_FILE_NAME_LEN + 1]; /* Buffer for echo string */
	int echoStringLen; /* Length of string to echo */
	int respStringLen; /* Size of received datagram */
	struct sigaction myAction;

	if ((argc < 4) || (argc > 5)) /* Test for correct number of arguments */
	{
		fprintf(stderr, "Incorrect arguments");
		exit(1);
	}

	servIP = argv[1]; /* First arg:  server IP address (dotted quad) */
	FileName = argv[2]; /* Second arg: File name */
	echoServPort = atoi(argv[3]); /* third argument: given port */

	if ((echoStringLen = strlen(FileName)) > MAX_FILE_NAME_LEN)
		DieWithError("Echo word too long");

	/* Create a best-effort datagram socket using UDP */
	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		DieWithError("socket() failed");

	/* Construct the server address structure */
	memset(&echoServAddr, 0, sizeof(echoServAddr));
	echoServAddr.sin_family = AF_INET;
	echoServAddr.sin_addr.s_addr = inet_addr(servIP); /* Server IP address */
	echoServAddr.sin_port = htons(echoServPort); /* Server port */

	/* Send the string to the server */
	if (sendto(sock, FileName, echoStringLen, 0,
			(struct sockaddr *) &echoServAddr, sizeof(echoServAddr))
			!= echoStringLen)
		DieWithError("sendto() sent a different number of bytes than expected");

	/* Get a response */

	fromSize = sizeof(fromAddr);

	//set alarm
	/* Set signal handler for alarm signal */
	myAction.sa_handler = CatchAlarm;
	if (sigfillset(&myAction.sa_mask) < 0) /* block everything in handler */
		DieWithError("sigfillset() failed");
	myAction.sa_flags = 0;
	if (sigaction(SIGALRM, &myAction, 0) < 0)
		DieWithError("sigaction() failed for SIGALRM");

	alarm(TIMEOUT_SECS); /* Set the timeout */

	//receive confirmation
	if ((respStringLen = recvfrom(sock, echoBuffer, MAX_FILE_NAME_LEN, 0,
			(struct sockaddr *) &fromAddr, &fromSize)) < 0) {
		if (errno == EINTR) /* Alarm went off  */
		{
			DieWithError("No Response");
		} else
			DieWithError("recvfrom() failed");
	}

	alarm(0);

	echoBuffer[respStringLen] = '\0';
	if (strcmp("CONFIRMED", echoBuffer)) {
		DieWithError("Error while receiving confirmation from server");
	}

	vector<packet*> v;
	uint32_t lastSeqno = 0;
	uint32_t lastLen = 0;
	struct packet* cur = (packet*) malloc(sizeof(packet));
	struct ack_packet* ack = (ack_packet*) malloc(sizeof(ack_packet));
	ack->len = sizeof(ack_packet);

	ofstream file(FileName, ios::out | ios::binary);

	while (recvfrom(sock, cur, MAX_FILE_NAME_LEN, 0,
			(struct sockaddr *) &fromAddr, &fromSize) >= 0) {
		// send ack
		ack->ackno = cur->seqno;
		sendto(sock, ack, sizeof(ack_packet), 0,
				(struct sockaddr *) &echoServAddr, sizeof(echoServAddr));
		if (cur->seqno == lastSeqno) {
			file.write(cur->data, cur->len);
			lastLen = cur->len;
			lastSeqno++;
			while (true) {
				packet* tmp = 0;
				for (int i = 0; i < v.size(); ++i)
					if (v[i]->seqno == lastSeqno) {
						tmp = v[i];
						v.erase(v.begin() + i);
					}
				if (tmp == 0) {
					break;
				}
				file.write(tmp->data, tmp->len);
				lastLen = tmp->len;
				lastSeqno++;
			}

			if (lastLen == 0) {
				break;
			}
		} else {
			// check if packet is duplicate
			bool isDuplicate = cur->seqno < lastSeqno; // if cur->seqno < lastSeqno discard (duplicate packet)
			for (int i = 0; i < v.size() && !isDuplicate; ++i) {
				if (v[i]->seqno == cur->seqno)
					isDuplicate = true;
			}
			if (!isDuplicate) {
				v.push_back(cur);
				cur = (packet*) malloc(sizeof(packet));
			}
		}
	}

	file.flush();
	file.close();
	close(sock);
	exit(0);
}
