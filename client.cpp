#include "client.h"

void *client_main_thread_function(void *arg);
void *client_msg_thread_function(void *arg);
void *client_key_thread_function(void *arg);
void *client_observe_thread_function(void *arg);

void clientMessageRequest(int sock, const char *msg);
void clientStateRequest(int sock, const char *keystr);
void clientFoodRequest(int sock);
void clientEndRequest(int sock);

void clientBackendStateRequest(const char *keystr);
void clientBackendTimeRequest();
void clientBackendExitRequest();
void clientBackendFoodRequest(char *foodstr);
void clientBackendPlayerDisconRequest(int id);

client *_pClient;

client::client(int sock)
	: m_nClientSock(sock)
{
	_pClient = this;    
}

/**
* Start client
* @return : if start is success, then return true, otherwise return false
*/
bool client::startClient()
{
	int res;
    m_bIsRunning = true;    
    m_nPlayerCount = 0;

    // Start observe thread
    res = pthread_create(&m_pObserveThread,NULL, client_observe_thread_function,NULL);
    if(res != 0)
    {
        perror("Observe thread creation failed");
        return false;        
    }

    // Start server communication thread
	res = pthread_create(&m_pMainThread,NULL, client_main_thread_function,NULL);
    if(res != 0)
    {
        perror("Main thread creation failed");
        return false;        
    }
    
    // Start message thread
    res = pthread_create(&m_pMsgThread,NULL, client_msg_thread_function,NULL);
    if(res != 0)
    {
        perror("Message thread creation failed");
        return false;
    }

    return true;    
}

/**
* Wait all thread exit
* @return : if waiting success and all thread have joined, then return true, otherwise return false
*/
bool client::waitThread()
{
	int res;
    void *thread_result;
    
    // Wait observe thread exit
    res = pthread_join(m_pObserveThread, &thread_result);
    if (res != 0)
    {
        perror("Observe thread join failed");
    }

    // When observe thread has exited, then other thread have to also exit

    // Send cancel request to server communication thread
    pthread_cancel(m_pMainThread);
    // Wait server communicatin thread exit
	res = pthread_join(m_pMainThread, &thread_result);
    if (res != 0)
    {
        perror("Main thread join failed");
    }

    // Send cancel request to python communication thread
    pthread_cancel(m_pKeyThread);
    // Wait python communication thread exit
    res = pthread_join(m_pKeyThread, &thread_result);
    if (res != 0)
    {
        perror("Key thread join failed");        
    }   

    // Send cancel request to message thread
    pthread_cancel(m_pMsgThread);
    // Wait message thread exit
    res = pthread_join(m_pMsgThread, &thread_result);
    if (res != 0)
    {
        perror("Message thread join failed");        
    }
    return true;
}

/**
* Start game
* @param : food_x, food_y - position of food
*/
void client::createSnakeGame(int food_x, int food_y)
{
    char cmd[MAX_LEN];
    int res;
    m_strFIFO_W_Path = (char *)calloc(MAX_PATH, sizeof(char));
    m_strFIFO_R_Path = (char *)calloc(MAX_PATH, sizeof(char));
    // Make read/write fifo in the path of '/tmp' 
    sprintf(m_strFIFO_R_Path, "/tmp/snakegame_fifo_w_%d", m_nId);
    sprintf(m_strFIFO_W_Path, "/tmp/snakegame_fifo_r_%d", m_nId);

    mkfifo(m_strFIFO_W_Path, 0666);
    mkfifo(m_strFIFO_R_Path, 0666);

    // Start python communication thread
    res = pthread_create(&m_pKeyThread,NULL, client_key_thread_function,NULL);
    if(res != 0)
    {
        perror("Key thread creation failed");
        return;
    }    

    sprintf(cmd, "python snake.py %d %d %d %d", m_nPlayerCount, m_nId, food_x, food_y);   
    
    // Create new child process
    m_PythonPid = fork();
    if (m_PythonPid == 0)
    {   // Run python game in child process
        system(cmd);        
        exit(0);
    }  
}

