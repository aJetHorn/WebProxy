/*
 * proxy.c - CS:APP Web proxy
 */

#include "csapp.h"

/*
 * Function prototypes
 */
void doit(int fd, struct sockaddr_in sockaddr); //proxy
void read_request(rio_t *rp, char *bufreq, char *hostname, int *port, char *uri);
//void serve_static(int fd, char *filename, int filesize);
//void get_filetype(char *filename, char *filetype);
//void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);



/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    int listenfd, connfd, port;
    unsigned int clientlen;
    struct sockaddr_in clientaddr;

    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);
    
    while (1) {
            clientlen = sizeof(clientaddr);
            connfd = Accept(listenfd, (SA *) & clientaddr,  &clientlen);
            doit(connfd, clientaddr); //doproxy, change parameters to pass through sockaddr_in
            Close(connfd);
        }

    exit(0);
}

void doit(int connfd, struct sockaddr_in sockaddr) //proxy
{
    int serverfd, len;
    //struct stat sbuf;
    char bufreq[MAXLINE], bufresp[MAXLINE], hostname[MAXLINE], logstring[MAXLINE], uri[MAXLINE];
    //, method[MAXLINE];
    //uri[MAXLINE], version[MAXLINE];
    //char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio_client, rio_server;
    int port;
    int size = 0; //use for output, doesn't work always!!!
    
    memset(bufreq, 0, sizeof(bufreq));
    memset(bufresp, 0, sizeof(bufresp));
    memset(hostname, 0, sizeof(hostname));
    
    
    /* Read request line and headers */
    Rio_readinitb(&rio_client, connfd);
    //Rio_readlineb(&rio, buf, MAXLINE);
    read_request(&rio_client, bufreq, hostname, &port, uri); //see read request
    
    printf("hostname:%s\tport:%d\n", hostname, port);
    printf("bufrequest: %s\n", bufreq);
    
    
    if((serverfd = open_clientfd(hostname, port)) < 0)
    {
        fprintf(stderr, "open server fd error\n");
        return ;
    }
    rio_readinitb(&rio_server, serverfd);
    
    //read_requesthdrs(&rio);
    
    if(rio_writen(serverfd, bufreq, sizeof(bufreq)) < 0)
    {
        fprintf(stderr, "rio_writen send request error\n");
        return ;
    }
    
    while((len = rio_readnb(&rio_server, bufresp, sizeof(bufresp))) > 0)
    {
        //printf("hostname:%s\tport:%d\nbufresponse:%s\n", hostname, port, bufresp);
        
        if(rio_writen(connfd, bufresp, sizeof(bufresp)) < 0)
        {
            fprintf(stderr, "rio_writen send response error\n");
            return;
        }
        size += len;
        memset(bufresp, 0, sizeof(bufresp)); //reset
    }
    
    format_log_entry(logstring, &sockaddr, uri, size);
    //proxy.log
    FILE *fp; //no error handling, running out of time!!
    fp = fopen("proxy.log", "a+");
    fprintf(fp, "%s\n", logstring);
    fclose(fp);
    

    
    //printf("%s\n", logstring);
    close(serverfd);
    return;
}


void read_request(rio_t *rp, char *bufreq, char *hostname, int *port, char *uri)
{
    char buf[MAXLINE], method[MAXLINE], pathname[MAXLINE];
    
    if(rio_readlineb(rp, buf, MAXLINE) < 0){
        return ;
    }
    sscanf(buf, "%s %s", method, uri);
    parse_uri(uri, hostname, pathname, port);
    sprintf(bufreq, "%s %s HTTP/1.0\r\n", method, uri);
    
    if(rio_readlineb(rp, buf, MAXLINE) < 0){
        return;
    }
    
    while(strcmp(buf, "\r\n")) { //originally used for testing.. extraneous now
        if(strstr(buf, "Host"))
        {
            strcat(bufreq, "Host: ");
            strcat(bufreq, hostname);
            strcat(bufreq, "\r\n");
        }
        else if(strstr(buf, "Proxy-Connection:")){
            strcat(bufreq, "Proxy-Connection: close\r\n");
        }
        else if(strstr(buf, "Connection:")){
            strcat(bufreq, "Connection: close\r\n");
        }
        else{
            strcat(bufreq, buf); //append extra
        }
        
        if(rio_readlineb(rp, buf, MAXLINE) < 0)
        {
            fprintf(stderr, "read request error\n");
            return;
        }
    }
    
    //append header separater
    strcat(bufreq, "\r\n");
    
    return;
}


void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg) //pulled from book
{
    char buf[MAXLINE], body[MAXBUF];
    
    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
    
    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')
        *port = atoi(hostend + 1);

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri, size);
}


