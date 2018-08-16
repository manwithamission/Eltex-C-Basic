
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "message.pb-c.h"

#define MAX_MSG_SIZE 20
#define RCVBUFSIZE 20
#define BROADCASTPORTCLIENTSENDER 2001
#define BROADCASTPORTCLIENTRECEIVER 2002
#define TCPPORTCLIENTSENDER 2500
#define TCPPORTCLIENTRECEIVER 2501

struct mesg_buffer {
	long mesg_type;
	uint8_t mesg_text[20];
} message;

int msgid;
int length;
struct msqid_ds msqid_ds, *buf;

char *broadcastaddress;

void Error(char *errorMessage) {
	perror(errorMessage);
	exit(1);
}

int queue() {

	key_t key;
	int msgid;
	key = ftok(".", 65);

	msgid = msgget(key, 0666 | IPC_CREAT);
	message.mesg_type = 1;

	return msgid;
}

int CreateTCPServerSocket(unsigned short port) {
	int serversocket;
	struct sockaddr_in addr;

	if ( (serversocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 ) {
		Error("socket error");
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port        = htons(port);

	if ( bind(serversocket, (struct sockaddr *) &addr, sizeof(addr)) < 0 ) {
		Error("bind error");
	}

	if ( listen(serversocket, 5) < 0 ) {
		Error("listen error");
	}

	return serversocket;
}

void *UdpBroadcastSenderForClientSender(void *arg) {

	int sock;
	struct sockaddr_in broadcastAddr;
	unsigned short broadcastPort;
	const char *sendString = "Жду сообщений";
	int broadcastPermission;
	unsigned int sendStringLen;

	buf = &msqid_ds;
	msgctl(msgid, IPC_STAT, buf);

	broadcastPort = htons(BROADCASTPORTCLIENTSENDER);

	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		Error("socket error");
	}

	broadcastPermission = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void *) &broadcastPermission, sizeof(broadcastPermission)) < 0) {
		Error("setsockopt error");
	}

	memset(&broadcastAddr, 0, sizeof(broadcastAddr));
	broadcastAddr.sin_family = AF_INET;
	broadcastAddr.sin_addr.s_addr = inet_addr(broadcastaddress);
	broadcastAddr.sin_port = broadcastPort;
	sendStringLen = strlen(sendString);
	
	while (1) {
		sleep(1);
		// printf("Сообщений в очереди UDP для клиента-отправителя %ld\n", buf->msg_qnum);
		if (buf->msg_qnum < 1) {
			sleep(1);
			if (sendto(sock, sendString, sendStringLen, 0, (struct sockaddr *) &broadcastAddr, sizeof(broadcastAddr)) != sendStringLen) {
				Error("sendto()");
			}
		}
	}
}

void *UdpBroadcastSenderForClientReceiver(void *arg) {

	int sock;
	struct sockaddr_in broadcastAddr;
	unsigned short broadcastPort;
	const char *sendString = "Есть сообщения";
	int broadcastPermission;
	unsigned int sendStringLen;

	buf = &msqid_ds;
	msgctl(msgid, IPC_STAT, buf);

	broadcastPort = htons(BROADCASTPORTCLIENTRECEIVER);

	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		Error("socket()");
	}

	broadcastPermission = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void *) &broadcastPermission, sizeof(broadcastPermission)) < 0) {
		Error("setsockopt()");
	}

	memset(&broadcastAddr, 0, sizeof(broadcastAddr));
	broadcastAddr.sin_family = AF_INET;
	broadcastAddr.sin_addr.s_addr = inet_addr(broadcastaddress);
	broadcastAddr.sin_port = broadcastPort;
	sendStringLen = strlen(sendString);
	
	while (1) {
		sleep(1);
		if (buf->msg_qnum > 0) {
			sleep(1);
			if (sendto(sock, sendString, sendStringLen, 0, (struct sockaddr *) &broadcastAddr, sizeof(broadcastAddr)) != sendStringLen) {
				Error("sendto()");
			}
		}
	}
}