/**
* Server communicatin thread function
*/
void *client_main_thread_function(void *arg)
{
    int new_socket , activity, i , valread , sd;
    int max_sd;
    
    char buffer[1025];  //data buffer of 1K

    //set of socket descriptors
    fd_set readfds;
        
    while(_pClient->m_bIsRunning)
    {        
        //clear the socket set
        FD_ZERO(&readfds);

        //add client socket to set
        FD_SET(_pClient->m_nClientSock, &readfds);        

        //wait for an activity on one of the sockets , timeout is 1s        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        activity = select( _pClient->m_nClientSock + 1 , &readfds , NULL , NULL , &timeout);

        if ((activity < 0) && (errno != EINTR))
        {
            printf("select error");
            continue;
        }
        
        if (FD_ISSET(_pClient->m_nClientSock, &readfds))
        {   // Something happened on the client socket
            if ((valread = read( _pClient->m_nClientSock , buffer, MAX_BUFFER)) == 0)
            {
                // Socket has disconnected, then send exit request to python
                clientBackendExitRequest();
                // Client running is false
                _pClient->m_bIsRunning = false;
            }
            else
            {
                int packet_len = *(int *)buffer;
                int msg_code = *(int *)(buffer + sizeof(int));               
                
                if (_pClient->m_bIsRunning == false)
                    continue;

                if (msg_code == USER_MSG)
                {   // User message
                    char *msg = (char *)calloc(packet_len - 2 * sizeof(int), 1);
                    memcpy(msg, buffer + sizeof(int) * 2, packet_len - 2 * sizeof(int));
                    printf("[backend] %s", msg);
                    free(msg);
                } else if (msg_code == STATE) {
                    // Other client state
                    char *statestr = (char *)calloc(packet_len - 2 * sizeof(int), 1);
                    memcpy(statestr, buffer + sizeof(int) * 2, packet_len - 2 * sizeof(int));                    
                    clientBackendStateRequest(statestr);
                    free(statestr);
                } else if (msg_code == TIME_SYNC) {
                    // Server time synchronization
                    clientBackendTimeRequest();                    
                } else if (msg_code == FOOD) {
                    // Food state                
                    char *foodstr = (char *)calloc(packet_len - 2 * sizeof(int), 1);
                    memcpy(foodstr, buffer + sizeof(int) * 2, packet_len - 2 * sizeof(int));                    
                    clientBackendFoodRequest(foodstr);
                    free(foodstr);
                } else if (msg_code == START)
                {   // Game start
                    int player_count = *(int *)(buffer + sizeof(int) * 2);
                    int id = *(int *)(buffer + sizeof(int) * 3);
                    int food_x = *(int *)(buffer + sizeof(int) * 4);
                    int food_y = *(int *)(buffer + sizeof(int) * 5);
                    _pClient->m_nId = id;
                    _pClient->m_nPlayerCount = player_count;                    
                    _pClient->createSnakeGame(food_x, food_y);                    
                } else if (msg_code == END)
                {   // Game end
                    clientBackendExitRequest();
                    _pClient->m_bIsRunning = false;                    
                } else if (msg_code == DISCON)
                {   // Other client disconnect      
                    int id = *(int *)(buffer + sizeof(int) * 2);
                    clientBackendPlayerDisconRequest(id);
                }                
            }
        }        
    }
    close(_pClient->m_nClientSock);    
    return NULL;
}

/**
* Communication with python thread function
*/
void *client_key_thread_function(void *arg)
{
    int fd = open(_pClient->m_strFIFO_R_Path, O_RDONLY);
    if (fd == -1)
    {
        printf("[backend] Open fifo failed in key function\n");
        return NULL;
    }
    char ch;
    std::string buf;
    while (_pClient->m_bIsRunning)
    {
        if (read(fd, &ch, 1) == 1)
        {           
            if (ch != '\n' && ch != '\0')
            {
                buf += ch;
            } else {
                const char *fifostr = buf.c_str();                

                if (strncmp(fifostr, "EXIT", 4) == 0)
                {   // Python game end. exit backend
                    printf("[backend] Exit received\n");
                    _pClient->m_bIsRunning = false;
                    //clientEndRequest(_pClient->m_nClientSock);
                } else if (strncmp(fifostr, "RECVEXIT", 8) == 0)
                {   // Python recv exit message, I can exit
                    printf("[backend] python get my exit message\n");
                } else if (strncmp(fifostr, "FOODHIT", 7) == 0)
                {   // Python has hit to food, send server this infomation
                    clientFoodRequest(_pClient->m_nClientSock);
                } else if (strncmp(fifostr, "STATE", 5) == 0)
                {   // Python state has changed, send server this infomation
                    clientStateRequest(_pClient->m_nClientSock, fifostr);
                } else if (strncmp(fifostr, "WIN", 3) == 0)
                {   // Python has won, send server this infomation
                    clientStateRequest(_pClient->m_nClientSock, fifostr);
                }
                buf = "";
            }
        }
    }

    close(fd);    
    return NULL;
}

