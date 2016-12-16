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
char *udp_argv = (char*)malloc(BUFSIZE);
char *serv_ip = (char*)malloc(BUFSIZE);
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
	fp = fopen("Received_File.mp4", "a+b");
	fseek(fp, 0, SEEK_SET);
	printf("IP >> %s\nPort >> %d\n", inet_ntoa(clntaddr_udp.sin_addr), clntaddr_udp.sin_port);
	while(1) {
		fseek(fp, cnt, SEEK_CUR);
		i = recvfrom(udp_s, file_buf, BUFSIZE, 0, (struct sockaddr*)&clntaddr_udp, (socklen_t*)&clntaddr_size);
		if(!strncmp(file_buf, "Finish", 6)) break;
		cnt += i;
		fflush(stdout);
		fwrite(file_buf, 1, i, fp);
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

 	udp_s = socket(AF_INET, SOCK_DGRAM, 0);

	memset(&clntaddr_udp, 0, sizeof(clntaddr_udp));
	clntaddr_udp.sin_family = AF_INET;
	clntaddr_udp.sin_addr.s_addr = clntaddr_tcp.sin_addr.s_addr;
	clntaddr_udp.sin_port = htons(atoi(udp_argv));
	clntaddr_size = sizeof(clntaddr_udp);

	usleep(700);
	sendto(udp_s, "Connected", 9, 0, (struct sockaddr*)&clntaddr_udp, (socklen_t)clntaddr_size);

	pthread_create(&udp, NULL, mergeFile, (void *)sock);
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

	if(argc != 4) {
		printf("Usage : %s <SERVER_IP> <tcp_port> <udp_port>\n", argv[0]);
		exit(1);
	}

	udp_argv = argv[3];

	tcp_s = socket(AF_INET, SOCK_STREAM, 0);

	memset(&clntaddr_tcp, 0, sizeof(clntaddr_tcp));
	clntaddr_tcp.sin_family = AF_INET;
	clntaddr_tcp.sin_addr.s_addr = inet_addr(argv[1]);
	clntaddr_tcp.sin_port = htons(atoi(argv[2]));

	clntaddr_size = sizeof(clntaddr_tcp);

	connect(tcp_s, (struct sockaddr*)&clntaddr_tcp, clntaddr_size);

	while(1) {
		pthread_create(&tcp, NULL, connect_tcp, (void *)tcp_s);
		pthread_join(tcp, (void**)&tcp_s);
		close(udp_s);
		if(size_err == 1) break;
	}
	close(tcp_s);
	return(0);
}
