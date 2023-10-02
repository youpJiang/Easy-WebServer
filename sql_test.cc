#include<mysql/mysql.h>
#include<iostream>
#include<map>
#include<string>

using namespace std;

map<string,string> users;
int main()
{
    MYSQL *conn = NULL;
    conn = mysql_init(conn);
    if(NULL == conn)
        {
            cout << "ERROR:" << mysql_error(conn);
            exit(1);
        }
    conn = mysql_real_connect(conn, "localhost", "root", "youpjiang", "webserver_db", 3306,  NULL, 0);
    if (mysql_query(conn, "SELECT username,passwd FROM user"))
    {
        cout << "mysql_query error!" << endl;
    }
    else{
        cout << "mysql_query successful!" << endl;
    }
    MYSQL_RES *result = mysql_store_result(conn);
    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
    for(auto &it:users)
    {
        cout << it.first << " : " << it.second << endl;
    }
    return 0;
}