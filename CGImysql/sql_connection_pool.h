#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include<list>
#include<string>
#include <mysql/mysql.h>
#include "../lock/locker.h"

using namespace std;

class ConnectionPool
{
public:
    MYSQL *GetConnection();             //get mysql conn.
    bool ReleaseConnection(MYSQL *conn); 
    int GetFreeConn();                  //get number of freeconns.
    void DestroyPool();                 //call by Destructor.

    //sigleton
    static ConnectionPool *GetInstance();
    void Init(string url, string user, string password, string databasename, int port, unsigned int maxconn);
private:
    ConnectionPool();
    ~ConnectionPool();
private:
    unsigned int maxconn_;      //max connections.
    unsigned int curconn_;      //current connections used.
    unsigned int freeconn_;     //free conns.

private:
    list<MYSQL *> connlist_;
    Locker lock;
    Sem reserve_;

private:
    string url_;
    string port_;
    string user_;
    string password_;
    string databasename_;
};

class ConnectionRAII
{
public:
    ConnectionRAII(MYSQL **con, ConnectionPool *connpool);
    ~ConnectionRAII();
private:
    MYSQL *conRAII_;
    ConnectionPool *poolRAII_;
};

#endif