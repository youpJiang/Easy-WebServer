server: main.c ./http/http_conn.cc ./http/http_conn.h
	g++ -g -o server main.c ./http/http_conn.cc ./http/http_conn.h