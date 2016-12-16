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
#define BUFSIZE 1457

char **file_buf;
int chkcnt = 0, loss_num = 0, last_size = 0;
bool loss_chk = false, chk_fin = false, all_fin = false;
struct sockaddr_in clntaddr_udp;
pthread_mutex_t mutx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cnd = PTHREAD_COND_INITIALIZER;

typedef struct {
	bool unused;
	int fileSize;
	int mtuSize;
	int dataSize;
	char fileName[256];
} dataInfo;

dataInfo *info = (dataInfo*)malloc(sizeof(dataInfo));

void *file_thread(void *arg) {
	int sock = (long)arg;
	int i = 0, cnt = 0, cnt_sum = 0;
	FILE *fp;
	pthread_cond_wait(&cnd, &mutx);
	info->mtuSize = 1500;
	info->dataSize = BUFSIZE;
	pthread_mutex_unlock(&mutx);
	if(strncmp(info->fileName, "recv", 4) != 0) {
		info->fileName[strlen(info->fileName) - 1] = '\0';
		fp = fopen(info->fileName, "rb");
		fseek(fp, 0, SEEK_END);
		info->fileSize = ftell(fp);
		rewind(fp);
		file_buf = (char**)malloc(sizeof(char*) * info->dataSize);
		pthread_cond_broadcast(&cnd);
		while(1) {
			fseek(fp, cnt_sum, SEEK_CUR);
			pthread_mutex_lock(&mutx);
			*(file_buf+i) = (char*)malloc(sizeof(char) * info->dataSize);
			if((cnt = fread(*(file_buf+(i++)), 1, info->dataSize, fp)) < 0) perror("fread() Error!\n");
			chkcnt++;
			cnt_sum += cnt;
			printf("i = %d, cnt = %d, cnt_sum = %d\n", i, cnt, cnt_sum);
			if(cnt_sum >= info->fileSize) {
				pthread_mutex_lock(&mutx);
				last_size = cnt;
				pthread_mutex_unlock(&mutx);
				while(!all_fin){}
				for(i = 0; i < chkcnt; i++) free(*(file_buf + i));
				free(file_buf);
				break;
			}
			pthread_mutex_unlock(&mutx);
			rewind(fp);
		}
	}
	else if(!strncmp(info->fileName, "recv", 4)) {
		fp = fopen("Received_file.mp4", "a+b");
		pthread_cond_broadcast(&cnd);
		while(1) {
			if(*(file_buf+cnt) != NULL && cnt < chkcnt) {
				if(!strncmp(*(file_buf+cnt), "finish", 6)) break;
				fwrite(*(file_buf+(cnt++)), 1, info->dataSize, fp);
			}
			else if(all_fin) break;
		}
	}
	else perror("File Thread Error!\n");
	printf("file_thread is done!\n");
	fclose(fp);
}

void *connect_udp(void *arg) {
	int sock = (long)arg;
	int i, cnt = 0;
	char finish_buf[10];
	int clntaddr_size = sizeof(clntaddr_udp);

	pthread_cond_wait(&cnd, &mutx);
	if(strncmp(info->fileName, "recv", 4) != 0) {
		pthread_mutex_unlock(&mutx);
		pthread_cond_wait(&cnd, &mutx);
		pthread_mutex_unlock(&mutx);
		while(1) {
			if(*(file_buf+cnt) != NULL && cnt < chkcnt) {
				sendto(sock, *(file_buf+(cnt++)), info->dataSize, 0, (struct sockaddr*)&clntaddr_udp, (socklen_t)clntaddr_size);
				printf("cnt = %d\n", cnt);
			}
			else if(cnt == chkcnt-1 && last_size != 0) {
				sendto(sock, *(file_buf+cnt), last_size, 0, (struct sockaddr*)&clntaddr_udp, (socklen_t)clntaddr_size);
				pthread_mutex_lock(&mutx);
				chk_fin = true;
				pthread_mutex_unlock(&mutx);
				printf("udp_thread is done!\n");
				break;
			}
		}
	}
	else if(!strncmp(info->fileName, "recv", 4)) {
		file_buf = (char**)malloc(sizeof(char*) * info->dataSize);
		pthread_mutex_unlock(&mutx);
		pthread_cond_wait(&cnd, &mutx);
		pthread_mutex_unlock(&mutx);
		sendto(sock, "ready", 5, 0, (struct sockaddr*)&clntaddr_udp, (socklen_t)clntaddr_size);
		while(1) {
			*(file_buf+cnt) = (char*)malloc(sizeof(char) * info->dataSize);
			chkcnt++;
			i = recvfrom(sock, *(file_buf+(cnt++)), info->dataSize, 0, (struct sockaddr*)&clntaddr_udp, (socklen_t*)&clntaddr_size);
			if(!strncmp(*(file_buf+cnt-1), "finish", 6)) {
				pthread_mutex_lock(&mutx);
				chk_fin = true;
				pthread_mutex_unlock(&mutx);
				printf("udp_thread is done!\n");
				pthread_cond_broadcast(&cnd);
				while(!all_fin){}
				for(i = 0; i < chkcnt; i++) {
					free(*file_buf+i);
					printf("UDP Thread >> i = %d\n", i);
				}
				free(file_buf);
				break;
			}
			if(cnt != chkcnt) {
				pthread_mutex_lock(&mutx);
				loss_num = cnt-1;
				loss_chk = true;
				pthread_mutex_unlock(&mutx);
				pthread_cond_signal(&cnd);
				pthread_cond_wait(&cnd, &mutx);
				pthread_mutex_unlock(&mutx);
			}
			if(i < info->dataSize) {
				printf("UDP Thread >> info->dataSize/last_size = %d\n", i);
				pthread_mutex_lock(&mutx);
				chk_fin = true;
				pthread_mutex_unlock(&mutx);
				pthread_cond_signal(&cnd);
				while(!all_fin){}
				for(i = 0; i < chkcnt; i++) free(*(file_buf+i));
				free(file_buf);
				break;
			}
		}
	}
	else perror("UDP Thread Error!\n");
}

