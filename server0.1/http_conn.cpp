#include"http_conn.h"
#include<sys/epoll.h>
#include<stdio.h>
#include<stdlib.h>
#include<cstring>
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

int http_conn::m_usrcount=0;
int http_conn::m_epollfd=-1;

const char * doc_root="/home/yixiangwang/newserver/resources";

bool http_conn::add_response(const char *format , ...)
{
    if(m_write_index>=writebufsize)
    return false;
    va_list arg_list;
    va_start(arg_list,format);
    int len =vsnprintf(writebuf+m_write_index,writebufsize-1-m_write_index,format,arg_list);
    if(len>=writebufsize-1-m_write_index)
    return false;
    m_write_index+=len;
    va_end(arg_list);
    return true;

}

bool http_conn::add_status_line(int status,const char* title)
{
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

bool http_conn::add_content(const char * content)
{
    return content!=nullptr;
}

bool http_conn::add_header(int content_len)
{
    add_response("Content-Length: %d\r\n",content_len);//协议长度
    add_response("Content-Type:%s\r\n","text/html");//协议类型
    add_response("Connection: %s\r\n",(m_linger==true)?"keep-alive":"close");//连接类型
    add_response("%s","\r\n");//空行
    return true;
}
//设置非阻塞
void setnonblocking(int fd)
{
    int old_flag=fcntl(fd,F_GETFL);
    int newflag=old_flag |O_NONBLOCK;
    fcntl(fd,F_SETFL,newflag);
}

void addfd(int epollfd,int fd,bool one_shot)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN | EPOLLRDHUP;

    if(one_shot)
    {
        event.events |EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    //设置文件描述符非阻塞
    setnonblocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,0);
    close(fd);
}

//重置socket上的epolloneshot事件 确保下一次可读可被触发
void modfd(int epollfd,int fd,int ev)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=ev | EPOLLONESHOT |EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

void http_conn::init(int sockfd,const sockaddr_in &addr)
{
    m_sockfd=sockfd;
    address=addr;
    //设置端口复用
    int reuse=1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    addfd(m_epollfd,sockfd,true);
    m_usrcount++;
    initstatus();
}
void http_conn::initstatus()
{
    m_check_state =CHECK_STATE_REQUESTLINE; //初始化状态为解析请求首航
    m_check_index = 0;
    m_start_line =0;
    m_read_index=0;
    m_method=GET;
    m_url=0;
    m_version=0;
    m_content_length=0;
    m_linger=false;

    memset(readbuf,0,2048*sizeof(readbuf));

}

