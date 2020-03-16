#include "server.h"

void *server_main_thread_function(void *arg);
void *server_msg_thread_function(void *arg);
void *server_key_thread_function(void *arg);
void *server_time_thread_function(void *arg);
void *server_observe_thread_function(void *arg);

void serverMessageRequest(int exceptId, char *msg);
void serverLoginRequest(int sock, int flag);
void serverStartGameRequest(POSITION pos);
void serverEndGameRequest();
void serverTimeSyncRequest();
void serverStateRequest(int exceptId, const char *keystr);
void serverPlayerDisconRequest(int id);
void serverFoodRequest(const char* foodstr);

void serverBackendPlayerDisconRequest(int id);
void serverBackendTimeRequest();
void serverBackendStateRequest(const char *keystr);
void serverBackendFoodRequest(char *foodstr);

void relocateFood(POSITION &pos);

server *_pServer;

server::server(int serverSock, struct sockaddr_in serverAddr)
	: m_nServerSock(serverSock), m_ServerAddr(serverAddr)
{
	_pServer = this;	
}

/**
* Start server
* @return : If starting server success, return true, otherwise return false
*/
bool server::startServer()
{
	int res;
	m_bIsRunning = true;
	m_bGameStart = false;
	m_nPlayerCount = 1;
	m_PythonPid = 0;

    // Init mutex
    res = pthread_mutex_init(&m_Mutex, NULL);

    // Start observe thread
    res = pthread_create(&m_pObserveThread,NULL, server_observe_thread_function,NULL);
    if(res != 0)
    {
        perror("Observe thread creation failed");
        return false;        
    }  

    // Start main communication thread
	res = pthread_create(&m_pMainThread,NULL, server_main_thread_function,NULL);
    if(res != 0)
    {
        perror("Main thread creation failed");
        return false;        
    }    

    // Start message thread
    res = pthread_create(&m_pMsgThread,NULL, server_msg_thread_function,NULL);
    if(res != 0)
    {
        perror("Message thread creation failed");
        return false;
    }

    return true;    
}

/**
* Wait all threads exit
* @return : if all threads have exited successfully, then return true, otherwise return false
*/
bool server::waitThread()
{
	int res;
	void *thread_result;

    // Wait observe thread exit
    res = pthread_join(m_pObserveThread, &thread_result);
    if(res != 0)
    {
        perror("Observe thread join failed");        
    }

    // If observe thread has exited, then other threads must be exited

    // Send cancel request to main thread
    pthread_cancel(m_pMainThread);
    // Wait main thread exit
	res = pthread_join(m_pMainThread, &thread_result);
    if(res != 0)
    {
        perror("Main thread join failed");        
    }    

    // Send cancel request to time thread
    pthread_cancel(m_pTimeThread);
    // Wait time thread exit
    res = pthread_join(m_pTimeThread, &thread_result);
    if(res != 0)
    {
        perror("Time thread join failed");        
    }    

    // Send cancel request to python communication thread
    pthread_cancel(m_pKeyThread);
    // Wait python communication thread exit
    res = pthread_join(m_pKeyThread, &thread_result);
    if(res != 0)
    {
        perror("Key thread join failed");
    }    
    
    // Send cancel request to message thread
    pthread_cancel(m_pMsgThread);
    // Wait message thread exit
    res = pthread_join(m_pMsgThread, &thread_result);
    if(res != 0)
    {
        perror("Message thread join failed");
    }    
    
    // Destroy mutex
    pthread_mutex_destroy(&m_Mutex);

    return true;
}

/**
* Start snake game
* @param : pos - food initial position
*/
void server::createSnakeGame(POSITION pos)
{
	char cmd[MAX_LEN];
	
	m_strFIFO_W_Path = (char *)calloc(MAX_PATH, sizeof(char));
	m_strFIFO_R_Path = (char *)calloc(MAX_PATH, sizeof(char));

    // Make read/write fifo in path of '/tmp'
	sprintf(m_strFIFO_R_Path, "/tmp/snakegame_fifo_w_%d", m_nId);
	sprintf(m_strFIFO_W_Path, "/tmp/snakegame_fifo_r_%d", m_nId);

	mkfifo(m_strFIFO_W_Path, 0666);
	mkfifo(m_strFIFO_R_Path, 0666);
	
    // Start python communication thread
	int res = pthread_create(&m_pKeyThread,NULL, server_key_thread_function,NULL);
    if(res != 0)
    {
        perror("Key thread creation failed");
        m_bIsRunning = false; m_bGameStart = false;
        return;
    }
    
    // Start time synchroniztion thread
    res = pthread_create(&m_pTimeThread,NULL, server_time_thread_function,NULL);
    if(res != 0)
    {
        perror("Time thread creation failed");
        m_bIsRunning = false; m_bGameStart = false;
        return;
    }

    m_bGameStart = true;
    sprintf(cmd, "python snake.py %d %d %d %d", m_nPlayerCount, m_nId, pos.xpos, pos.ypos);
    // Create child process
    m_PythonPid = fork();
    if (m_PythonPid == 0)
    {
        // Run python game in child process
        system(cmd);
        exit(0);
    }    

}