/**
* Client observe thread function
*/
void *client_observe_thread_function(void *arg)
{    
    while (_pClient->m_bIsRunning)
    {
        usleep(50000);
    }
    
    return NULL;
}

/**
* Client message thread function
*/
void *client_msg_thread_function(void *arg)
{
    while (_pClient->m_bIsRunning)
    {
        char msg[MAX_BUFFER];
        // Get string from stdin
        if (fgets(msg, MAX_BUFFER, stdin) != NULL)
        {
            if (strncmp(msg, "-m", 2) == 0)     // message command
            {   // '-m' indicates that this is message, send server this message
                clientMessageRequest(_pClient->m_nClientSock, msg + 2);
            }
        }
    }
    printf("[backend] Exit from msg thread\n");
    return NULL;
}

/**
* Send inputed message to server
* @param : sock - client socket
* @param : mag - client message
*/
void clientMessageRequest(int sock, const char *msg)
{
    int packet_len = (1 + 1) * sizeof(int) + strlen(msg);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = USER_MSG;
    strncpy(packet + 8, msg, strlen(msg));    
    send(sock, packet, packet_len, 0);       
    free(packet);
}

/**
* Send server client state
* @param : sock - client socket
* @param : statestr - client state string
*/
void clientStateRequest(int sock, const char *statestr)
{
    int packet_len = (1 + 1) * sizeof(int) + strlen(statestr);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = STATE;
    strncpy(packet + 8, statestr, strlen(statestr));    
    send(sock, packet, packet_len, 0);    
    free(packet);
}

/**
* Send server food relocate request
* @param : sock - client socket
*/
void clientFoodRequest(int sock)
{
    int packet_len = (1 + 1) * sizeof(int);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = FOOD;    
    send(sock, packet, packet_len, 0);    
    free(packet);
}

/**
* Send server end request when python exit
* @param : sock - client socket
*/
void clientEndRequest(int sock)
{
    int packet_len = (1 + 1) * sizeof(int);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = END;
    send(sock, packet, packet_len, 0);    
    free(packet);   
}

/**
* Send python other client state
* @param : statestr - other client state string
*/
void clientBackendStateRequest(const char *statestr)
{    
    std::string buf(statestr);
    buf = buf + '\n';
    int fd = open(_pClient->m_strFIFO_W_Path, O_WRONLY);
    if (fd == -1)
    {
        printf("[backend] Open fifo failed in key request\n");
        return;
    }
    write(fd, buf.c_str(), strlen(buf.c_str()));
    close(fd);
}

/**
* Send python time synchronization request
*/
void clientBackendTimeRequest()
{
    int fd = open(_pClient->m_strFIFO_W_Path, O_WRONLY);
    if (fd == -1)
    {
        printf("[backend] Open fifo %s failed in time request\n", _pClient->m_strFIFO_W_Path);
        return;
    }
    
    write(fd, "TIME\n", 5);    
    close(fd);
}

/**
* Send python exit request
*/
void clientBackendExitRequest()
{    
    int fd = open(_pClient->m_strFIFO_W_Path, O_WRONLY);
    if (fd == -1)
    {
        printf("[backend] Open fifo %s failed in time request\n", _pClient->m_strFIFO_W_Path);
        return;
    }
    write(fd, "EXIT\n", 5);
    close(fd);   
}

/**
* Send python food relocate request
* @param : foodstr - food position string
*/
void clientBackendFoodRequest(char *foodstr)
{
    std::string buf(foodstr);
    buf = buf + '\n';
    int fd = open(_pClient->m_strFIFO_W_Path, O_WRONLY);
    if (fd == -1)
    {
        printf("[backend] Open fifo failed in food request\n");
        return;
    }
    
    write(fd, buf.c_str(), strlen(buf.c_str()));
    close(fd);  
}

/**
* Send python other player disconnect request
* @param : id - id of client has disconnected
*/
void clientBackendPlayerDisconRequest(int id)
{
    int fd = open(_pClient->m_strFIFO_W_Path, O_WRONLY);
    if (fd == -1)
    {
        printf("[backend] Open fifo failed in disconnect request\n");
        return;
    }
    char tmp[12] = {0};
    sprintf(tmp, "DISC:%d\n", id);
    write(fd, tmp, 12);
    close(fd);
}
