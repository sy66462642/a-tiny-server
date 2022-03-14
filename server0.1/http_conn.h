#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_h
#include<sys/epoll.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/uio.h>
#include"locker.h"
#define FILENAME_LEN 100

class http_conn{

    public:
    static int m_epollfd; //a所有的时间注册到同一个socket上
    static int m_usrcount; //统计用户的数量
    static const int readbufsize=2048;
    static const int writebufsize=2048;

    enum CHECK_STATE{CHECK_STATE_REQUESTLINE=0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};
    //主状态机状态
    enum METHOD{GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT};
    //方式
    enum LINE_STATUS{LINE_OK=0,LINE_BAD,lINE_OPEN};
    //从状态机状态
    enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCES,FORBIDDEN_REQUEST,FILE_REQUEST,iNTERNAL_ERROR,CLOSED_CONNECTION};
    //HTTP 请求结果

    http_conn(){}
    ~http_conn(){}

    void process();
    void init(int sockfd,const sockaddr_in &addr);
    void close_conn();
    bool read(); //feizusedu
    bool write(); //feizusexie
    void unmap();

    HTTP_CODE process_read();//解析HTTP请求
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);//解析HTTP请求首行
    HTTP_CODE parse_headers(char *text);//解析HTTP请求头
    HTTP_CODE parse_content(char *text);//解析HTTP请求体
    bool add_status_line(int status,const char * title);
    bool add_response(const char * format , ...);
    bool add_header(int content_len);
    bool add_content(const char * content);
    LINE_STATUS parse_line(); //解析一行
    char * get_line(){return readbuf+m_start_line;}

    private:

    int m_sockfd;
    int m_content_length;
    sockaddr_in address;
    char readbuf[readbufsize];
    char writebuf[writebufsize];
    int m_read_index; //标识读入客户端数据的最后一个字节的下一个位置。
    int m_write_index;
    int m_check_index; //当前字符在缓冲区的位置。
    int m_start_line; //当前行的起始位置

    CHECK_STATE m_check_state;//主状态

    void initstatus();//初始化连接的信息
    HTTP_CODE do_request();
    char * m_url;//协议
    char * m_version;
    char * m_host;
    char m_real_file[FILENAME_LEN];
    bool m_linger; //判断请求是否保持连接
    char* m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    


    METHOD m_method;

};
#endif