void *TcpConnectionClientSender(void *arg) {
	DMessage *msg;		// DMessage using submessages
	Submessage *sub1;	// Submessages

	int serversocket;
	int clientsocket;
	unsigned short serverport;
	struct sockaddr_in echoClntAddr;
	unsigned int clntLen;
	clntLen = sizeof(echoClntAddr);
	int recvMsgSize;
	
	buf = &msqid_ds;

	uint8_t buffer[MAX_MSG_SIZE]; // Input data container for bytes
	
	serverport = TCPPORTCLIENTSENDER;

	serversocket = CreateTCPServerSocket(serverport);

	while (1) {
		if ((clientsocket = accept(serversocket, (struct sockaddr *) &echoClntAddr, &clntLen)) < 0) {
			Error("accept()");
		}
		printf("Жду сообщений от клиента-отправителя\n");

		while (1) {
			if ((recvMsgSize = recv(clientsocket, buffer, RCVBUFSIZE, 0)) < 0) {
				Error("recv() failed");
			}
			if (recvMsgSize > 0) {
				// printf("%s\n", buffer);
				msg = dmessage__unpack(NULL, RCVBUFSIZE, buffer); // Deserialize the serialized input
				if (msg == NULL){ // Something failed
					fprintf(stderr,"error unpacking incoming message\n");
				}
				sub1 = msg->a;
				strcpy(message.mesg_text, sub1->value);
				printf("%s\n", message.mesg_text);
				message.mesg_type = 1;
				msgsnd(msgid, &message, length, 1);
				msgctl(msgid, IPC_STAT, buf);
				printf("Сообщений в очереди %ld\n", buf->msg_qnum);
				dmessage__free_unpacked(msg,NULL);
			} 
			if (recvMsgSize == 0) {
				// msgctl(msgid, IPC_STAT, buf);
				// printf("%ld\n", buf->msg_qnum);
				break;
			}
		}
	}
}

void *TcpConnectionClientReceiver(void *arg) {
	int serversocket;
	int clientsocket;
	unsigned short serverport;
	struct sockaddr_in echoClntAddr;
	unsigned int clntLen;
	clntLen = sizeof(echoClntAddr);
	int i = 1;
	
	buf = &msqid_ds;

	DMessage msg    = DMESSAGE__INIT;   // DMESSAGE
	Submessage sub1 = SUBMESSAGE__INIT; // SUBMESSAGE A
	void *bufstring;
	unsigned len;
	
	serverport = TCPPORTCLIENTRECEIVER;

	serversocket = CreateTCPServerSocket(serverport);

	while (1) {
		printf("Жду подключения клиента-получателя\n");
		if ((clientsocket = accept(serversocket, (struct sockaddr *) &echoClntAddr, &clntLen)) < 0) {
			Error("accept() failed");
		}
		while (1) {
			msgctl(msgid, IPC_STAT, buf);
			if (buf->msg_qnum >= 0) {
				sleep(3);
				if (msgrcv(msgid, &message, length, message.mesg_type, 0) < 0) {
					Error("msgrecv()");
				}
				sub1.value = message.mesg_text;
				msg.a = &sub1;
				len = dmessage__get_packed_size(&msg);		// This is the calculated packing length
				bufstring = malloc(len);					// Allocate memory
				dmessage__pack(&msg, bufstring);			// Pack msg, including submessages
				// printf("%ld\n", buf->msg_qnum);
				if (send(clientsocket, bufstring, len, 0) < 0) {
					Error("send()");
				}
				printf("[%d]Отправлено клиенту-получателю:%p %s %d\n", i, bufstring, sub1.value, len);
				// printf("[%d]Отправлено клиенту-получателю:%s\n", i, message.mesg_text);
				free(bufstring);
				i++;
			}
		}
		msgctl(msgid, IPC_RMID, 0);
	}
}

int main(int argc, char *argv[]) {
	msgid = queue();
	length = sizeof(message) - sizeof(long);
	int threadcount = 4;
	pthread_t threads[threadcount];
	void *status[threadcount];
	
	broadcastaddress = (char *) malloc(sizeof(char)*15);
	broadcastaddress = "127.0.0.1";

	pthread_create(&threads[1], NULL, UdpBroadcastSenderForClientSender, 0);
	pthread_create(&threads[2], NULL, TcpConnectionClientSender, 0);
	pthread_create(&threads[3], NULL, UdpBroadcastSenderForClientReceiver, 0);
	pthread_create(&threads[4], NULL, TcpConnectionClientReceiver, 0);

	pthread_join(threads[1], &status[1]);
	pthread_join(threads[2], &status[2]);
	pthread_join(threads[3], &status[3]);
	pthread_join(threads[4], &status[4]);
}