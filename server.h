
#include "global.h"

class server
{
public:
	server(int serverSock, struct sockaddr_in serverAddr);
	bool startServer();
	bool waitThread();
	void createSnakeGame(POSITION pos);

public:
	int m_nServerSock;						// Server socket
	struct sockaddr_in m_ServerAddr;		// Server address has binded to server socket
	int m_nId;								// Server id in game
	int m_nPlayerCount;						// Current player count
	int m_ClientSockArr[MAX_PLAYER];		// Client socket array
	volatile bool m_bIsRunning;				// Flag to show server is running	
	bool m_bGameStart;						// Flag to show game has started
	char* m_strFIFO_W_Path;					// Write fifo path
	char* m_strFIFO_R_Path;					// Read fifo path
	pthread_mutex_t m_Mutex;				// Mutex to control process synchronization
	pid_t m_PythonPid;						// Python process id
	
private:
	pthread_t m_pMainThread;				// Main communication thread
	pthread_t m_pMsgThread;					// Message thread
	pthread_t m_pTimeThread;				// Time count thread
	pthread_t m_pKeyThread;					// Python communication thread
	pthread_t m_pObserveThread;				// Observe thread
};