void http_conn::close_conn(){
    if(m_sockfd!=-1)
    {
        removefd(m_epollfd, m_sockfd );
        m_sockfd=-1;
        m_usrcount--;

    }   
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status= LINE_OK;
    HTTP_CODE ret=NO_REQUEST;
    char *text=0;
    while((m_check_state== CHECK_STATE_CONTENT && line_status==LINE_OK)
    ||((line_status=parse_line())==LINE_OK))
    {
        //解析到了一行完整数据或者请求体

        //获取一行数据
        text=get_line();
        m_start_line=m_check_index;
        printf("got 1 http line %s\n",text);
        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret=parse_request_line(text);
                if(ret=BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;

            }
            case CHECK_STATE_HEADER:
            {
                ret=parse_headers(text);
                if(ret=BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if(ret==GET_REQUEST)
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret=parse_content(text);
                if(ret==GET_REQUEST)
                {
                    return do_request();
                }
                line_status=lINE_OPEN;
                break;

            }
            default:
            {
                return iNTERNAL_ERROR;
            }
        }

    }
    return NO_REQUEST;
}
//解析HTTP请求行，请求方法，URL 版本
bool http_conn::process_write(http_conn::HTTP_CODE ret)
{
    switch(ret)
    {
        case iNTERNAL_ERROR:
        {
            add_status_line(500,"internalerr");
            add_header(strlen("The server has an error"));
            if(!add_content("The server has an error"))
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400,"Notfound");
            add_header(strlen("A bad request"));
            if(!add_content("A bad request"))
            {
                return false;
            }
            break;
        }
        case NO_REQUEST:
        {
            add_status_line(404,"NptFound");
            add_header(strlen("Cannot find the resources"));
            if(!add_content("Cannot find the resources"))
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403,"forbiddenrequest");
            add_header(strlen("The request is forbidden"));
            if(!add_content("The request is forbidden"))
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200,"OK");
            add_header(m_file_stat.st_size);
            m_iv[0].iov_base =writebuf;
            m_iv[0].iov_len =m_write_index;
            m_iv[1].iov_base=m_file_address;
            m_iv[1].iov_len=m_file_stat.st_size;
            m_iv_count=2;
            return true;
        }
        
        default :
            return false;

        
    }
    return false;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    m_url=strpbrk(text," \t");

    *m_url++ ='\0';

    char * method =text;
    if(strcasecmp(method,"GET")==0)
        m_method=GET;
    else
        return BAD_REQUEST;
    m_version = strpbrk(m_url," \t");
    if(!m_version)
        return BAD_REQUEST;
    
    *m_version++ ='\0';
    if(strcasecmp(method,"HTTP/1.1")!=0)
    return BAD_REQUEST;

    if(strncasecmp(m_url,"http://",7)==0)
    {
        m_url+=7;
        m_url=strchr(m_url,'/');
    }
    if(!m_url ||m_url[0]!='/')
    return BAD_REQUEST;

    m_check_state =CHECK_STATE_HEADER; //检查请求头

    return NO_REQUEST;
}
http_conn::HTTP_CODE  http_conn::parse_headers(char *text)
{
    if(text[0]=='\0')
    {
        if(m_content_length!=0)
        {
            m_check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text,"connection",11)==0)
    {
        text+=11;
        text+=strspn(text," \t");
        if(strcasecmp(text,"keep-alive")==0)
        {
            m_linger=true;
        }
    }
    else if(strncasecmp(text,"content-length",15)==0)
    {
        text+=15;
        text+=strspn(text," \t");
        m_content_length=atoi(text);
    }
    else if(strncasecmp(text,"Host:",5)==0)
    {
        text+=5;
        text+=strspn(text," \t");
        m_host=text;
    }
    else
    {
        printf("unknown_header %s\n",text);
    }
    return NO_REQUEST;
}
http_conn::HTTP_CODE  http_conn::parse_content(char *text)
{
    if(m_read_index>=m_content_length+m_check_index)
    {
        text[m_content_length]='\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}
//解析一行判断\r\n
http_conn::LINE_STATUS http_conn::parse_line() 
{
    char tmp;
    for(;m_check_index<m_read_index;++m_check_index)
    {
        tmp=readbuf[m_check_index];
        if(tmp=='\r')
        {
            if(m_check_index+1==m_read_index)
            return lINE_OPEN;
            else if(readbuf[m_check_index+1]=='\n')
            {
            readbuf[m_check_index++]='\0';
            readbuf[m_check_index++]='\0';
            return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(tmp=='\n')
        {
            if((m_check_index >1) && (readbuf[m_check_index-1]=='\r'))
            {
            readbuf[m_check_index-1]='\0';
            readbuf[m_check_index++]='\0';
            return LINE_OK;
            }
            return LINE_BAD;
        }
        

    }
    return lINE_OPEN;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file,doc_root);
    int len =strlen(doc_root);
    strncpy(m_real_file+len,m_url,FILENAME_LEN -len -1);
    printf("%s\n",m_real_file);
    if(stat(m_real_file,&m_file_stat)<0)
    {
        return NO_RESOURCES;
    }
    if(!(m_file_stat.st_mode& S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    int fd=open(m_real_file,O_RDONLY);
    
    m_file_address =(char *)mmap(0,m_file_stat.st_size,PROT_READ,MAP_SHARED,fd,0);
    close(fd);
    return FILE_REQUEST;

}
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address=0;
    }
}
//read data until no data can be read or client was closed 
bool http_conn::read()
{
    if(m_read_index>= readbufsize)
    {
        return false;
    }
    //getbufs
    int bytes_read=0;
    while(1)
    {
        bytes_read=recv(m_sockfd,m_read_index+readbuf,readbufsize-m_read_index,0);
        if(bytes_read==-1)
        {
            if(errno==EAGAIN || errno== EWOULDBLOCK)
            {break;}
            return false;

        }
        else if(bytes_read==0)
        return false;
        m_read_index+=bytes_read;
        printf("%s\n",readbuf);
        return true;

    }
    return true;
}
bool http_conn::write()
{
   int temp=0;
   int bytes_have_send =0;
   int bytes_to_send =m_write_index;
   if(bytes_to_send ==0)
   {
       modfd(m_epollfd,m_sockfd,EPOLLIN);
       initstatus();
       return true;
   }
   while(1)
   {
       temp=writev(m_sockfd,m_iv,m_iv_count);//分散写，写多块内存
       if(temp<=-1)
       //TCP与写缓存没有空间
       {
           if(errno==EAGAIN)
           {
               modfd(m_epollfd,m_sockfd,EPOLLOUT);
               return true;
           }
           unmap();
           return false;
       }
       bytes_to_send-=temp;
       bytes_have_send+=temp;
       if(bytes_to_send<=bytes_have_send)
       {
           //发送成功，根据connetcion字段决定是否建立连接
           unmap();
           if(m_linger)
           {
               initstatus();
               modfd(m_epollfd,m_sockfd,EPOLLIN);
               return true;
           }
           else
           {
               modfd(m_epollfd,m_sockfd,EPOLLIN);
               return false;
           }
       }

   }
}
//有线程池中的工作线程调用，是处理HTTP请求的入口函数
void http_conn::process()
{
    //解析请求，生成响应
    HTTP_CODE read_ret=process_read();

    if(read_ret==NO_REQUEST)
    {
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }
    bool write_ret=process_write(read_ret);
    if(!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);

}

