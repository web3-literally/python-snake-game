
#ifndef GLOBAL_H
#define GLOBAL_H

#include <stdio.h>
#include <iostream>
#include <cstdio>
#include <string>
#include <string.h>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/time.h>

#define MAX_BUFFER 	1024
#define MAX_PATH	256
#define MAX_LEN		64
#define MAX_PENDING	3				// Max numbers of socket that pend in server queue
#define MAX_PLAYER	4				// Game player number
#define TRUE   		1
#define FALSE  		0				
#define PORT 		8080			// Game play port

#define DISP_WIDTH	800				// Game scene width
#define DISP_HEIGHT	600				// Game scene height

#define	LOGIN			0x01		// Login code
#define USER_MSG 		0x02		// Message code
#define START			0x03		// Game start code
#define END 			0x04		// Game end code
#define STATE			0x05		// Game state code
#define TIME_SYNC		0x06		// Time synchronization code
#define FOOD			0x07		// Food state code
#define DISCON			0x08		// Client disconnect code

// Struct to show position
struct POSITION
{
	int xpos;
	int ypos;
};

#endif
