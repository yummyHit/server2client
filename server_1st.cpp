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
char *file_name = (char*)malloc(BUFSIZE);
FILE *fp;
int udp_s, clntaddr_size, file_len, size_err = 0, divLen = 0;
struct sockaddr_in clntaddr;
pthread_mutex_t mutx = PTHREAD_MUTEX_INITIALIZER;

void *divFile(void *arg) {
	int sock = (long)arg;
	int cnt = 0;
	pthread_mutex_lock(&mutx);
	*file_buf = 0;
	fseek(fp, 0, SEEK_SET);
	while(1) {
		fseek(fp, divLen, SEEK_CUR);
		if((cnt = fread(file_buf, 1, BUFSIZE, fp)) < 0) perror("Fread() Error!\n");
		if(sendto(udp_s, file_buf, cnt, 0, (struct sockaddr*)&clntaddr, (socklen_t)clntaddr_size) < 0) perror("Sendto() Error!\n");
		divLen += cnt;
		printf("file_Len : %d & divLen : %d\n", file_len, divLen);
		printf("UDP >> file_buf : %s\nUDP >> cursor : %d\n", file_buf, cnt);
		if(divLen >= file_len) break;
		rewind(fp);
	}
	sendto(udp_s, "Finish", 6, 0, (struct sockaddr*)&clntaddr, (socklen_t)clntaddr_size);
	printf("UDP >> divFile() Finished! divLen is %d\n", divLen);
	pthread_mutex_unlock(&mutx);
}

void *connect_tcp(void *arg) {
	int sock = (long)arg;
	struct sockaddr_in servaddr_udp;
	pthread_t udp;
	FILE *chk_loss;

 	if((udp_s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) perror("udp socket not created in server\n");

	memset(&servaddr_udp, 0, sizeof(servaddr_udp));
	servaddr_udp.sin_family = AF_INET;
	servaddr_udp.sin_addr.s_addr = INADDR_ANY;
	servaddr_udp.sin_port = htons(8815);
	
	if(bind(udp_s, (struct sockaddr*)&servaddr_udp, sizeof(servaddr_udp)) == -1) perror("UDP Bind() Error!\n");
	clntaddr.sin_port = servaddr_udp.sin_port;

	pthread_mutex_lock(&mutx);
	if(recvfrom(udp_s, file_buf, BUFSIZE, 0, (struct sockaddr*)&clntaddr, (socklen_t*)&clntaddr_size) < 0) perror("Recvfrom()_Connected Error!\n");
	else if(!strncmp(file_buf, "Connected", 9)) printf("UDP Client >> %s\n", file_buf);
	else perror("UDP Client not connected!\n");

	if((fp = fopen(file_name, "rb")) == NULL) perror("File does not exist!\n");
	chk_loss = fp;
	pthread_mutex_unlock(&mutx);

	fseek(chk_loss, 0, SEEK_END);
	file_len = ftell(chk_loss);
	printf("TCP >> file_len is %d\n", file_len);
	
	if(pthread_create(&udp, NULL, divFile, (void *)sock) < 0) perror("UDP Thread is not created!\n");
	pthread_join(udp, (void**)&udp_s);

	pthread_mutex_lock(&mutx);
	fclose(fp);
	recv(sock, file_buf, BUFSIZE, 0);
	if(!strncmp(file_buf, "Finish", 6)) {
		if(divLen != file_len) {
			printf("File size is not correct!\n");
			send(sock, "Failed", 6, 0); 
			size_err = 111;
		}
		else {
			printf("Successfully finished!\n");
			send(sock, "Success", 7, 0);
			size_err = 1;
		}
	}
	pthread_mutex_unlock(&mutx);
}

int main(int argc, char **argv) {
	struct sockaddr_in servaddr_tcp;
	int clnt_s, tcp_s;
	pthread_t tcp;

	if(argc != 2) {
		printf("Usage : %s <file_name>\n", argv[0]);
		exit(1);
	}
	
	file_name = argv[1];

	if((tcp_s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("tcp socket not created in server\n");
		exit(1);
	}

	memset(&servaddr_tcp, 0, sizeof(servaddr_tcp));
	servaddr_tcp.sin_family = AF_INET;
	servaddr_tcp.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr_tcp.sin_port = htons(5518);

	if(bind(tcp_s, (struct sockaddr*)&servaddr_tcp, sizeof(servaddr_tcp)) == -1) {
		perror("TCP Bind() Error!\n");
		exit(1);
	}
	if(listen(tcp_s, 5) == -1) {
		perror("Listen() Error!\n");
		exit(1);
	}

	clntaddr_size = sizeof(clntaddr);
	if((clnt_s = accept(tcp_s, (struct sockaddr *)&clntaddr, (socklen_t*)&clntaddr_size)) == -1) perror("Accept() Error!\n");
	printf("###Client accept Success!!###\n");
	while(1) {
		if(pthread_create(&tcp, NULL, connect_tcp, (void *)clnt_s) < 0) perror("TCP Thread is not created!\n");
		pthread_join(tcp, (void**)&clnt_s);
		close(udp_s);
		if(size_err == 1) break;
	}
	close(tcp_s);
	return(0);
}
