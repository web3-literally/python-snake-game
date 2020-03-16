#! /usr/bin/env python

import errno
import os
import pygame
import random
import string
import sys
import threading
from multiprocessing import Process, Lock, Queue

# Screen Size (The default is set to smallest screen resolution for PCs)
# TODO: Change this to detect the user's current screen resolution and use those dimensions for fullscreen.
WIDTH = 800
HEIGHT = 600

# Score tracking 
score = 0

# Fifo read/write path
FIFO_W_PATH = ""
FIFO_R_PATH = ""

# Sync to server time
IsSync = False;

# Array to represent snakes
_pSnakeArr = []

# Array to show die order
_pDieOrder = []

# Food instance
_pFood = None

# Number of players
_pPlayerNum = 0

# Self index
_pSelfIndex = -1

# Keys for control snake
_pKeyArr = [pygame.K_UP, pygame.K_DOWN, pygame.K_LEFT, pygame.K_RIGHT]

# Colors : Red, Green, Blue, White
_pColorArr = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)]

# Initial Position of the players
_pInitPosArr = [(WIDTH / 5, HEIGHT / 4), (WIDTH / 5, (HEIGHT * 3) / 4), ((WIDTH * 4) / 5, HEIGHT / 4), ((WIDTH * 4) / 5, (HEIGHT * 3) / 4)]

# Display string array
_pDispStrArr = ["Player 1: Red", "Player 2: Green", "Player 3: Blue", "Player 4: White"]

# Winner string array
_pWinStrArr = ["Winner: Player 1! ", "Winner: Player 2! ", "Winner: Player 3! ", "Winner: Player 4! "]

# Speed of the game
speed = 30

# Check if the game is running
running = True

# Get exit msg from backend(really from server)
exitFromServer = False

# Index of winner
winnerIndex = -1

# Queue to store received data
_pSnakeQueue = Queue()

# Initialize pygame and env
def startGame(food):
    # Anaylse argument
    args = sys.argv
    if len(args) != 5:
        print '[python] Argument is invalid. \n Argument type must be : python snake.py [player count] [self index] [Food_x] [Food_y]'
        sys.exit()
    
    global _pPlayerNum, _pSelfIndex
    _pPlayerNum = int(args[1])
    _pSelfIndex = int(args[2])
    food.setPosition(int(args[3]), int(args[4]))    

    global FIFO_W_PATH, FIFO_R_PATH
    FIFO_W_PATH = '/tmp/snakegame_fifo_w_{0}'.format(_pSelfIndex)
    FIFO_R_PATH = '/tmp/snakegame_fifo_r_{0}'.format(_pSelfIndex)

    # init pygame
    pygame.init()
    pygame.font.init()
    random.seed()
    global screen
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    gameTitle = 'Network Snake Game : Player{0}'.format(_pSelfIndex)
    pygame.display.set_caption(gameTitle)
    global clock
    clock = pygame.time.Clock()

# Create 'cnt' numbers of snake
def createSnakeArray(snakeArr, cnt):
    for x in xrange(0, cnt):
        snakeArr.append(snake(_pInitPosArr[x][0], _pInitPosArr[x][1], _pColorArr[x]))

def text(intext, size, inx, iny, color):
    font = pygame.font.Font(None, size)
    text = font.render((intext), 0, color)
    if inx == -1:
        x = WIDTH / 2
    else:
        x = inx
    if iny == -1:
        y = HEIGHT / 2
    else:
        y = iny
    textpos = text.get_rect(centerx=x, centery=y)
    screen.blit(text, textpos)

# Send data to backend via fifo
def sendToBackend(data):
    try:
        with open(FIFO_W_PATH, 'w') as fifo:
            # print '[python] Send to backend : {0}'.format(data)            
            fifo.write(data)
            fifo.flush()
            fifo.close()
    except Exception as e:
        print '[python] Send to backend failed'

