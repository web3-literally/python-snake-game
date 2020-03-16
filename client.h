
#include "global.h"

class client
{
public:
	client(int sock);
	bool startClient();
	bool waitThread();
	void createSnakeGame(int food_x, int food_y);

public:
	int m_nClientSock;				// Client socket
	int m_nId;						// Client id
	int m_nPlayerCount;				// Current player count
	volatile bool m_bIsRunning;		// Flag to show game is running
	bool m_bPythonRunning;			// Flag to show python is running
	char* m_strFIFO_W_Path;			// Write fifo path
	char* m_strFIFO_R_Path;			// Read fifo path
	pthread_mutex_t m_Mutex;		// Mutex to control process synchronization
	pid_t m_PythonPid;				// Process id of python process
	
private:
	pthread_t m_pMainThread;		// Communicate with server thread 
	pthread_t m_pMsgThread;			// Message thread
	pthread_t m_pObserveThread;		// Observe thread
	pthread_t m_pKeyThread;			// Communicate with python thread
};