/**
* Server main communication thread function
*/
void *server_main_thread_function(void *arg)
{
    int new_socket , activity, i , valread , sd;
    int max_sd;

    //initialise all client_socket[] to 0 so not checked
    for (i = 0; i < MAX_PLAYER; i++)
    {
        _pServer->m_ClientSockArr[i] = 0;
    }

    char buffer[1025];  //data buffer of 1K

    //set of socket descriptors
    fd_set readfds;
    
    //accept the incoming connection
    int addrlen = sizeof(_pServer->m_ServerAddr);
    puts("Waiting for incoming connections ...");

    while(_pServer->m_bIsRunning)
    {
        //clear the socket set
        FD_ZERO(&readfds);

        //add master socket to set
        FD_SET(_pServer->m_nServerSock, &readfds);
        max_sd = _pServer->m_nServerSock;

        //add child sockets to set
        for ( i = 1 ; i < MAX_PLAYER ; i++)
        {
            //socket descriptor
            sd = _pServer->m_ClientSockArr[i];

            //if valid socket descriptor then add to read list
            if(sd > 0)
                FD_SET( sd , &readfds);

            //highest file descriptor number, need it for the select function
            if(sd > max_sd)
                max_sd = sd;
        }

        //wait for an activity on one of the sockets , timeout is NULL ,
        //so wait indefinitely
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;        

        activity = select( max_sd + 1 , &readfds , NULL , NULL , &timeout);
        
        if ((activity < 0) && (errno != EINTR))
        {
            continue;            
        }        

        //If something happened on the master socket ,
        //then its an incoming connection
        if (FD_ISSET(_pServer->m_nServerSock, &readfds))
        {
            if ((new_socket = accept(_pServer->m_nServerSock, (struct sockaddr *)&_pServer->m_ServerAddr, (socklen_t*)&addrlen))<0)
            {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            //inform user of socket number - used in send and receive commands
            printf("New connection , socket fd is %d , ip is : %s , port : %d \n" , new_socket , 
            						inet_ntoa(_pServer->m_ServerAddr.sin_addr) , ntohs(_pServer->m_ServerAddr.sin_port));
            
            //add new socket to array of sockets
            for (i = 1; i < MAX_PLAYER; i++)
            {
                //if position is empty
                if( _pServer->m_ClientSockArr[i] == 0 )
                {
                    _pServer->m_ClientSockArr[i] = new_socket;
                    printf("Player %d has joined.\n" , i + 1);

                    break;
                }
            }

            // If player count has exceeded MAX_PLAYER, then send client refuse request
            if (i == MAX_PLAYER)
            {
                printf("New player from %s can not play because of maximum player count\n", inet_ntoa(_pServer->m_ServerAddr.sin_addr));
                serverLoginRequest(new_socket, -1);
            } else if (_pServer->m_bGameStart)
            {   // If game has already started, then send client refuse request
                printf("New player from %s can not play because game already start\n", inet_ntoa(_pServer->m_ServerAddr.sin_addr));
                serverLoginRequest(new_socket, -2);
            } else
            {   // Increase player count, and send client request agree request
            	_pServer->m_nPlayerCount ++;
                printf("New player created. Current player count is %d\n", _pServer->m_nPlayerCount);                
                serverLoginRequest(new_socket, 0);
            }
        }

        //else its some IO operation on some other socket
        for (i = 1; i < MAX_PLAYER; i++)
        {
            sd = _pServer->m_ClientSockArr[i];

            if (FD_ISSET(sd , &readfds))
            {
                //Check if it was for closing , and also read the
                //incoming message
                if ((valread = read(sd , buffer, MAX_BUFFER)) == 0)
                {
                    //Somebody disconnected , get his details and print
                    getpeername(sd, (struct sockaddr*)&_pServer->m_ServerAddr, (socklen_t*)&addrlen);
					_pServer->m_nPlayerCount--;

					printf("Player %d, Host disconnected , ip %s , port %d. Current player count is %d \n" , i + 1,
                          inet_ntoa(_pServer->m_ServerAddr.sin_addr) , ntohs(_pServer->m_ServerAddr.sin_port), _pServer->m_nPlayerCount);

                    //Close the socket and mark as 0 in list for reuse
                    close( sd );
                    _pServer->m_ClientSockArr[i] = 0;
                    
                    if (_pServer->m_bGameStart)
                    {   // Send player disconnect request to all clients and python
                        serverPlayerDisconRequest(i + 1);
                        serverBackendPlayerDisconRequest(i + 1);
                    }
                }

                //Echo back the message that came in
                else
                {
                    int packet_len = *(int *)buffer;
                    int msg_code = *(int *)(buffer + sizeof(int));
                    if (msg_code == USER_MSG)
                    {   // User message
                        char *msg = (char *)calloc(packet_len - 2 * sizeof(int) + 64, 1);
                        sprintf(msg, "Message from Player%d : %s\n", i + 1, buffer + sizeof(int) * 2);
                        printf("[backend] %s", msg);
                    	serverMessageRequest(i + 1, msg);
                        free(msg);
                    } else if (msg_code == STATE)
                    {   // Client state
                    	char *statestr = (char *)calloc(packet_len - 2 * sizeof(int), 1);
	                    memcpy(statestr, buffer + sizeof(int) * 2, packet_len - 2 * sizeof(int));
	                    serverBackendStateRequest(statestr);
	                    serverStateRequest(i + 1, statestr);
	                    free(statestr);
                    } else if (msg_code == FOOD)
                    {   // Food request
                        POSITION pos;
                        relocateFood(pos);
                        char tmp[32];
                        sprintf(tmp, "FOOD:%d:%d\n", pos.xpos, pos.ypos);
                        serverFoodRequest(tmp);
                        serverBackendFoodRequest(tmp);
                    } else if (msg_code == END)
                    {   // End request
                        // Player i + 1 exit
                        serverPlayerDisconRequest(i + 1);
                        serverBackendPlayerDisconRequest(i + 1);
                    }
                }
            }
        }
    }

    close(_pServer->m_nServerSock);
}

/**
* Python thread function
*/
void *server_key_thread_function(void *arg)
{
	int fd = open(_pServer->m_strFIFO_R_Path, O_RDONLY);
	if (fd == -1)
	{
		printf("[backend] Open named pipe failed\n");
		return NULL;
	}
	char ch;
	std::string buf;
    while (_pServer->m_bIsRunning)
    {
    	if (read(fd, &ch, 1) == 1)
    	{    		
    		if (ch != '\n' && ch != '\0')
    		{
    			buf += ch;
    		} else {
    			const char *fifostr = buf.c_str();                
    			if (strncmp(fifostr, "FOODHIT", 7) == 0)
    			{	// Food hit to snake. Relocate it                    
                    POSITION pos;
                    relocateFood(pos);                    
                    char tmp[32];
                    sprintf(tmp, "FOOD:%d:%d\n", pos.xpos, pos.ypos);
                    serverBackendFoodRequest(tmp);
        			serverFoodRequest(tmp);
    			} else if (strncmp(fifostr, "EXIT", 4) == 0)
                {   // Python game end. Also all clients must end                    
                    _pServer->m_bIsRunning = false;
                    serverEndGameRequest();                    
                } else if (strncmp(fifostr, "STATE", 5) == 0)
                {   // Send my state to all clients
                    serverStateRequest(1, fifostr);
                }  else if (strncmp(fifostr, "WIN", 3) == 0)
                {   // Send win state to all clients
                    serverStateRequest(1, fifostr);
                }

    			buf = "";
    		}
    	}
    }  
}

/**
* Server time synchronization thread
*/
void *server_time_thread_function(void *arg)
{
    while (_pServer->m_bIsRunning)
    {    	
    	usleep(50000);	// 5ms interval
        // Send time synchronization request to all clients
        serverTimeSyncRequest();
        // Send time synchronization request to python
        serverBackendTimeRequest();
    }
}

/**
* Server observe thread function
*/
void *server_observe_thread_function(void *arg)
{
    while (_pServer->m_bIsRunning)
    {       
        usleep(50000);  // 5ms interval        
    }    
}

/**
* Server message thread function
*/
void *server_msg_thread_function(void *arg)
{    
    while (_pServer->m_bIsRunning)
    {
        char msg[MAX_BUFFER];
        // Get string from stdin
        if (fgets(msg, MAX_BUFFER, stdin) != NULL)
        {        	
        	if (strncmp(msg, "-c", 2) == 0)	// '-c' means game control command
        	{
        		if (!_pServer->m_bGameStart && strncmp(msg + 3, "start", 5) == 0)		// start game
        		{ // '-c start' means game start
        			_pServer->m_nId = 1;
                    POSITION pos;
                    // relocate food
                    relocateFood(pos);        			
        			serverStartGameRequest(pos);
                    _pServer->createSnakeGame(pos);
        		} else if (strncmp(msg + 3, "exit", 4) == 0)		// end game
        		{ // '-c exit' means game end
        			serverEndGameRequest();
        			_pServer->m_bIsRunning = false;
        		}
        	} else if (strncmp(msg, "-m", 2) == 0)
        	{  // '-m' means message command
        		char* buf = (char *)calloc(strlen(msg) + 32, sizeof(char));
        		sprintf(buf, "Message from Player1 : %s\n", msg + 2);                
        		serverMessageRequest(1, buf);
        		free(buf);
        	}
        }
    }
}

/**
* Reply to client who wants to login
* @param : sock - client socket
* @param : flag - packet type (-1 : exceed maximum, -2 : game start already, 0 : agree)
*/
void serverLoginRequest(int sock, int flag)
{
    int packet_len = (1 + 1 + 1) * sizeof(int);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = LOGIN;
    *(int *)(packet + 8) = flag;
    pthread_mutex_lock(&_pServer->m_Mutex);
    send(sock, packet, packet_len, 0);
    pthread_mutex_unlock(&_pServer->m_Mutex);
    free(packet);
}

/**
* Send all clients message
* @param : exceptId - do not send message to clients of this id
* @param : msg - message content
*/
void serverMessageRequest(int exceptId, char *msg)
{
	int packet_len = (1 + 1) * sizeof(int) + strlen(msg);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = USER_MSG;
    strncpy(packet + 8, msg, strlen(msg));

	for (int i = 1; i < MAX_PLAYER; ++i)
    {
        if (_pServer->m_ClientSockArr[i] > 0 && i + 1 != exceptId)
        {
            pthread_mutex_lock(&_pServer->m_Mutex);
		    send(_pServer->m_ClientSockArr[i], packet, packet_len, 0);
		    pthread_mutex_unlock(&_pServer->m_Mutex);
        }
    }
    
    free(packet);
}

/**
* Send start game request to all clients
* @param : pos - food initial position
*/
void serverStartGameRequest(POSITION pos)
{
	int packet_len = (1 + 1 + 1 + 1 + 2) * sizeof(int);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = START;
    *(int *)(packet + 8) = _pServer->m_nPlayerCount;
    *(int *)(packet + 16) = pos.xpos;
    *(int *)(packet + 20) = pos.ypos;

	for (int i = 1; i < MAX_PLAYER; ++i)
    {
        if (_pServer->m_ClientSockArr[i] > 0)
        {
        	*(int *)(packet + 12) = i + 1;    
		    pthread_mutex_lock(&_pServer->m_Mutex);    
		    send(_pServer->m_ClientSockArr[i], packet, packet_len, 0);
		    pthread_mutex_unlock(&_pServer->m_Mutex);
        }
    }
    
    free(packet);	
}

/**
* Send end game request to all clients
*/
void serverEndGameRequest()
{
	int packet_len = (1 + 1) * sizeof(int);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = END;

	for (int i = 1; i < MAX_PLAYER; ++i)
	{
	    if (_pServer->m_ClientSockArr[i] > 0)
	    {
	    	pthread_mutex_lock(&_pServer->m_Mutex);
		    send(_pServer->m_ClientSockArr[i], packet, packet_len, 0);   
		    pthread_mutex_unlock(&_pServer->m_Mutex);	        
	    }
    }

    free(packet);		
}

/**
* Send one player state to clients
* @param : exceptId - do not send request to the client of this id
* @param : statestr - state string
*/
void serverStateRequest(int exceptId, const char *statestr)
{
    int packet_len = (1 + 1) * sizeof(int) + strlen(statestr);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = STATE;
    strncpy(packet + 8, statestr, strlen(statestr));

    for (int i = 1; i < MAX_PLAYER; ++i)
	{
	    if (_pServer->m_ClientSockArr[i] > 0 && i + 1 != exceptId)
	    {
	    	pthread_mutex_lock(&_pServer->m_Mutex);
		    send(_pServer->m_ClientSockArr[i], packet, packet_len, 0);   
		    pthread_mutex_unlock(&_pServer->m_Mutex);	        
	    }
    }

    free(packet);
}

/**
* Send all clients server time synchronization request
*/
void serverTimeSyncRequest()
{
	int packet_len = (1 + 1) * sizeof(int);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = TIME_SYNC;

	for (int i = 1; i < MAX_PLAYER; ++i)
	{
	    if (_pServer->m_ClientSockArr[i] > 0)
	    {
	    	pthread_mutex_lock(&_pServer->m_Mutex);
		    send(_pServer->m_ClientSockArr[i], packet, packet_len, 0);   
		    pthread_mutex_unlock(&_pServer->m_Mutex);	        
	    }
    }

    free(packet);
}

/**
* Send food state string to all clients
* @param : foodstr - food state string
*/
void serverFoodRequest(const char * foodstr)
{
	int packet_len = (1 + 1) * sizeof(int) + strlen(foodstr);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = FOOD;
    strncpy(packet + 8, foodstr, strlen(foodstr));
        
	for (int i = 1; i < MAX_PLAYER; ++i)
	{    
	    if (_pServer->m_ClientSockArr[i] > 0)
	    {            
 	    	pthread_mutex_lock(&_pServer->m_Mutex);            
		    send(_pServer->m_ClientSockArr[i], packet, packet_len, 0);            
		    pthread_mutex_unlock(&_pServer->m_Mutex);
	    }
    }

    free(packet);
}

/**
* Send player disconnect request to all clients
* @param : id - the id of client has disconnected
*/
void serverPlayerDisconRequest(int id)
{
    int packet_len = (1 + 1 + 1) * sizeof(int);
    char *packet = (char *)calloc(packet_len, sizeof(char));
    *(int *)packet = packet_len;
    *(int *)(packet + 4) = DISCON;
    *(int *)(packet + 8) = id;    
    
    for (int i = 1; i < MAX_PLAYER; ++i)
    {        
        if (_pServer->m_ClientSockArr[i] > 0)
        {
            pthread_mutex_lock(&_pServer->m_Mutex);
            send(_pServer->m_ClientSockArr[i], packet, packet_len, 0);   
            pthread_mutex_unlock(&_pServer->m_Mutex);           
        }
    }

    free(packet);    
}
    
/**
* Send python player disconnect request
* @param : id - the player has disconnected
*/
void serverBackendPlayerDisconRequest(int id)
{
    if (!_pServer->m_bIsRunning)
    {        
        return;
    }   

    int fd = open(_pServer->m_strFIFO_W_Path, O_WRONLY);
    if (fd == -1)
    {
        printf("[backend] Open fifo failed in disconnect function\n");
        return;
    }
    char tmp[12];
    sprintf(tmp, "DISC:%d\n", id);
    
    write(fd, tmp, 12);
    close(fd);
}

/**
* Send time synchronization request to python
*/
void serverBackendTimeRequest()
{
    if (!_pServer->m_bIsRunning)
    {
        return;
    }
    
    int fd = open(_pServer->m_strFIFO_W_Path, O_WRONLY);
    if (fd == -1)
    {
        printf("[backend] Open fifo failed in time function\n");
        return;
    }    
    write(fd, "TIME\n", 5);
    close(fd);
}

/**
* Send player state to python
* @param : statestr - player state string
*/
void serverBackendStateRequest(const char *statestr)
{   
    if (!_pServer->m_bIsRunning)
    {
        return;
    }
    
    std::string buf(statestr);
    buf = buf + '\n';
    int fd = open(_pServer->m_strFIFO_W_Path, O_WRONLY);
    if (fd == -1)
    {
        printf("[backend] Open fifo failed in key function\n");
        return;
    }
    write(fd, buf.c_str(), strlen(buf.c_str()));    
    close(fd);
}

/**
* Send food state to python
* @param : foodstr - food state string
*/
void serverBackendFoodRequest(char *foodstr)
{
    if (!_pServer->m_bIsRunning)
    {
        return;
    }
    
    std::string buf(foodstr);
    buf = buf + '\n';
    int fd = open(_pServer->m_strFIFO_W_Path, O_WRONLY);
    if (fd == -1)
    {
        printf("[backend] Open fifo failed in key function\n");
        return;
    }    
    write(fd, buf.c_str(), strlen(buf.c_str()));
    
    close(fd);
}

/**
* Relocate food
* @param : pos - new postion of food
*/
void relocateFood(POSITION &pos)
{    
    
    int x = 0, y = 0;
    while (true)
    {
        time_t cur = time(NULL);
        srand(cur);
        x = rand() % DISP_WIDTH;
        y = rand() % DISP_HEIGHT;
        x = x - x % 10;
        y = y - y % 10;
        if (x > 20 && x < DISP_WIDTH - 20 && y > 20 && y < DISP_HEIGHT - 20)
        {
            break;
        }
    }    
    
    pos.xpos = x;
    pos.ypos = y;
}
