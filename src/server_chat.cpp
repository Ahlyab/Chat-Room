#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define NAME_LEN 32
/*
    Name that comes from client end check if it's already in clients and return  a boolean value
*/
static  unsigned int cli_count =0;
static int uid =10;

typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char name[NAME_LEN];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

int check_duplicate_client_name(char *name)
{
    int i=0, j=0;
    while(clients[i])
    {
        if(strcmp(name, clients[i]->name)==0)
        {
            char temp[NAME_LEN] = "XZ";
            strncpy(clients[i]->name,temp, sizeof(clients[i]->name));
            return 1;
        }
            
        ++i;
    }

    return 0;
}

void str_overwrite_stdout(){
    printf("\r%s",">");
    fflush(stdout);
}

void str_trim_lf(char* arr, int length)
{
    for(int i=0;i<length;i++)
    {
        if(arr[i] =='\n')
        {
            arr[i] = '\0';
            break;
        }
    }
}

void queue_add(client_t *cl)
{
    pthread_mutex_lock(&clients_mutex);
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(!clients[i])
        {
            clients[i] = cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void queue_remove(int uid)
{
    pthread_mutex_lock(&clients_mutex);
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(clients[i])
        {
            if(clients[i]->uid == uid)
            {
                clients[i]==NULL;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);

}

void print_ip_addr(struct sockaddr_in addr)
{
    printf("%d,%d,%d,%d",
            addr.sin_addr.s_addr & 0xff,
            (addr.sin_addr.s_addr & 0xff00)>>8,
            (addr.sin_addr.s_addr & 0xff0000)>>16,
            (addr.sin_addr.s_addr & 0xff000000)>24
            );
}

void send_message(char *s,int uid)
{
    pthread_mutex_lock(&clients_mutex);
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(clients[i])
        {
            if(clients[i]->uid!=uid)
            {
                if(write(clients[i]->sockfd,s,strlen(s))<0)
                {
                    printf("ERROR: writ to descriptor failed\n");
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void *arg)
{
    char buffer[BUFFER_SZ];
    char name[NAME_LEN];
    int leave_flag =0;
    cli_count++;

    client_t *cli = (client_t*)arg;
    if(recv(cli->sockfd, name, NAME_LEN,0)<=0 || strlen(name)<2 || strlen(name) >= NAME_LEN -1 )
    {
        printf("Enter the name correctly\n");
        leave_flag =1;
        char response[1024] = "1";
        send(cli->sockfd, response, sizeof(response), 0);
    }
    else if(check_duplicate_client_name(name))
    {
        char response[1024] = "1";
        send(cli->sockfd, response, sizeof(response), 0);
    }
    else
    {
        char response[1024] = "0";
        send(cli->sockfd, response, sizeof(response), 0);
        strcpy(cli->name, name);
        sprintf(buffer,"%s has joined the chatroom\n",cli->name);
        printf("%s",buffer);
        send_message(buffer,cli->uid);
    }
    bzero(buffer, BUFFER_SZ);
    while(true)
    {
        if(leave_flag)
        {
            break;
        }
        int receive = recv(cli->sockfd,buffer,BUFFER_SZ,0);
        if(receive>0)
        {
            if(strlen(buffer)>0)
            {
                send_message(buffer,cli->uid);
                str_trim_lf(buffer,strlen(buffer));
                printf("%s\n",buffer);
            }
        }
        else if (receive==0 || strcmp(buffer,"exit")==0)
        {
            if(strlen(cli->name)>3)
                sprintf(buffer,"%s has left the chatroom\n", cli->name);    
            printf("%s", buffer);
            send_message(buffer,cli->uid);
            leave_flag =1;
        }
        else
        {
            printf("ERROR: -1\n");
            leave_flag =1;
        }
        bzero(buffer,BUFFER_SZ);
    }
    close(cli->sockfd);
    queue_remove(cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());
    return NULL;
    

}
int main(int argc, char **argv)
{
    if(argc!=2)
    {
        printf("Usage: %s <port> \n", argv[0]);
        return EXIT_FAILURE;
    }
    char *ip = (char*)"127.0.0.1";
    int port = atoi(argv[1]);

    int option =1;
    int listenfd = 0, clientfd =0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(port);

    signal(SIGPIPE, SIG_IGN);
    
    if(setsockopt(listenfd, SOL_SOCKET,(SO_REUSEPORT | SO_REUSEADDR), (char*)&option, sizeof(option)) <0)
    {
        printf("ERROR: setsockopt\n");
        return EXIT_FAILURE;
    }

    if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))<0)
    {
        printf("ERROR: bind\n");
        return EXIT_FAILURE;
    }

    if(listen(listenfd, 10)<0)
    {
        printf("ERROR : listen\n");
        return EXIT_FAILURE;
    }
    printf("            HI! WELCOME TO THE CHATROOM.          \n");
    while(true)
    { 
        socklen_t clilen = sizeof(cli_addr);
        clientfd = accept(listenfd,(struct sockaddr*)&cli_addr, &clilen);
        
        if((cli_count+1)== MAX_CLIENTS)
        {
            printf("Chatroom reached it's limit. Try again later.");
            print_ip_addr(cli_addr);
            close(clientfd);
            continue;
        }

        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->address = cli_addr;
        cli->sockfd = clientfd;
        cli->uid = uid++;

        queue_add(cli);
        pthread_create(&tid,NULL,&handle_client,(void*)cli);

        sleep(1);

    }
    return EXIT_SUCCESS;



}