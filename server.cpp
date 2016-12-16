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
struct sockaddr_in servaddr_udp;
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
		printf("fread() start!\n");
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
				while(!all_fin){}
				for(i = 0; i < chkcnt; i++) {
					free(*file_buf+i);
					printf("File Thread >> i = %d\n", i);
				}
				free(file_buf);
				break;
			}
		}
	}
	else if(!strncmp(info->fileName, "recv", 4)) {
		fp = fopen("Received_file", "a+b");
		pthread_mutex_unlock(&mutx);
		while(1) {
			if(*(file_buf+cnt) != NULL) fwrite(*(file_buf+(cnt++)), 1, info->dataSize, fp);
			else if(cnt == chkcnt) {
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

void *connect_udp(void *arg) {
	int sock = (long)arg;
	int i, cnt = 0;
	char ready_buf[10];
	int servaddr_size = sizeof(servaddr_udp);

	pthread_cond_wait(&cnd, &mutx);
	if(strncmp(info->fileName, "recv", 4) != 0) {
		pthread_mutex_unlock(&mutx);
		pthread_cond_wait(&cnd, &mutx);
		pthread_mutex_unlock(&mutx);
		recvfrom(sock, ready_buf, sizeof(ready_buf), 0, (struct sockaddr*)&servaddr_udp, (socklen_t*)&servaddr_size);
		if(!strncmp(ready_buf, "ready", 5)) {
			printf("Sending...\n");
			usleep(100);
			while(1) {
				if(*(file_buf+cnt) != NULL && cnt < chkcnt) {
					usleep(5);
					pthread_mutex_lock(&mutx);
					sendto(sock, *(file_buf+(cnt++)), info->dataSize, 0, (struct sockaddr*)&servaddr_udp, (socklen_t)servaddr_size);
					pthread_mutex_unlock(&mutx);
				}
				else if(cnt == chkcnt && last_size != 0) {
					pthread_mutex_lock(&mutx);
					sendto(sock, *(file_buf+cnt), last_size, 0, (struct sockaddr*)&servaddr_udp, (socklen_t)servaddr_size);
					chk_fin = true;
					pthread_mutex_unlock(&mutx);
					printf("udp_thread is done!\n");
					usleep(10);
					sendto(sock, "finish", 6, 0, (struct sockaddr*)&servaddr_udp, (socklen_t)servaddr_size);
					break;
				}
			}
		}
		else {
			printf("Not ready!\n");
			pthread_mutex_lock(&mutx);
			chk_fin = true;
			pthread_mutex_unlock(&mutx);
		}
	}
	else if(!strncmp(info->fileName, "recv", 4)) {
		file_buf = (char**)malloc(info->dataSize);
		pthread_mutex_unlock(&mutx);
		while(1) {
			pthread_mutex_lock(&mutx);
			*(file_buf+cnt) = (char*)malloc(info->dataSize);
			chkcnt++;
			i = recvfrom(sock, *(file_buf+(cnt++)), info->dataSize, 0, (struct sockaddr*)&servaddr_udp, (socklen_t*)&servaddr_size);
			pthread_mutex_unlock(&mutx);
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
	else perror("UDP Thread Error!\n");
}

void *connect_tcp(void *arg) {
	int sock = (long)arg;
	int i, tmp;
	char recv_msg[100];
	
	sprintf(recv_msg, "Server >> What do u want?(send / recv)");
	send(sock, recv_msg, strlen(recv_msg), 0);
	tmp = recv(sock, recv_msg, sizeof(recv_msg), 0);
	recv_msg[tmp - 1] = '\0';
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
			while(1) {
				if(chk_fin) break;
				tmp = recv(sock, recv_msg, sizeof(recv_msg), 0);
				if(!strncmp(recv_msg, "finish", 6)) continue;
				tmp = 0;
				for(i = 0; i < strlen(recv_msg); i++) {
					tmp *= 10;
					tmp += recv_msg[i];
				}
				send(sock, *(file_buf+tmp), info->dataSize, 0);
			}
			pthread_mutex_lock(&mutx);
			all_fin = true;
			pthread_mutex_unlock(&mutx);
		}
	}
	else if(!strncmp(recv_msg, "send", 4)) {
		pthread_mutex_lock(&mutx);
		sprintf(info->fileName, "recv");
		pthread_mutex_unlock(&mutx);
		pthread_cond_broadcast(&cnd);
		while(1) {
			if(chk_fin) {
				pthread_mutex_lock(&mutx);
				all_fin = true;
				pthread_mutex_unlock(&mutx);
				break;
			}
			pthread_cond_wait(&cnd, &mutx);
			if(loss_chk == true) {
				sprintf(recv_msg, "%ld", loss_num);
				send(sock, recv_msg, strlen(recv_msg), 0);
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
	struct sockaddr_in servaddr_tcp, clntaddr;
	int tcp_s, udp_s, clnt_s, clntaddr_size;
	pthread_t tcp, udp, file;

	if(argc != 3) {
		printf("Usage : %s <tcp_port> <udp_port>\n\tex)./server 8888 7777\n\tIt must different number between tcp port and udp port.\n", argv[0]);
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

	udp_s = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&servaddr_udp, 0, sizeof(servaddr_udp));
	servaddr_udp.sin_family = AF_INET;
	servaddr_udp.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr_udp.sin_port = htons(atoi(argv[2]));

	bind(udp_s, (struct sockaddr*)&servaddr_udp, sizeof(servaddr_udp));
	pthread_create(&udp, NULL, connect_udp, (void *)udp_s);

	pthread_join(file, (void**)&clnt_s);
	pthread_join(udp, (void**)&clnt_s);
	close(udp_s);
	pthread_join(tcp, (void**)&clnt_s);
	close(tcp_s);
	printf("File transfer is done!\n");
	pthread_mutex_destroy(&mutx);
	pthread_cond_destroy(&cnd);
	return(0);
}