# Get state of snake
def getSnakeState(snake):
    state = ''
    
    if snake.die:
        state = 'die'
    else:
        state = 'live'
        state = state + ':' + str(snake.x) + ':' + str(snake.y) + ':' + str(snake.hdir) + ':' + str(snake.vdir)
        pixelLen = len(snake.pixels)
        state = state + ':' + str(snake.length) + ':' + str(pixelLen)

        for index in xrange(0, pixelLen):
            state = state + ':' + str(snake.pixels[index][0]) + ':' + str(snake.pixels[index][1])
    state = state + '\n'
    return state

# Get state of food
def getFoodState(food):
    state = ''    
    state = str(food.x) + ':' + str(food.y)
    return state

# Move and draw all snakes
def moveAndDrawSnakes(snakeArr):    
    snake_cnt = len(snakeArr)
    for x in xrange(0, snake_cnt):
        snakeArr[x].move(x, snakeArr)
        snakeArr[x].draw()

# Display player string to screen
def dispPlayerString(cnt):
    for x in xrange(0, cnt):
        text(_pDispStrArr[x], 16, (WIDTH * (x + 1)) / 5, 10, _pColorArr[x])

# Display fps counter
def dispFpsCounter():
    text('fps: 60', 16, WIDTH - 60, HEIGHT - 20, (200, 200, 200))

# Check condition if snake hits to food
def checkHitCondition(snake, food):
    global score    
    if food.hitCheck(snake.pixels):        
        score = score + 10
        snake.length = snake.length + 7
        # Send to backend for relocating food
        sendstr = 'FOODHIT\n'
        # print '[python] Hit food. send relocate request'
        sendToBackend(sendstr)

        mystate = 'STATE:{0}:'.format(_pSelfIndex)
        mystate += getSnakeState(_pSnakeArr[_pSelfIndex - 1]) + '\n'
        sendToBackend(mystate)

# Class to represent snake instance
class snake:
    def __init__(self, x, y, color = (0, 255, 0), pixels = None, index = -1):
        self.x = x
        self.y = y
        self.hdir = 0
        self.vdir = -10
        self.length = 7
        if pixels == None:
            self.pixels = [(x, y), (x, y), (x, y), (x, y), (x, y), (x, y), (x, y)]
        else:
            self.pixels = pixels
        if index == None:
            self.index = -1
        else:
            self.index = index
        self.color = color
        self.crash = False        
        self.die = False

    def events(self, key):
        # Controls the left event
        if key == pygame.K_LEFT and self.hdir != 10: # Check to make sure we aren't already going left.
            self.hdir = -10
            self.vdir = 0
        # Controls the right event
        if key == pygame.K_RIGHT and self.hdir != -10: # Check to make sure we aren't already going right.
            self.hdir = 10
            self.vdir = 0
        # Controls the up event
        if key == pygame.K_UP and self.vdir != 10: # Check to make sure we aren't already going up.
            self.hdir = 0
            self.vdir = -10
        # Controls the down event
        if key == pygame.K_DOWN and self.vdir != -10: # Check to make sure we aren't already going down.
            self.hdir = 0
            self.vdir = 10

    def move(self, index, snakeArr):
        if not self.die:
            self.x += self.hdir
            self.y += self.vdir

            if (self.x, self.y) in self.pixels:
                self.crash = True

            # Checks if this hits to other snakes
            snake_cnt = len(snakeArr)
            for x in xrange(0, snake_cnt):
                if x != index and snakeArr[x].die == False and (self.x, self.y) in snakeArr[x].pixels:                    
                    self.crash = True
                    if self.x == snakeArr[x].x and self.y == snakeArr[x].y:
                        snakeArr[x].crash = True
            
            # Wraps the snake
            if self.x < 0:
                self.x = WIDTH - 10
            if self.x >= WIDTH:
                self.x = 0
            if self.y <= 0:
                self.y = HEIGHT - 20
            if self.y >= HEIGHT - 10:
                self.y = 10

            self.pixels.insert(0, (self.x, self.y))

            if len(self.pixels) > self.length:
                del self.pixels[self.length]

    def draw(self):
        if not self.die:
            for x, y in self.pixels:
                pygame.draw.rect(screen, self.color, (x, y + 10, 10, 10), 1)


