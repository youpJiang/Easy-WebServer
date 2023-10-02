#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/uio.h>
#include <stdarg.h>
#include <fcntl.h>

#include "../CGImysql/sql_connection_pool.h"


class HttpConn{
public:
    HttpConn(){}
    ~HttpConn(){}

public:
    //设置读取文件的名称m_real_file大小
    static const int FILENAME_LEN = 200;
    //设置读缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE = 2048;
     //设置写缓冲区m_write_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;

    //message request method, this project use post & get only.
    enum METHOD
    {
        GET = 0,  //fix the start with 0 ,followed by plus one in sequence
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    //main state machine
    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    //sub state machine
    enum LINE_STATUS{
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    //HTTP code.
    enum HTTP_CODE{
        NO_REQUEST,     //请求不完整，需要继续读取请求报文数据
        GET_REQUEST,    //获得了完整的HTTP请求
        BAD_REQUEST,    //HTTP请求报文有语法错误
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,   
        INTERNAL_ERROR, //服务器内部错误，一般不会触发
        CLOSED_CONNECTION
    };

public:
    
    void init(int sockfd, const sockaddr_in &addr);//init func for external calls.
    //close http connect.
    void close_conn(bool real_close = true);
    void process(); // thread call this func to parsing message.
    bool read_once(); // read all the data sent from client.
    bool write();
    void initmysql_result(ConnectionPool *connpool);
private:
    //init private vars.
    void init();
    //after read_once, reading from m_read_buf and parsing.
    HTTP_CODE process_read();

    //all response message related func call this func.
    //update the content of m_write_idx and m_write_buf.
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_len);
    bool add_content_length(int content_length);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char *content);

    
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    //create the response message.
    HTTP_CODE do_request();
    
    char *get_line(){return m_read_buf + m_start_line;};

    //sub state machine read a line and parse which part it is in HTTP.
    LINE_STATUS parse_line();

    void unmap();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;

private:
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE]; //message data after reading.
    int m_read_idx; //next position after the last Byte of m_read_buf. Updated by read_once().
    int m_checked_idx; //currently read position. Updated by sub state machine.
    int m_start_line; //char nums have been parsed. Updated by main state machine.
    
    char m_write_buf[WRITE_BUFFER_SIZE]; //the message going to send.
    int m_write_idx;

    CHECK_STATE m_check_state;
    METHOD m_method;

    //6 var about parsing message/
    char m_real_file[FILENAME_LEN]; //storage the address of requsting file.(web root dir + filename)
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger; //linger connect or not.

    char *m_file_address;  //storage the content of requsting file.
    struct stat m_file_stat; //such as size ,time created, time used, time edited.
    struct iovec m_iv[2];   //for muti-element arr.
    int m_iv_count;
    int cgi;                //whether message is POST
    char *m_string;     //storage the POST data.
    int bytes_to_send;
    int bytes_have_send;

};

#endif