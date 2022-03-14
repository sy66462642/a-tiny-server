#include"locker.h"
#include"threadpool.h"
#include<cstdio>
#include<cstring>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<errno.h>
#include<cstdlib>
#include<signal.h>
#include"http_conn.h"

#define MAXFD 65535
#define MAXEVENT 9999

void addsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa,0,sizeof(sa));
    sa.sa_handler=handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig,&sa,NULL);
}

extern void addfd(int epollfd,int fd,bool one_shot);
extern void deletefd(int epollfd,int fd);
extern void modfd(int epollfd,int fd,int ev);


int main(int argc,char* argv[])
{
    if(argc<=1)
    {
        printf("Please run this program :%s portNumber",basename(argv[0]));
        exit(-1);
    }
    //getport
    int port=atoi(argv[1]);

    addsig(SIGPIPE,SIG_IGN);

    //create threadpool
    ThreadPool<http_conn> * pool=NULL;
    try
    {
       pool =new ThreadPool<http_conn>;
    }
    catch(const std::exception& e)
    {
        exit(-1);
    }

    //save clint data
    http_conn *users =new http_conn[MAXFD];
    int listenfd=socket(PF_INET,SOCK_STREAM,0);

    //mulituse port
    int reuse=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //bind
    struct sockaddr_in address;
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(port);
    bind(listenfd,(struct sockaddr*)&address,sizeof(address));

    //listen
    listen(listenfd,5);

    //epoll class 
    epoll_event events[MAXEVENT];
    int epollfd =epoll_create(5);

    //add fd to epoll class
    
    addfd(epollfd,listenfd,false);

    http_conn::m_epollfd=epollfd;
    while(1)
    {
        int num=epoll_wait(epollfd,events,MAXEVENT,-1);
        if(num<0&&errno!=EINTR)
        {
            printf("epoll failed\n");
            break;
        }
        for(int i=0;i<num;i++)
        {
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd)
            {
                struct sockaddr_in clientaddress;
                socklen_t clientaddr_len =sizeof(clientaddress);
                int connfd=accept(listenfd,(struct sockaddr* )&clientaddress,&clientaddr_len);
            if(http_conn::m_usrcount>=MAXFD)
            {
                close(connfd);

                continue;
            }
            //初始化新客户数据
            users[connfd].init(connfd,clientaddress);
            }
            else if(events[i].events &(EPOLLRDHUP |EPOLLHUP| EPOLLERR))
            {
                //错误事件发生。
                users[sockfd].close_conn();

            }

            else if(events[i].events &EPOLLIN)
            {
                if(users[sockfd].read())
                {
                    pool->appendTask(users+sockfd);
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }

            else if(events[i].events &EPOLLOUT)
            {
                if(!users[sockfd].write())
                {
                   users[sockfd].close_conn();
                }
            }
        }

    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
 
    return 0;
}
