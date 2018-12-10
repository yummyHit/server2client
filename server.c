#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>
#include <pthread.h>
#define BUFSIZE 4088

char **file_buf;
FILE *fp;
struct sockaddr_in servaddr_udp;
pthread_mutex_t mutx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cnd = PTHREAD_COND_INITIALIZER;

int sendto_simul(int s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
int send_simul(int s, const void *buf, size_t len, int flags);

typedef struct {
	int fileSize;
	int dataSize;
	char *fileName;
} dataInfo;

dataInfo *info;

void *connect_udp(void *arg) {
	int sock = (long)arg;
	int i = 0, j = 0, cnt = 0, cnt_sum = 0, tmp;
	char ready_buf[10], read_buf[4088];
	int servaddr_size = sizeof(servaddr_udp);

	recvfrom(sock, ready_buf, sizeof(ready_buf), 0, (struct sockaddr*)&servaddr_udp, (socklen_t*)&servaddr_size);
	if(!strncmp(ready_buf, "Connected", 9)) printf("UDP Client >> %s\n", ready_buf);
	else perror("UDP Client not connected!\n");
	usleep(100);
	file_buf = (char**)malloc(info->dataSize);
	printf("Sending...\n");
	while(1) {
		tmp = 1;
		if(i >= 10) {
			j = i;
			for(j; j >= 10;) {
				j /= 10;
				tmp++;
			}
		}
		rewind(fp);
		fseek(fp, cnt_sum, SEEK_CUR);
		cnt = fread(read_buf, 1, info->dataSize, fp);
		cnt_sum += cnt;
		*(file_buf+i) = (char*)malloc(info->dataSize+(tmp+2));
		sprintf(*(file_buf+i), "[%d]%s", i, read_buf);
//		nanosleep((const struct timespec[]){{0, 500}}, NULL);
		usleep(1);
		if(cnt_sum >= info->fileSize) {
			printf("This is last packet\ncnt_sum = %d, info->fileSize = %d\n", cnt_sum, info->fileSize);
			sendto_simul(sock, *(file_buf+i), cnt+(tmp+2), 0, (struct sockaddr*)&servaddr_udp, (socklen_t)servaddr_size);
			usleep(100);
			sendto_simul(sock, "finish", 6, 0, (struct sockaddr*)&servaddr_udp, (socklen_t)servaddr_size);
			sendto_simul(sock, "finish", 6, 0, (struct sockaddr*)&servaddr_udp, (socklen_t)servaddr_size);
			sendto_simul(sock, "finish", 6, 0, (struct sockaddr*)&servaddr_udp, (socklen_t)servaddr_size);
			break;
		}
		else sendto_simul(sock, *(file_buf+i), info->dataSize+(tmp+2), 0, (struct sockaddr*)&servaddr_udp, (socklen_t)servaddr_size);
		i++;
	}
	pthread_cond_signal(&cnd);
	printf("udp_thread is done!\n");
}

void *connect_tcp(void *arg) {
	int sock = (long)arg;
	int i, tmp, cnt, chk = -1;
	char recv_msg[10];

	memset(recv_msg, 0, 10);
	recv(sock, recv_msg, sizeof(recv_msg), 0);
	printf("TCP Client >> %s\n", recv_msg);
	pthread_mutex_lock(&mutx);
	info->dataSize = BUFSIZE;
	fp = fopen(info->fileName, "rb");
	fseek(fp, 0, SEEK_END);
	info->fileSize = ftell(fp);
	pthread_mutex_unlock(&mutx);
	pthread_cond_wait(&cnd, &mutx);
	while(1) {
		tmp = 0; cnt = 1;
		recv(sock, recv_msg, sizeof(recv_msg), 0);
		recv_msg[strlen(recv_msg)] = '\0';
		if(!strncmp(recv_msg, "finish", 6)) break;
		for(i = 0; i < strlen(recv_msg); i++) {
			tmp *= 10;
			tmp += recv_msg[i] - '0';
		}
		if(chk == tmp) continue;
		chk = tmp;
		if(tmp >= 10) {
			i = tmp;
			for(i; i >= 10;) {
				i /= 10;
				cnt++;
			}
		}
		send(sock, *(file_buf+tmp), info->dataSize+(cnt+2), 0);
	}
	printf("tcp_thread is done!\n");
}

int main(int argc, char **argv) {
	struct sockaddr_in servaddr_tcp, clntaddr;
	int tcp_s, udp_s, clnt_s, clntaddr_size;
	pthread_t tcp, udp;

	if(argc != 4) {
		printf("Usage : %s <file_name> <tcp_port> <udp_port>\n\tex)./server test.mp4 8888 7777\n\tIt must different number between tcp port and udp port.\n", argv[0]);
		exit(1);
	}
	info = (dataInfo*)malloc(sizeof(dataInfo));
	info->fileName = argv[1];

	srand((unsigned)time(NULL));	

	tcp_s = socket(AF_INET, SOCK_STREAM, 0);
	memset(&servaddr_tcp, 0, sizeof(servaddr_tcp));
	servaddr_tcp.sin_family = AF_INET;
	servaddr_tcp.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr_tcp.sin_port = htons(atoi(argv[2]));

	bind(tcp_s, (struct sockaddr*)&servaddr_tcp, sizeof(servaddr_tcp));
	listen(tcp_s, 5);

	clntaddr_size = sizeof(clntaddr);
	clnt_s = accept(tcp_s, (struct sockaddr*)&clntaddr, (socklen_t*)&clntaddr_size);
	printf("###Client accept Successfully!!###\n");
	pthread_create(&tcp, NULL, connect_tcp, (void *)clnt_s);

	udp_s = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&servaddr_udp, 0, sizeof(servaddr_udp));
	servaddr_udp.sin_family = AF_INET;
	servaddr_udp.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr_udp.sin_port = htons(atoi(argv[3]));

	bind(udp_s, (struct sockaddr*)&servaddr_udp, sizeof(servaddr_udp));
	pthread_create(&udp, NULL, connect_udp, (void *)udp_s);

	pthread_join(tcp, (void**)&clnt_s);
	close(tcp_s);
	pthread_detach(udp);
	close(udp_s);
	fclose(fp);
	printf("File transfer is done!\n");
	pthread_mutex_destroy(&mutx);
	pthread_cond_destroy(&cnd);
	return(0);
}

int sendto_simul(int s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    int packet_len = 0;
    if (rand() % 20 != 1) packet_len = sendto(s, buf, len, flags, to, tolen);
    return packet_len;
}

int send_simul(int s, const void *buf, size_t len, int flags) {
	int packet_len = 0;
	if (rand() % 20 != 1) packet_len = send(s, buf, len, flags);
	return packet_len;
}
