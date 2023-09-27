#include"http_conn.h"
#include<map>
#include<string.h>
#include<fstream>
#include<iostream>

//#define connfdET //边缘触发非阻塞
#define connfdLT //水平触发阻塞

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

//define message info of HTTP response.
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";
const char *doc_root = "/home/jyp/cpp_proj/MyWebserver/root";
//nonblocking for fd.
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}
//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool oneshoot)
{
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef listenfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

    if (oneshoot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}


// //register read event.
// void addfd(int epollfd, int fd, bool oneshoot){
//     epoll_event event;
//     event.data.fd = fd;

//     event.events = EPOLLIN | EPOLLRDHUP;
//     if(oneshoot)
//         event.events |= EPOLLONESHOT;
//     epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
// }

//register ONESHOOT event.
void modfd(int epollfd, int fd, int event){
    epoll_event new_event;
    new_event.data.fd = fd;

    //connfdLT
    new_event.events = event | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &new_event);
}

void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

int HttpConn::m_user_count = 0;
int HttpConn::m_epollfd = -1;

void HttpConn::close_conn(bool real_close){
    if(real_close && (-1 != m_sockfd)){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        --m_user_count;
    }
}

void HttpConn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true);
    ++m_user_count;

    init();
}


void HttpConn::init(){
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_read_idx = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
    //'\0' 表示空字符，它的ASCII码值为0
}

HttpConn::LINE_STATUS HttpConn::parse_line(){
    char temp;
    for(;m_checked_idx < m_read_idx; ++m_checked_idx){
        temp = m_read_buf[m_checked_idx];
        if('\r' == temp){
            if((m_checked_idx + 1) == m_read_idx) return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n'){
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
        }else if ('\n' == temp){
            if(1 < m_checked_idx && '\r' == m_read_buf[m_checked_idx - 1]){
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

bool HttpConn::read_once(){
    if(m_read_idx >= READ_BUFFER_SIZE)return false;
    int bytes_read = 0;

    //connfdLT
    bytes_read = recv(m_sockfd, m_read_buf, READ_BUFFER_SIZE - m_read_idx, 0);
    if(bytes_read <=0 )return false;
    m_read_idx+=bytes_read;
    std::cout << std::endl << "read_once(): 收到报文如下" << std::endl;
    std::cout << m_read_buf << std::endl;
    return true;
}

HttpConn::HTTP_CODE HttpConn::parse_request_line(char *text){
    //parse the request line of message
    //RETURN NO_REQUESET if everything OK ,else RETURN BAD_REQUEST
    m_url = strpbrk(text, " \t");  // strpbrk:string pointer break.
    if(!m_url)return BAD_REQUEST;
    *m_url++ = '\0';
    char *method = text;
    if(0 == strcasecmp(method, "GET")){
        m_method = GET;
    }else if(0 == strcasecmp(method, "POST")){
        m_method = POST;
    }else return BAD_REQUEST;       //GET OR POST only.

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version)return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(0 != strcasecmp(m_version, "HTTP/1.1"))return BAD_REQUEST;
    if(0 == strncasecmp(m_url, "http://", 7)){
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(0 == strncasecmp(m_url, "https://", 8)){
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/')return BAD_REQUEST;
    if(strlen(m_url) == 1)strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parse_headers(char* text){
    //parseing headers and empty line.
    if('\0' == text[0]){
        if(0 != m_content_length){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }else if(0 == strncasecmp(text, "Connection:", 11)){
        text += 11;
        text += strspn(text, " \t");
        if(0 == strcasecmp(text, "keep-alive")){
            m_linger = true;
        }
    }else if(0 == strncasecmp(text, "Content-length:", 15)){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }else if(0 == strncasecmp(text, "Host:", 5)){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }else{
        // std::cout << "Unknow header: " << text << std::endl;
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parse_content(char *text){
    if(m_read_idx >= (m_content_length + m_checked_idx)){
        text[m_content_length] = '\0';
        m_string  = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::do_request(){
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char *p = strrchr(m_url,'/');

    // /0:register page.
    if(*(p + 1) == '0'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // /1:login page.
    else if(*(p + 1) == '1'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // POST: picture.
    else if(*(p + 1) == '5'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //POST: video
    else if(*(p + 1) == '6'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // POST: my resume.
    else if(*(p + 1) == '7'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/resume.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode)){
        std::cout << "do_request return BAD!" <<std::endl;
        return BAD_REQUEST;
    }

    //Get the file descriptor as read-only, map the file into memory via mmap
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

bool HttpConn::add_response(const char *format, ...){
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool HttpConn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}
bool HttpConn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool HttpConn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool HttpConn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive":"close");
}
bool HttpConn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool HttpConn::add_content(const char *content)
{
    return add_response("%s", content);
}
void HttpConn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
bool HttpConn::write(){
    int temp = 0;

    //response message is empty, does't usually happen.
    if(0 == bytes_to_send){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1){
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if(0 > temp){
            if(EAGAIN == errno){
                //register write event.
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                std::cout << "buffer full, register EPOLLOUT event." << std::endl;
                return true;
            }
            //if sent error but not the buffer problem, unmap it .
            unmap();
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        //all data have sent.
        if(0 >= bytes_to_send){
            unmap();
            // std::cout << "all data sent, register EPOLLIN event." << std::endl;
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            if(m_linger){
                init();
                return true;
            }
            else{
                return false;
            }
        }
    }
}
HttpConn::HTTP_CODE HttpConn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    
    while((m_check_state==CHECK_STATE_CONTENT && line_status==LINE_OK) || LINE_OK == (line_status = parse_line())){
        text = get_line();
        m_start_line = m_checked_idx;
        switch(m_check_state){
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if(BAD_REQUEST == ret) 
                {std::cout << "312 return BAD!" <<std::endl;
                return BAD_REQUEST;}
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if(BAD_REQUEST == ret)
                {std::cout << "320 return BAD!" <<std::endl;
                return BAD_REQUEST;}
            else if(GET_REQUEST == ret) return do_request();
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if(GET_REQUEST == ret) return do_request();
            line_status = LINE_OPEN; // avoid entrying loop again.
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}
bool HttpConn::process_write(HTTP_CODE ret){
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        //HTTP status code 500
        add_status_line(500,error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if(0 != m_file_stat.st_size){
            add_headers(m_file_stat.st_size);
            //The first iovec point to response message buffer.
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            //The second iovec point to file pointer returned by mmap.
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count  = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else{
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
        break;
    }
    default:
        return false;
    }
    //one iovec needed only except FILE_REQUEST, pointed the m_write_buf.
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
void HttpConn::process(){
    HTTP_CODE read_ret = process_read();

    //incomplete request, keep on recving data.
    if(NO_REQUEST == read_ret){
        //register and listen for read event.
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return ;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret){
        std::cout << "!write_ret, 关闭conn连接！" << std::endl;
        close_conn();
    }
    std::cout << "要发送的报文：" << std::endl;
    std::cout << m_write_buf << std::endl;
    std::cout << m_real_file << std::endl;
    //register and listen for write event.
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}