# Class to represend food instance
class food():
    # Initialize the position for where food is placed.
    def __init__(self):        
        self.x = random.randrange(20, WIDTH, 10)
        self.y = random.randrange(20, HEIGHT, 10)
        
    # Set position of food
    def setPosition(self, xpos, ypos):
        self.x = xpos
        self.y = ypos

    # Check if the snake hit himself.
    def hitCheck(self, snakePixels):
        if snakePixels[0][0] == self.x and snakePixels[0][1] == self.y:
            return True

    # After a snake eats, relocate the food to a random position.
    def relocate(self):
        self.x = random.randrange(20, WIDTH, 10)
        self.y = random.randrange(20, HEIGHT, 10)
    
    # Draw the food onto the screen.
    def draw(self):
        pygame.draw.rect(screen, (255, 0, 0), (self.x, self.y + 10, 10, 10), 0)


# Thread class to observe status from backend via fifo
class StatusThread(threading.Thread):
    def __init__(self):
        threading.Thread.__init__(self)        
    def run(self):
        # Checks for backend status
        
        with open(FIFO_R_PATH, 'r') as fifo:
            readstr = "";
            while running:
                # Non-blocking read.
                ch = fifo.read(1)
                if ch != '\n' and ch != '':
                    readstr += str(ch)
                else:         
                    try:               
                        self.proc(readstr)
                        readstr = ""
                    except Exception as e:
                        print '[python] Process failed in Status Thread. packet {0}'.format(readstr)

    # Process packet function
    # packet structure : CODE:EXT_CODE:DATA
    def proc(self, packet = ''):
        if packet == '' or packet == '\n':
            return
        
        global IsSync, _pFood, _pSnakeQueue, running, exitFromServer, winnerIndex, _pDieOrder

        str_list = packet.split(':')
        if len(str_list) < 1:
            return

        code = str_list[0]
        list_len = len(str_list)

        # Time synchronization request
        if list_len == 1 and code == 'TIME':
                IsSync = True

        # Exit request
        elif code == 'EXIT':
            print '[python] Get exit request'
            exitFromServer = True
            running = False
            # Send exit to backend
            # sendToBackend('RECVEXIT\n')            

        # Food state request
        elif len(str_list) == 3 and code == 'FOOD':
            # print '[python] Read packet : {0}'.format(packet)
            food_x = int(str_list[1])
            food_y = int(str_list[2])
            _pFood.setPosition(food_x, food_y)

        # Winner state request
        elif code == 'WIN':
            # print '[python] WINNER : {0}'.format(packet)
            winnerIndex = int(str_list[1]) - 1
            tempsnake = snake(int(str_list[3]), int(str_list[4]))
            tempsnake.die = False
            tempsnake.hdir = int(str_list[5])
            tempsnake.vdir = int(str_list[6])
            tempsnake.length = int(str_list[7])
            pixelLen = int(str_list[8])
            temp_pixels = []
            for i in xrange(0, pixelLen):
                try:
                    x = int(str_list[9 + i * 2])
                    y = int(str_list[10 + i * 2])
                    temp_pixels.append((x, y))
                except Exception as e:
                    break
            tempsnake.pixels = temp_pixels
            tempsnake.index = winnerIndex
            _pSnakeQueue.put(tempsnake)
        # Snake state request
        elif code == 'STATE':
            # print '[python] Read packet : {0}'.format(packet)
            # Create new snake and save state information in it, then put it in queue
            tempsnake = None
            index = int(str_list[1]) - 1
            if str_list[2] == 'die':
                # the snake has died
                tempsnake = snake(_pInitPosArr[index][0], _pInitPosArr[index][1])
                tempsnake.crash = True
                tempsnake.die = True                
                _pDieOrder.append(index)
            elif str_list[2] == 'live':
                # the snake's state has changed
                tempsnake = snake(int(str_list[3]), int(str_list[4]))
                tempsnake.die = False
                tempsnake.hdir = int(str_list[5])
                tempsnake.vdir = int(str_list[6])
                tempsnake.length = int(str_list[7])
                pixelLen = int(str_list[8])
                temp_pixels = []
                for i in xrange(0, pixelLen):
                    try:
                        x = int(str_list[9 + i * 2])
                        y = int(str_list[10 + i * 2])
                        temp_pixels.append((x, y))
                    except Exception as e:
                        break
                tempsnake.pixels = temp_pixels
            tempsnake.index = index
            _pSnakeQueue.put(tempsnake)

        # Snake has disconnected
        elif code == 'DISC':
            index = int(str_list[1]) - 1
            tempsnake = snake(_pInitPosArr[index][0], _pInitPosArr[index][1])
            tempsnake.crash = True
            tempsnake.die = True
            tempsnake.index = index
            _pDieOrder.append(index)
            _pSnakeQueue.put(tempsnake)

