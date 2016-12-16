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
	if(strncmp(info->fileName, "recv", 4) != 0) {
		info->fileName[strlen(info->fileName) - 1] = '\0';
		pthread_mutex_unlock(&mutx);
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
			pthread_mutex_unlock(&mutx);
			cnt_sum += cnt;
			rewind(fp);
			if(cnt_sum >= info->fileSize) {
				pthread_mutex_lock(&mutx);
				last_size = cnt;
				pthread_mutex_unlock(&mutx);
				while(!chk_fin){}
				break;
			}
		}
	}
	else if(!strncmp(info->fileName, "recv", 4)) {
		fp = fopen("Received_file", "a+b");
		pthread_mutex_unlock(&mutx);
		while(1) {
			if(*(file_buf+cnt) != NULL) fwrite(*(file_buf+(cnt++)), 1, info->dataSize, fp);
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
	
	sprintf(recv_msg, "Server >> What do u want?(send / recv)");
	send(sock, recv_msg, strlen(recv_msg), 0);
	i = recv(sock, recv_msg, sizeof(recv_msg), 0);
	recv_msg[i-1] = '\0';
	printf("\nClient choose '%s'.", recv_msg);

	if(!strncmp(recv_msg, "recv", 4)) {
		printf("Please put your send file name.\n");
		pthread_mutex_lock(&mutx);
		fgets(info->fileName, sizeof(info->fileName), stdin);
		pthread_mutex_unlock(&mutx);
		pthread_cond_broadcast(&cnd);
		pthread_cond_wait(&cnd, &mutx);
		if(info->dataSize == BUFSIZE) {
			pthread_mutex_unlock(&mutx);
			printf("Sending...\n");
			while(1) {
				if(*(file_buf+cnt) != NULL && cnt < chkcnt) {
					pthread_mutex_lock(&mutx);
					send(sock, *(file_buf+(cnt++)), info->dataSize, 0);
					pthread_mutex_unlock(&mutx);
				}
				else if(cnt == chkcnt && last_size != 0) {
					printf("tcp thread >> last_size = %d, chkcnt = %d\n", last_size, chkcnt);
					pthread_mutex_lock(&mutx);
					send(sock, *(file_buf+cnt), last_size, 0);
					chk_fin = true;
					pthread_mutex_unlock(&mutx);
					printf("I'll send to client 'finish' msg!\n");
					sprintf(recv_msg, "finish");
					send(sock, recv_msg, strlen(recv_msg), 0);
					break;
				}
			}
		}
	}
	else if(!strncmp(recv_msg, "send", 4)) {
		pthread_mutex_lock(&mutx);
		sprintf(info->fileName, "recv");
		file_buf = (char**)malloc(info->dataSize);
		pthread_mutex_unlock(&mutx);
		pthread_cond_broadcast(&cnd);
		while(1) {
			pthread_mutex_lock(&mutx);
			*(file_buf+cnt) = (char*)malloc(info->dataSize);
			chkcnt++;
			i = recv(sock, *(file_buf+(cnt++)), info->dataSize, 0);
			pthread_mutex_unlock(&mutx);
			if(cnt != chkcnt) {
				pthread_cond_signal(&cnd);
				pthread_cond_wait(&cnd, &mutx);
				pthread_mutex_unlock(&mutx);
			}
			if(i < info->dataSize) {
				pthread_mutex_lock(&mutx);
				chk_fin = true;
				pthread_mutex_unlock(&mutx);
				while(!all_fin){}
				for(i = 0; i < chkcnt; i++) {
					free(*(file_buf+i));
					printf("UDP Thread >> i = %d\n", i);
				}
				free(file_buf);
				break;
			}
		}
	}
	printf("tcp_thread is done!\n");
}

int main(int argc, char **argv) {
	struct sockaddr_in servaddr_tcp, clntaddr;
	int tcp_s, clnt_s, clntaddr_size, i = 0;
	pthread_t tcp, file;

	if(argc != 2) {
		printf("Usage : %s <tcp_port>\n\tex)./server 8888\n\tIt must different number between tcp port.\n", argv[0]);
		exit(1);
	}
	
	tcp_s = socket(AF_INET, SOCK_STREAM, 0);
	memset(&servaddr_tcp, 0, sizeof(servaddr_tcp));
	servaddr_tcp.sin_family = AF_INET;
	servaddr_tcp.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr_tcp.sin_port = htons(atoi(argv[1]));

	bind(tcp_s, (struct sockaddr*)&servaddr_tcp, sizeof(servaddr_tcp));
	listen(tcp_s, 5);

	clntaddr_size = sizeof(clntaddr);
	if((clnt_s = accept(tcp_s, (struct sockaddr*)&clntaddr, (socklen_t*)&clntaddr_size)) == -1) perror("Accept() Error!\n");
	printf("###Client accept Successfully!!###\n");
	pthread_create(&tcp, NULL, connect_tcp, (void *)clnt_s);
	pthread_create(&file, NULL, file_thread, (void *)clnt_s);

	pthread_join(file, (void**)&clnt_s);
	pthread_join(tcp, (void**)&clnt_s);
	printf("pthread_join finished!\n");
	for(i = 0; i < chkcnt; i++) free(*(file_buf+i));
	free(file_buf);
	close(tcp_s);
	printf("File transfer is done!\n");
	pthread_mutex_destroy(&mutx);
	pthread_cond_destroy(&cnd);
	return(0);
}
