#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>
#define BUFSIZE 4096

char *file_buf = (char*)malloc(BUFSIZE);
//char file_buf[BUFSIZE];
char err_check[BUFSIZE];
FILE *fp;
int udp_s, clntaddr_size, size_err = 0;
struct sockaddr_in clntaddr_tcp, clntaddr_udp;
pthread_mutex_t mutx = PTHREAD_MUTEX_INITIALIZER;

void *mergeFile(void *arg) {
	int sock = (long)arg;
	int i = 0, cnt = 0;
	pthread_mutex_lock(&mutx);
	*file_buf = 0;
	fp = fopen("Received_File", "a+b");
	fseek(fp, 0, SEEK_SET);
	printf("IP >> %s\nPort >> %d\n", inet_ntoa(clntaddr_udp.sin_addr), clntaddr_udp.sin_port);
	while(1) {
		fseek(fp, cnt, SEEK_CUR);
		if((i = recvfrom(udp_s, file_buf, BUFSIZE, 0, (struct sockaddr*)&clntaddr_udp, (socklen_t*)&clntaddr_size)) < 0) {
			perror("Recvfrom() Error!\n");
			break;
		}
		else if(!strncmp(file_buf, "Finish", 6)) break;
		cnt += i;
		fflush(stdout);
		if(fwrite(file_buf, 1, i, fp) < 0) perror("Fwrite() Error!\n");
		rewind(fp);
	}
	printf("UDP >> mergeFile() Finished!\n");
	pthread_mutex_unlock(&mutx);
}

void *connect_tcp(void *arg) {
	int sock = (long)arg;
	size_t file_len;
	pthread_t udp;
	FILE *chk_loss;

	pthread_mutex_lock(&mutx);
 	if((udp_s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) perror("udp socket not created in client\n");

	memset(&clntaddr_udp, 0, sizeof(clntaddr_udp));
	clntaddr_udp.sin_family = AF_INET;
	clntaddr_udp.sin_addr.s_addr = INADDR_ANY;
	clntaddr_udp.sin_port = htons(8815);
	clntaddr_size = sizeof(clntaddr_udp);

	if(sendto(udp_s, "Connected", 9, 0, (struct sockaddr*)&clntaddr_udp, (socklen_t)clntaddr_size) < 0) perror("Sendto()_Connected Error!\n");
	pthread_mutex_unlock(&mutx);

	if(pthread_create(&udp, NULL, mergeFile, (void *)sock) < 0) perror("UDP Thread is not created!\n");
	pthread_join(udp, (void**)&udp_s);

	pthread_mutex_lock(&mutx);
	send(sock, "Finish", 6, 0);
	clntaddr_size = sizeof(clntaddr_tcp);
	recv(sock, err_check, BUFSIZE, 0);
	printf("TCP >> %s\n", err_check);
	if(!strncmp(err_check, "Success", 7)) {
		printf("Successfully Received !!\n");
		size_err = 1;
	}
	else {
		printf("Receive Failed ..\n");
		size_err = 111;
	}
	pthread_mutex_unlock(&mutx);
}

int main(int argc, char **argv) {
	int tcp_s;
	pthread_t tcp;

	if(argc != 2) {
		printf("Usage : %s <SERVER_IP>\n", argv[0]);
		exit(1);
	}

	if((tcp_s = socket(AF_INET, SOCK_STREAM, 0)) == -1) perror("tcp socket not created in client\n");

	memset(&clntaddr_tcp, 0, sizeof(clntaddr_tcp));
	clntaddr_tcp.sin_family = AF_INET;
	clntaddr_tcp.sin_addr.s_addr = inet_addr(argv[1]);
	clntaddr_tcp.sin_port = htons(5518);

	clntaddr_size = sizeof(clntaddr_tcp);

	if(connect(tcp_s, (struct sockaddr*)&clntaddr_tcp, clntaddr_size) == -1) perror("connect() Error!\n");

	while(1) {
		if(pthread_create(&tcp, NULL, connect_tcp, (void *)tcp_s) < 0) perror("TCP Thread is not created!\n");
		pthread_join(tcp, (void**)&tcp_s);
		close(udp_s);
		if(size_err == 1) break;
	}
	close(tcp_s);
	return(0);
}
