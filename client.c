#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <pthread.h>
#define BUFSIZE 4088

char **file_buf;
int *loss_num;
int last_num = 0;
double operating_time;
FILE *fp;
struct sockaddr_in clntaddr_udp;
struct timeval start_point, end_point;
pthread_mutex_t mutx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cnd = PTHREAD_COND_INITIALIZER;

int send_simul(int s, const void *buf, size_t len, int flags);

typedef struct {
	int fileSize;
	int dataSize;
} dataInfo;

dataInfo *info;

void *connect_udp(void *arg) {
	int sock = (long)arg;
	int i = 0, j = 0, cnt = 0, bracket = 0, tmp, size;
	char send_buf[10];
	int clntaddr_size = sizeof(clntaddr_udp);

	usleep(50);
	sendto(sock, "Connected", 9, 0, (struct sockaddr*)&clntaddr_udp, (socklen_t)clntaddr_size);
	printf("Receiving...\n");
	file_buf = (char**)malloc(info->dataSize);
	while(1) {
		size = 1; tmp = 0;
		if(i >= 10) {
			cnt = i;
			for(cnt; cnt >= 10;) {
				cnt /= 10;
				size++;
			}
		}
		*(file_buf+i) = (char*)malloc(info->dataSize+(size+2));
		recvfrom(sock, *(file_buf+i), info->dataSize+(size+2), 0, (struct sockaddr*)&clntaddr_udp, (socklen_t*)&clntaddr_size);
		for(cnt = 1; cnt < 10; cnt++) {
			if(*(*(file_buf+i)+cnt) == ']') {
				bracket = cnt;
				break;
			}
			tmp *= 10;
			tmp += *(*(file_buf+i)+cnt) - '0';
		}
		if(!strncmp(*(file_buf+i), "finish", 6)) {
			printf("I got a finish msg from server!\n");
			pthread_cond_signal(&cnd);
			last_num = i;
			break;
		}
		else if(i != tmp) {
			*(file_buf+tmp) = (char*)malloc(info->dataSize+(size+2));
			for(cnt = 0; cnt < info->dataSize; cnt++) *(*(file_buf+tmp)+cnt) = *(*(file_buf+i)+cnt+bracket+1);
			memset(*(file_buf+i), 0, info->dataSize+(size+2));
			while(i < tmp) *(loss_num+(j++)) = i++;
		}
		else for(cnt = 0; cnt < info->dataSize; cnt++) *(*(file_buf+i)+cnt) = *(*(file_buf+i)+cnt+bracket+1);
		i++;
	}
	printf("udp_thread is done! last_num = %d\n", i);
}

void *connect_tcp(void *arg) {
	int sock = (long)arg;
	int i, j = 0, bracket = 0, tmp, loss, cnt = -1;
	char recv_msg[10];

	memset(recv_msg, 0, sizeof(recv_msg));
	send(sock, "Connected", 9, 0);
	pthread_mutex_lock(&mutx);
	info = (dataInfo*)malloc(sizeof(dataInfo));
	info->dataSize = BUFSIZE;
	loss_num = (int*)malloc(sizeof(int) * BUFSIZE);
	pthread_mutex_unlock(&mutx);
	pthread_cond_wait(&cnd, &mutx);
	while(1) {
		loss = *(loss_num+(j++));
		tmp = 1;
		if(loss >= 10) {
			i = loss;
			for(i; i >= 10;) {
				i /= 10;
				tmp++;
			}
		}
		sprintf(recv_msg, "%d", loss);
		send(sock, recv_msg, sizeof(recv_msg), 0);
		*(file_buf+loss) = (char*)malloc(info->dataSize+(tmp+2));
//		memset(*(file_buf+loss), 0, info->dataSize+(tmp+2));
		recv(sock, *(file_buf+loss), info->dataSize+(tmp+2), 0);
		for(i = 1; i < 10; i++)
			if(*(*(file_buf+loss)+i) == ']') {
				bracket = i;
				break;
			}
		for(i = 0; i < info->dataSize; i++) *(*(file_buf+loss)+i) = *(*(file_buf+loss)+i+bracket+1);
		if(*(loss_num+j) >= last_num || *(loss_num+j) == 0) {
			send(sock, "finish", 6, 0);
			break;
		}
	}
	printf("File writing..\n");
	fp = fopen("Received_file.mp4", "a+b");
	for(i = 0; i < last_num; i++) fwrite(*(file_buf+i), 1, info->dataSize, fp);
	printf("tcp_thread is done!\n");
}

int main(int argc, char **argv) {
	struct sockaddr_in clntaddr_tcp;
	int tcp_s, udp_s;
	pthread_t tcp, udp, file;
	srand((unsigned)time(NULL));
	gettimeofday(&start_point, NULL);

	if(argc != 4) {
		printf("Usage : %s <server_ip> <tcp_port> <udp_port>\n\tex)./client 127.0.0.1 8888 7777\n\tIt must different number between tcp port and udp port.\n", argv[0]);
		exit(1);
	}

	tcp_s = socket(AF_INET, SOCK_STREAM, 0);

	memset(&clntaddr_tcp, 0, sizeof(clntaddr_tcp));
	clntaddr_tcp.sin_family = AF_INET;
	clntaddr_tcp.sin_addr.s_addr = inet_addr(argv[1]);
	clntaddr_tcp.sin_port = htons(atoi(argv[2]));

	connect(tcp_s, (struct sockaddr*)&clntaddr_tcp, sizeof(clntaddr_tcp));
	printf("###Connected Successfully!!###\n");

	pthread_create(&tcp, NULL, connect_tcp, (void *)tcp_s);

	udp_s = socket(AF_INET, SOCK_DGRAM, 0);

	memset(&clntaddr_udp, 0, sizeof(clntaddr_udp));
	clntaddr_udp.sin_family = AF_INET;
	clntaddr_udp.sin_addr.s_addr = inet_addr(argv[1]);
	clntaddr_udp.sin_port = htons(atoi(argv[3]));

	pthread_create(&udp, NULL, connect_udp, (void *)udp_s);

	pthread_join(tcp, (void**)&tcp_s);
	close(tcp_s);
	pthread_detach(udp);
	close(udp_s);
	fclose(fp);
	printf("File transfer is done!\n");
	pthread_mutex_destroy(&mutx);
	pthread_cond_destroy(&cnd);
	gettimeofday(&end_point, NULL); 
	operating_time = (double)(end_point.tv_sec)+(double)(end_point.tv_usec)/1000000.0-(double)(start_point.tv_sec)-(double)(start_point.tv_usec)/1000000.0;
	printf("server send time %f\n",operating_time);
	return(0);
}

int send_simul(int s, const void *buf, size_t len, int flags) {
	int packet_len = 0;
	if (rand() % 20 != 1) packet_len = send(s, buf, len, flags);
	return packet_len;
}