void *connect_tcp(void *arg) {
	int sock = (long)arg;
	int i, tmp;
	char recv_msg[100];

	tmp = recv(sock, recv_msg, sizeof(recv_msg), 0);
	recv_msg[tmp] = '\0';
	printf("%s\n", recv_msg);
	fgets(recv_msg, 100, stdin);
	send(sock, recv_msg, strlen(recv_msg), 0);
	fflush(stdin);

	if(!strncmp(recv_msg, "send", 4)) {
		pthread_mutex_lock(&mutx);
		printf("Please put your send file name.\n");
		fgets(info->fileName, 256, stdin);
		fflush(stdin);
		pthread_mutex_unlock(&mutx);
		pthread_cond_broadcast(&cnd);
		pthread_cond_wait(&cnd, &mutx);
		if(info->dataSize == BUFSIZE) {
			pthread_mutex_unlock(&mutx);
			while(1) {
				if(chk_fin) break;
				tmp = recv(sock, recv_msg, sizeof(recv_msg), 0);
				recv_msg[tmp] = '\0';
				for(i = 0; i < strlen(recv_msg); i++) {
					tmp *= 10;
					tmp += recv_msg[i];
				}
				send(sock, *(file_buf+tmp), info->dataSize, 0);
			}
		}
	}
	else if(!strncmp(recv_msg, "recv", 4)) {
		pthread_mutex_lock(&mutx);
		sprintf(info->fileName, "recv");
		pthread_mutex_unlock(&mutx);
		pthread_cond_broadcast(&cnd);
		pthread_cond_wait(&cnd, &mutx);
		pthread_mutex_unlock(&mutx);
		while(1) {
			if(chk_fin) {
				send(sock, "finish", 6, 0);
				pthread_mutex_lock(&mutx);
				all_fin = true;
				pthread_mutex_unlock(&mutx);
				break;
			}
			pthread_cond_wait(&cnd, &mutx);
			pthread_mutex_unlock(&mutx);
			if(loss_chk == true) {
				sprintf(recv_msg, "%ld", loss_num);
				send(sock, recv_msg, strlen(recv_msg), 0);
				pthread_mutex_lock(&mutx);
				recv(sock, *(file_buf+chkcnt), info->dataSize, 0);
				loss_chk = false;
				pthread_mutex_unlock(&mutx);
				pthread_cond_signal(&cnd);
			}
		}
	}
	printf("tcp_thread is done!\n");
}

int main(int argc, char **argv) {
	struct sockaddr_in clntaddr_tcp;
	int tcp_s, udp_s;
	pthread_t tcp, udp, file;

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
	pthread_create(&file, NULL, file_thread, (void *)tcp_s);

	udp_s = socket(AF_INET, SOCK_DGRAM, 0);

	memset(&clntaddr_udp, 0, sizeof(clntaddr_udp));
	clntaddr_udp.sin_family = AF_INET;
	clntaddr_udp.sin_addr.s_addr = INADDR_ANY;
	clntaddr_udp.sin_port = htons(atoi(argv[3]));

	pthread_create(&udp, NULL, connect_udp, (void *)udp_s);

	pthread_join(udp, (void**)&udp_s);
	close(udp_s);
	pthread_join(file, (void**)&tcp_s);
	pthread_join(tcp, (void**)&tcp_s);
	close(tcp_s);
	printf("File transfer is done!\n");
	pthread_mutex_destroy(&mutx);
	pthread_cond_destroy(&cnd);
	return(0);
}
