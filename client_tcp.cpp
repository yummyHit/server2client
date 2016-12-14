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

char **file_buf;
int chkcnt = 0, last_size = 0;
bool chk_fin = false, all_fin = false;
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
		pthread_cond_signal(&cnd);
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
		pthread_cond_signal(&cnd);
		while(1) {
			if(*(file_buf+cnt) != NULL && cnt < chkcnt) {
				if(!strncmp(*(file_buf+cnt), "finish", 6)) {	
					printf("file thread >> finish got it!\n");
					pthread_mutex_lock(&mutx);
					all_fin = true;
					pthread_mutex_unlock(&mutx);
					break;
				}
				fwrite(*(file_buf+(cnt++)), 1, info->dataSize, fp);
			}
			else if(cnt > chkcnt || chk_fin) {
				printf("file thread >> chk_fin got it!\n");
				pthread_mutex_lock(&mutx);
				all_fin = true;
				pthread_mutex_unlock(&mutx);
				break;
			}
		}
	}
	else perror("File Thread Error!\n");
	printf("file_thread is done!\n");
	fclose(fp);
}

void *connect_tcp(void *arg) {
	int sock = (long)arg;
	int i = 0, cnt = 0;
	char recv_msg[100];

	i = recv(sock, recv_msg, sizeof(recv_msg), 0);
	recv_msg[i] = '\0';
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
		pthread_cond_signal(&cnd);
		pthread_cond_wait(&cnd, &mutx);
		if(info->dataSize == BUFSIZE) {
			pthread_mutex_unlock(&mutx);
			while(1) {
				if(chk_fin) break;
				cnt = recv(sock, recv_msg, sizeof(recv_msg), 0);
				recv_msg[cnt] = '\0';
				send(sock, *(file_buf+cnt), info->dataSize, 0);
			}
		}
	}
	else if(!strncmp(recv_msg, "recv", 4)) {
		pthread_mutex_lock(&mutx);
		sprintf(info->fileName, "recv");
		file_buf = (char**)malloc(sizeof(char*) * info->dataSize);
		pthread_mutex_unlock(&mutx);
		pthread_cond_signal(&cnd);
		pthread_cond_wait(&cnd, &mutx);
		pthread_mutex_unlock(&mutx);
		while(1) {
			*(file_buf+cnt) = (char*)malloc(sizeof(char) * info->dataSize);
			chkcnt++;
			recv(sock, *(file_buf+cnt), info->dataSize, 0);
			if(!strncmp(*(file_buf+cnt), "finish", 6)) {
				printf("tcp thread >> finish msg got it!\n");
				pthread_mutex_lock(&mutx);
				chk_fin = true;
				pthread_mutex_unlock(&mutx);
				while(!all_fin){}
				break;
			}
			cnt++;
		}
	}
	printf("tcp_thread is done!\n");
}

int main(int argc, char **argv) {
	struct sockaddr_in clntaddr_tcp;
	int tcp_s, i = 0;
	pthread_t tcp, file;

	if(argc != 3) {
		printf("Usage : %s <server_ip> <tcp_port>\n\tex)./client 127.0.0.1 8888\n\tIt must different number between tcp port.\n", argv[0]);
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

	pthread_join(file, (void**)&tcp_s);
	pthread_join(tcp, (void**)&tcp_s);
	printf("pthread_join is finished!\n");
	for(i = 0; i < chkcnt; i++) free(*(file_buf+i));
	free(file_buf);
	close(tcp_s);
	printf("File transfer is done!\n");
	pthread_mutex_destroy(&mutx);
	pthread_cond_destroy(&cnd);
	return(0);
}
