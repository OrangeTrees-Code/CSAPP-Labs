#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef char string[MAXLINE];

typedef struct{
    string host;
    string port;
    string path;
} url_t;


int parse_header(rio_t *client_rio_p, string header_info, string host)
{
    string buf;
    int has_host_flag = 0;
    while (1)
    {
        rio_readlineb(client_rio_p, buf, MAXLINE);
        
        if (strcmp(buf, "\r\n") == 0)
        {
            break;
        }
        
        if (!strncasecmp(buf, "Host:", strlen("Host:")))
        {
            has_host_flag = 1;
        }
        
        if (!strncasecmp(buf, "Connection:", strlen("Connection:")))
        {
            continue;
        }
        if (!strncasecmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:")))
        {
            continue;
        }
        if (!strncasecmp(buf, "User-Agent:", strlen("User-Agent:")))
        {
            continue;
        }
        
        strcat(header_info, buf);
    }
    
    if (!has_host_flag)
    {
        sprintf(buf, "Host: %s\r\n", host);
        strcpy(header_info, buf);
    }
    
    strcat(header_info, "Connection: close\r\n");
    strcat(header_info, "Proxy-Connection: close\r\n");
    strcat(header_info, user_agent_hdr);
    
    strcat(header_info, "\r\n");
    return 0;
}


int parse_url(string url, url_t *url_info)
{
    
    const int http_prefix_len = strlen("http://");
    if (strncasecmp(url, "http://", http_prefix_len))
    {
        fprintf(stderr, "Not http protocol: %s\n", url);
        return -1;
    }
    
    char *host_start = url + http_prefix_len;
    char *port_start = strchr(host_start, ':');
    char *path_start = strchr(host_start, '/');

    
    if (path_start == NULL)
    {
        return -1;
    }

    
    if (port_start == NULL)
    {
        *path_start = '\0';
        strcpy(url_info->host, host_start);
        strcpy(url_info->port, "80");
        *path_start = '/';
        strcpy(url_info->path, path_start);
    }

    
    else
    {
        *port_start = '\0';
        strcpy(url_info->host, host_start);
        *port_start = ':';
        *path_start = '\0';
        strcpy(url_info->port, port_start + 1);
        *path_start = '/';
        strcpy(url_info->path, path_start);
    }

    return 0;
}

void do_get(rio_t *client_rio_p, string url)
{
    if (query_cache(client_rio_p, url))
    {
        return;
    }
    url_t urlInfo;
    if (parse_url(url, &urlInfo) < 0)
    {
        fprintf(stderr, "Parse url error\n");
        return;
    }

    string header_info;
    parse_header(client_rio_p, header_info, urlInfo.host);

    int serverfd = open_clientfd(urlInfo.host, urlInfo.port);
    if (serverfd < 0)
    {
        fprintf(stderr, "Open connect to %s:%s error\n", urlInfo.host, urlInfo.port);
        return;
    }

    rio_t server_rio;
    rio_readinitb(&server_rio, serverfd);

    string buf;
    sprintf(buf, "GET %s HTTP/1.0/r/n%s", urlInfo.path, header_info);

    if (rio_writen(serverfd, buf, strlen(buf)) != strlen(buf)){
        fprintf(stderr, "Send request line and header error\n");
        close(serverfd);
        return;
    }

    int totalReceive, currentReceive;
    char file_cache[MAX_OBJECT_SIZE];

    while(currentReceive = rio_readnb(&server_rio,buf,MAXLINE)){
        if (currentReceive < 0){
            fprintf(stderr, "Read server response error\n");
            close(serverfd);
            return;
        }
        if(totalReceive + currentReceive < MAX_OBJECT_SIZE){
            memcpy(file_cache + totalReceive, buf, currentReceive);
        }
        totalReceive += currentReceive;

        if(rio_writen(client_rio_p->rio_fd,buf,currentReceive)!=currentReceive){
            fprintf(stderr, "Send response to client error\n");
            close(serverfd);
            return;
        }


    }

    if(totalReceive<MAX_OBJECT_SIZE){
        add_cache(url, file_cache, totalReceive);
    }
}
void *thread(void* vargp){
    pthread_detach(pthread_self());

    int clientfd = *((int *)vargp);
    free(vargp);

    rio_t client_rio;
    rio_readinitb(&client_rio, clientfd);

    string buf;
    if (rio_readlineb(&client_rio, buf,MAXLINE)){
        fprintf(stderr, "Read request line error: %s\n", strerror(errno));
        close(clientfd);
        return NULL;
    }

    string method, path, httpVersion;
    if(sscanf(buf,"%s %s %s",method,path,httpVersion)<3){
        fprintf(stderr, "Parse request line error: %s\n", strerror(errno));
        close(clientfd);
        return NULL;
    }


    if (!strcasecmp(method, "GET"))
    {
        do_get(&client_rio, path);
    }
    close(clientfd);
    return NULL;
}
int main(int argc,char** argv)
{
    
    signal(SIGPIPE, SIG_IGN);
    if(argc<2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int listenfd,*connectfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    listenfd = Open_listenfd(argv[1]);
    init_cache();
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connectfd = (int *)malloc(sizeof(int));
        *connectfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (*connectfd < 0)
        {
            fprintf(stderr, "Accept Error: %s\n", strerror(errno));
            continue;
        }
        pthread_create(&tid, NULL, thread, (void *)connectfd);
    }
    close(listenfd);
    printf("%s", user_agent_hdr);
    return 0;
}