# Create food first for get postion from arguments
_pFood = food()

# Initialize pygame and environment
startGame(_pFood)

# Create snake array
createSnakeArray(_pSnakeArr, _pPlayerNum)
    
# Receive status info periodly from backend
statThread = StatusThread()
statThread.start()

while running:
    # Wait until time synchronize request come in
    while not IsSync:
        if not running:
            break
        clock.tick(100)
    IsSync = False

    screen.fill((0, 0, 0))

    # There is no winner yet
    if winnerIndex == -1:        
        while _pSnakeQueue.empty() == False:
            tempsnake = _pSnakeQueue.get()
            index = tempsnake.index
            if tempsnake.die == True:
                _pSnakeArr[index].die = True
                _pSnakeArr[index].crash = True
            else:
                _pSnakeArr[index].x = tempsnake.x
                _pSnakeArr[index].y = tempsnake.y
                _pSnakeArr[index].hdir = tempsnake.hdir            
                _pSnakeArr[index].vdir = tempsnake.vdir
                _pSnakeArr[index].length = tempsnake.length
                _pSnakeArr[index].pixels = []
                _pSnakeArr[index].pixels = tempsnake.pixels            
            del tempsnake

        # move snake and food
        moveAndDrawSnakes(_pSnakeArr)        
        _pFood.draw()

        # Check if self hits to food
        checkHitCondition(_pSnakeArr[_pSelfIndex - 1], _pFood)

    # There is winner, his index is winnerIndex, draw only him
    else:
        while _pSnakeQueue.empty() == False:
            tempsnake = _pSnakeQueue.get()
            if tempsnake.index == winnerIndex:                        
                _pSnakeArr[winnerIndex].x = tempsnake.x
                _pSnakeArr[winnerIndex].y = tempsnake.y
                _pSnakeArr[winnerIndex].hdir = tempsnake.hdir            
                _pSnakeArr[winnerIndex].vdir = tempsnake.vdir
                _pSnakeArr[winnerIndex].length = tempsnake.length
                _pSnakeArr[winnerIndex].pixels = []
                _pSnakeArr[winnerIndex].pixels = tempsnake.pixels            
            del tempsnake

        _pSnakeArr[winnerIndex].draw()
        _pFood.draw()
    
    # Display player string
    dispPlayerString(_pPlayerNum)

    # Display fps counter
    dispFpsCounter()

    # Checks for user input and perform the relevant actions.
    for event in pygame.event.get():
        if event.type == pygame.QUIT:            
            running = False

        if event.type == pygame.KEYDOWN:
            if event.key in _pKeyArr and _pSnakeArr[_pSelfIndex - 1].die == False:
                _pSnakeArr[_pSelfIndex - 1].events(event.key)
                mystate = 'STATE:{0}:'.format(_pSelfIndex)
                mystate += getSnakeState(_pSnakeArr[_pSelfIndex - 1]) + '\n'
                if running:                    
                    sendToBackend(mystate)

            if event.key == pygame.K_ESCAPE:                
                running = False

            clock.tick(speed)
    
    # Updates the display at the end.
    pygame.display.flip()
    clock.tick(speed)

    # Logic to check who wins in a multiplayer game

    live_cnt = 0    

    # Check if I have crashed. If true, then send my state to backend
    if _pSnakeArr[_pSelfIndex - 1].crash and not _pSnakeArr[_pSelfIndex - 1].die:
        _pSnakeArr[_pSelfIndex - 1].die = True
        _pDieOrder.append(_pSelfIndex - 1)
        mystate = 'STATE:{0}:'.format(_pSelfIndex)
        mystate += getSnakeState(_pSnakeArr[_pSelfIndex - 1]) + '\n'
        if running:
            sendToBackend(mystate)

    # Check when I am alive, if there is another snake alive
    elif not _pSnakeArr[_pSelfIndex - 1].crash:        
        for index in xrange(0, _pPlayerNum):
            if _pSnakeArr[index].die == False:
                live_cnt += 1
        # live_cnt = 1 then I am the only snake alive.
        if live_cnt == 1:
            # Send 'WIN' message to backend
            mystate = 'WIN:{0}:'.format(_pSelfIndex)
            mystate += getSnakeState(_pSnakeArr[_pSelfIndex - 1]) + '\n'
            if running:
                # print '[python] WINNER : {0}'.format(mystate)
                sendToBackend(mystate)
            winnerIndex = _pSelfIndex - 1

    # I have died early, and there is winner
    elif winnerIndex != -1:
        live_cnt = 1

    # If only one player has lived, diplay string to show he is winner
    while (live_cnt == 1 and _pPlayerNum != 1 and running):        
        text(_pWinStrArr[winnerIndex], 40, -1, -1, _pColorArr[winnerIndex])        
        text("Press X to exit", 30, -1, HEIGHT / 2 + 30, (255, 255, 255))
        
        pygame.display.flip()
        clock.tick(50)

        for event in pygame.event.get():
            if event.type == pygame.QUIT:                
                running = False
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_x:                    
                    running = False        

    # If no player has lived, then it is tie state and display string to show it is tie state
    while (live_cnt == 0 and _pPlayerNum != 1 and running):
        _ballow = True
        for i in xrange(0, _pPlayerNum):
            if _pSnakeArr[i].die == False:
                _ballow = False
                break
        
        if not _ballow:
            break

        lastIndex = _pDieOrder[_pPlayerNum - 1]
        finalIndex = _pDieOrder[_pPlayerNum - 2]
        dispstr = 'Player {0} tied with Player {1}'.format(lastIndex + 1, finalIndex + 1)
        text(dispstr, 40, -1, -1, (100, 100, 100))
        text("Press X to exit", 30, -1, HEIGHT / 2 + 30, (255, 255, 255))
        
        pygame.display.flip()
        clock.tick(50)

        for event in pygame.event.get():
            if event.type == pygame.QUIT:                
                running = False
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_x:                    
                    running = False
        
    while (live_cnt == 0 and _pPlayerNum == 1 and running):
        text("Game Over, Score: " + str(score), 40, -1, -1, (255, 255, 255))        
        text("Press X to exit", 30, -1, HEIGHT / 2 + 30, (255, 255, 255))
        
        pygame.display.flip()
        clock.tick(50)

        for event in pygame.event.get():
            if event.type == pygame.QUIT:                
                running = False
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_x:                    
                    running = False

# Wait for status thread exit
statThread.join()

# Send exit to backend
if not exitFromServer:
    sendToBackend('EXIT\n')

sys.exit()
