//Example code: A simple server side code, which echos back the received message.
//Handle multiple socket connections with select and fd_set on Linux

#include "global.h"
#include "server.h"
#include "client.h"

void showHelp();
bool checkArgument(int argc, char* argv[], char *serverAddr, bool & isServer);
int server_connect(int& serverSock, struct sockaddr_in& serverAddr);
int client_connect(char* serverAddr, int &sock);

/*
* Main function
*/
int main(int argc , char *argv[])
{
    char serverAddr[MAX_LEN] = {0};
    bool isServer;    

    // Check arguments
    if (!(checkArgument(argc, argv, serverAddr, isServer))) // If arguments are invalid, then exit    
        return -1;    

    // Act for server
    if (isServer)
    {
        int serverSock;
        struct sockaddr_in serverAddr;

        if (server_connect(serverSock, serverAddr) == -1)   // If create server socket failed, then exit.        
            return -1;        

        server *_pServer = new server(serverSock, serverAddr);
        if (!_pServer->startServer())   // If server can not start, then exit
            exit(EXIT_FAILURE);
        // Wait server exit
        _pServer->waitThread();
        int status = 0;
        wait(&status);  // Wait all children processes exit
        exit(0);
    }
    // Act for client
    else
    {
        int id; int sock;
        if (client_connect(serverAddr, sock) == -1) // If can not connect to server has address 'serverAddr', then exit
            exit(EXIT_FAILURE);
        client *_pClient = new client(sock);
        if (!_pClient->startClient())            
            exit(EXIT_FAILURE);
        // Wait client exit
        _pClient->waitThread();        
        int status = 0;
        wait(&status);  // Wait all children processes exit
        exit(0);
    }
    
    return 0;
}

/**
* Check arguments
* @param : argc - argument count
* @param : argv - inputed arguments
* @param : serverAddr - after analysing, if it is act for client, then third argument is for server address and it is assigned to serverAddr
* @param : isServer - if it is act for server, then true value is assigned to isServer
* @return : if arguments are valid, return true. if not return false
*/
bool checkArgument(int argc, char *argv[], char *serverAddr, bool &isServer)
{
    if (argc < 2 || argc > 4)
    {   // Arguments count must be between 2 and 4
        printf("Invalid arguments.\n");
        showHelp();
        return false;
    }
    if (strcmp(argv[1], "-h") == 0)
    {   // If second argument is '-h', then show help
        showHelp();
        return false;
    }
    if ((strcmp(argv[1], "-c") != 0 && strcmp(argv[1], "-s") != 0) || (strcmp(argv[1], "-s") == 0 && argc == 3) || 
                (strcmp(argv[1], "-c") == 0 && argc == 2))
    {   // Arguments must be type of './snake -c/-s [server addr]'
        printf("Invalid arguments.\n");
        showHelp();
        return false;   
    }
    if (strcmp(argv[1], "-s") == 0)
    {   // If second argument is '-s', then it is server
        isServer = true;
    } else
    {   // If second argument is not '-s', then it is client and server address is assigned.
        isServer = false;
        strncpy(serverAddr, argv[2], MAX_LEN);
    }
    return true;
}

/**
* Show help
*/
void showHelp()
{
    printf("Command type is : \n");
    printf("snake [-c/-s/-h] [server address(in case of argument 2 is -c)]\n");
}

/**
* Client connect function
* @param : serverAddr - address of server
* @param : sock - after connect to server, it is assigned to sock
* @return : if connection is success, return 0. if not return -1.
*/
int client_connect(char* serverAddr, int &sock){
    
    struct sockaddr_in address;
    int valread;
    struct sockaddr_in serv_addr;
    
    char buffer[MAX_BUFFER] = {0};

    // Create new socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Socket creation error \n");
        return -1;
    }

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);   // port is 8080

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, serverAddr, &serv_addr.sin_addr)<=0)
    {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // Connect to server using created socket and server address
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\nConnection Failed \n");
        return -1;
    }

    // After connect server reply and client receive it.
    if ((valread = read( sock , buffer, MAX_BUFFER)) == 0)
    {
        printf("Read Failed\n");
        close(sock);
        return -1;
    }

    int packet_len = *(int *)buffer;
    int msg_code = *(int *)(buffer + sizeof(int));
    int flag = *(int *)(buffer + sizeof(int) * 2);
    if (flag == -1)
    {
        printf("Player count is maximum. Can not play\n");
        return -1;
    } else if (flag == -2)
    {
        printf("Game already start. Can not play\n");
        return -1;
    } else
    {
        printf("Login success. Please wait until start game\n");
    }

    return 0;
}

/**
* Server connect function
* @param : serverSock - after create server, it is assigned with server socket
* @param : serverAddr - after create server, it is assigned with server address
*/
int server_connect(int& serverSock, struct sockaddr_in& serverAddr){
    int opt = TRUE;
    
    //create a master socket
    if( (serverSock = socket(AF_INET , SOCK_STREAM , 0)) == 0)
    {
        perror("socket failed");
        return -1;
    }

    // Set master socket to allow multiple connections
    if( setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0 )
    {
        perror("setsockopt");
        return -1;        
    }

    // Set server address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons( PORT );

    //bind the socket to server address
    if (bind(serverSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr))<0)
    {        
        perror("bind failed");
        return -1;
    }

    printf("Listen on port %d \n", PORT);

    //try to specify maximum of 3 pending connections for the master socket
    if (listen(serverSock, MAX_PLAYER - 1) < 0)
    {
        perror("listen");
        return -1;
    }

    return 0;
}
