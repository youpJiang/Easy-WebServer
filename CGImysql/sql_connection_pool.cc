
#include<iostream>
#include "sql_connection_pool.h"
using namespace std;


ConnectionPool::ConnectionPool()
{
    this->curconn_ = 0;
    this->freeconn_ = 0;
}
ConnectionPool::~ConnectionPool()
{
    DestroyPool();
}
ConnectionPool *ConnectionPool::GetInstance()
{
    static ConnectionPool conn_pool;
    return &conn_pool;
}

void ConnectionPool::Init(string url, string user, string password, string databasename, int port, unsigned int maxconn)
{
    this->url_ = url;
    this->user_ = user;
    this->password_ = password;
    this->databasename_ = databasename;
    this->port_ = port;
    lock.lock();
    for(int i = 0; i < maxconn; ++i)
    {
        MYSQL *conn = NULL;
        conn = mysql_init(conn);
        if(NULL == conn)
        {
            cout << "ERROR:" << mysql_error(conn);
            exit(1);
        }
        //connect mysql engine.
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), password.c_str(), databasename.c_str(), port, NULL, 0);
        if (conn == NULL)
		{
			cout << "Error: " << mysql_error(conn);
			exit(1);
		}
        connlist_.push_back(conn);
        ++freeconn_;
    }
    // cout << "数据库连接池的大小为：" << connlist_.size() << endl;
    reserve_ = Sem(freeconn_); // Synchronization Mechanism for Multi-thread Contention for Connections Using Signals
    this->maxconn_ = freeconn_; // init the maxconn_ with actual freeconn_ created.
    lock.unlock();
}
MYSQL *ConnectionPool::GetConnection()
{
    MYSQL *con = NULL;
    int size = connlist_.size();
    if( 0 == size)
        return NULL;
    reserve_.wait();

    lock.lock();
    con = connlist_.front();
    connlist_.pop_front();
    --freeconn_;
    ++curconn_;
    lock.unlock();

    return con;
}

bool ConnectionPool::ReleaseConnection(MYSQL *conn)
{
    if(NULL == conn)
        return false;

    lock.lock();
    connlist_.push_back(conn);
    ++freeconn_;
    --curconn_;
    lock.unlock();

    reserve_.post();
    return true;
}
void ConnectionPool::DestroyPool()
{
    lock.lock();
    if(0 < connlist_.size())
    {
        for(auto &it:connlist_)
        {
            MYSQL *con = it;
            mysql_close(it);
        }
        curconn_ = 0;
        freeconn_ = 0;

        connlist_.clear();
        lock.unlock();
    }
    lock.unlock();
}
//get a sql conn from coonpool and put it in *sql.
ConnectionRAII::ConnectionRAII(MYSQL **sql, ConnectionPool *connpool)
{
    *sql = connpool->GetConnection();
    conRAII_ = *sql;
    poolRAII_ = connpool;
}

ConnectionRAII::~ConnectionRAII()
{
    poolRAII_->ReleaseConnection(conRAII_);
}
