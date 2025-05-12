// controlLinux.c - Linux Version
// Converted from Windows version for Linux with Mosquitto client

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

// Just use the commands directly from PATH
char mosquittoPath[] = "mosquitto_pub";
char mosquittoSub[] = "mosquitto_sub";

// MQTT Configuration
#define MQTT_HOST "" // Add your MQTT broker address here
#define MQTT_TOPIC "TTT"

// Board state
char board[3][3] = {
    {' ', ' ', ' '},
    {' ', ' ', ' '},
    {' ', ' ', ' '}
};
char currentPlayer = 'X';
char positions[9][4];  // Array to store position strings like "1,2"
int current_index = 0;
int autoplay_enabled = 0;
const int autoplay_delay = 500;

// Color codes for Linux terminal
#define COLOR_RED "\033[0;31m"
#define COLOR_GREEN "\033[0;32m"
#define COLOR_BLUE "\033[0;34m"
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_WHITE "\033[0;37m"
#define COLOR_RESET "\033[0m"

// Handles for the MQTT subscriber process and pipes
pid_t mqtt_sub_pid = -1;
int mqtt_pipe_fd[2] = {-1, -1};
int listener_running = 0;

// Function prototypes
void displayBoard();
void setConsoleColor(const char *color);
void resetConsoleColor();
void publishMessage(const char *message);
void startBoardListener();
void stopBoardListener();
void *mqttListenerThread(void *arg);
void updateBoard(const char *topic, const char *message);
void makeMove(int row, int col);
void resetGame();
void generateBoardPositions();
void randomMove();
void toggleAutoplay();
void cleanup();

// Set console text color
void setConsoleColor(const char *color) {
    printf("%s", color);
}

// Reset console text color to default
void resetConsoleColor() {
    printf("%s", COLOR_RESET);
}

// Clear the console screen
void clearScreen() {
    printf("\033[H\033[J");  // ANSI escape code to clear screen
}

// Display the current state of the board
void displayBoard() {
    clearScreen();

    setConsoleColor(COLOR_YELLOW);
    printf("===========================\n");
    printf("Tic-Tac-Toe Game Board\n");
    printf("===========================\n\n");
    resetConsoleColor();

    printf("Current Player: ");
    setConsoleColor(COLOR_GREEN);
    printf("%c\n\n", currentPlayer);
    resetConsoleColor();

    printf("    1   2   3\n");
    printf("  +-----------+\n");

    for (int i = 0; i < 3; i++) {
        printf("%d | ", i + 1);

        for (int j = 0; j < 3; j++) {
            setConsoleColor(COLOR_RED);
            printf("%c", board[i][j]);
            resetConsoleColor();

            if (j < 2) {
                printf(" | ");
            }
        }

        printf(" |\n");

        if (i < 2) {
            printf("  |-----------|\n");
        }
    }

    printf("  +-----------+\n\n");
    printf("Enter move as 'row,col' (e.g. '1,3')\n");
    printf("Or 'r' to reset, 'q' to quit, 'a' to automate\n\n");
}

// Publish a message to the MQTT broker
void publishMessage(const char *message) {
    char command[512];
    printf("Sending: %s\n", message);

    // Create the command string
    snprintf(command, sizeof(command),
             "%s -h %s -t %s -m \"%s\" > /dev/null 2>&1",
             mosquittoPath, MQTT_HOST, MQTT_TOPIC, message);

    // Execute the command using system
    system(command);

    // Sleep briefly to allow time for the message to be processed
    usleep(100000);  // 100ms in microseconds
}

// Thread function to read from the pipe and process MQTT messages
void *mqttListenerThread(void *arg) {
    char buffer[1024];
    ssize_t bytesRead;
    static char leftover[1024] = "";
    FILE *fp = fdopen(mqtt_pipe_fd[0], "r");

    if (fp == NULL) {
        perror("fdopen failed");
        return NULL;
    }

    while (listener_running) {
        // Read a line from the pipe
        if (fgets(buffer, sizeof(buffer), fp) == NULL) {
            if (feof(fp)) {
                break; // End of file
            }
            usleep(100000);  // 100ms
            continue;
        }

        // Remove trailing newline
        buffer[strcspn(buffer, "\n")] = '\0';

        // Skip empty lines
        if (strlen(buffer) == 0) {
            continue;
        }

        // Parse the line: format is "topic message"
        char *space = strchr(buffer, ' ');
        if (space) {
            *space = '\0';  // Split topic and message
            char *topic = buffer;
            char *message = space + 1;

            // Process the message
            updateBoard(topic, message);
        }

        usleep(100000);  // 100ms, don't monopolize CPU
    }

    fclose(fp);
    return NULL;
}

// Start the MQTT subscriber process
void startBoardListener() {
    if (listener_running) {
        return;
    }

    // Create pipe for reading subscriber output
    if (pipe(mqtt_pipe_fd) == -1) {
        perror("pipe failed");
        return;
    }

    // Fork to create child process
    mqtt_sub_pid = fork();

    if (mqtt_sub_pid < 0) {
        perror("fork failed");
        close(mqtt_pipe_fd[0]);
        close(mqtt_pipe_fd[1]);
        return;
    }

    if (mqtt_sub_pid == 0) {
        // Child process - will run mosquitto_sub

        // Close read end of pipe
        close(mqtt_pipe_fd[0]);

        // Redirect stdout to pipe
        dup2(mqtt_pipe_fd[1], STDOUT_FILENO);
        close(mqtt_pipe_fd[1]);

        // Execute mosquitto_sub
        char topic_arg[100];
        snprintf(topic_arg, sizeof(topic_arg), "%s/#", MQTT_TOPIC);

        // Since we're using commands from PATH, we need to use the first argument as the program name
        execlp("mosquitto_sub", "mosquitto_sub", "-h", MQTT_HOST, "-t", topic_arg, "-v", NULL);

        // If execl returns, there was an error
        perror("execl failed");
        exit(EXIT_FAILURE);
    }

    // Parent process continues here

    // Close write end of pipe in parent
    close(mqtt_pipe_fd[1]);
    mqtt_pipe_fd[1] = -1;

    // Start a thread that reads from the pipe
    listener_running = 1;

    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, mqttListenerThread, NULL) != 0) {
        perror("pthread_create failed");
        stopBoardListener();
        return;
    }

    // Detach the thread so its resources are freed automatically
    pthread_detach(thread_id);

    printf("MQTT subscriber started\n");
    displayBoard();
}

// Stop the MQTT subscriber process
void stopBoardListener() {
    if (!listener_running) {
        return;
    }

    listener_running = 0;

    // Terminate the mosquitto_sub process
    if (mqtt_sub_pid > 0) {
        kill(mqtt_sub_pid, SIGTERM);
        waitpid(mqtt_sub_pid, NULL, 0);
        mqtt_sub_pid = -1;
    }

    // Close the pipe
    if (mqtt_pipe_fd[0] >= 0) {
        close(mqtt_pipe_fd[0]);
        mqtt_pipe_fd[0] = -1;
    }

    if (mqtt_pipe_fd[1] >= 0) {
        close(mqtt_pipe_fd[1]);
        mqtt_pipe_fd[1] = -1;
    }

    printf("MQTT listener stopped\n");
}

// Update the board state based on MQTT messages
void updateBoard(const char *topic, const char *message) {
    char subTopic[256];

    // Check for board state updates
    snprintf(subTopic, sizeof(subTopic), "%s/board", MQTT_TOPIC);
    if (strcmp(topic, subTopic) == 0) {
        // Update board state (flat string to 2D array)
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                board[i][j] = message[i * 3 + j];
            }
        }
        displayBoard();
        return;
    }

    // Check for current player updates
    snprintf(subTopic, sizeof(subTopic), "%s/player", MQTT_TOPIC);
    if (strcmp(topic, subTopic) == 0) {
        currentPlayer = message[0];
        return;
    }

    // Check for game status updates
    snprintf(subTopic, sizeof(subTopic), "%s/status", MQTT_TOPIC);
    if (strcmp(topic, subTopic) == 0) {
        if (strstr(message, "wins") != NULL) {
            setConsoleColor(COLOR_GREEN);
            printf("Player %s!\n", message);
            resetConsoleColor();
        }
        else if (strcmp(message, "draw") == 0) {
            setConsoleColor(COLOR_BLUE);
            printf("Game ended in a draw!\n");
            resetConsoleColor();
        }
        else if (strcmp(message, "reset") == 0) {
            setConsoleColor(COLOR_YELLOW);
            printf("Game has been reset.\n");
            resetConsoleColor();
        }
        return;  // Let the board update handle the display
    }

    // Check for move updates
    if (strstr(topic, "/moves") != NULL) {
        setConsoleColor(COLOR_BLUE);
        printf("Move made: %s\n", message);
        resetConsoleColor();
    }
}

// Make a move on the board
void makeMove(int row, int col) {
    char move[10];
    sprintf(move, "%d,%d", row, col);
    publishMessage(move);
}

// Reset the game
void resetGame() {
    publishMessage("r");
    printf("Game reset command sent\n");
    usleep(500000);  // 500ms in microseconds
}

// Generate all possible board positions in random order
void generateBoardPositions() {
    int index = 0;
    // Create array of all positions
    for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 3; j++) {
            sprintf(positions[index], "%d,%d", i, j);
            index++;
        }
    }

    // Fisher-Yates shuffle
    for (int i = 8; i > 0; i--) {
        int j = rand() % (i + 1);
        char temp[4];
        strcpy(temp, positions[i]);
        strcpy(positions[i], positions[j]);
        strcpy(positions[j], temp);
    }
    current_index = 0;
}

// Make a random move
void randomMove() {
    // If all moves used, reset for next round
    if (current_index >= 9) {
        printf("All positions played. Restarting board...\n");
        generateBoardPositions();
    }
    publishMessage(positions[current_index]);
    printf("Random move sent: %s\n", positions[current_index]);
    current_index++;
    sleep(1);  // Wait a second between moves
}

// Toggle autoplay mode
void toggleAutoplay() {
    autoplay_enabled = !autoplay_enabled;
    if (autoplay_enabled) {
        srand(time(NULL));  // Initialize random seed
        printf("Autoplay enabled\n");
        generateBoardPositions();
    } else {
        printf("Autoplay disabled\n");
    }
}

// Cleanup function to be called on exit
void cleanup() {
    stopBoardListener();
    printf("Thanks for playing!\n");
}

// Signal handler for graceful termination
void signalHandler(int sig) {
    printf("\nReceived signal %d. Exiting...\n", sig);
    cleanup();
    exit(0);
}

// Main function
int main(int argc, char *argv[]) {
    char input[20];
    int row, col;

    // Set up signal handlers for graceful termination
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Register cleanup function to be called on normal exit
    atexit(cleanup);

    // Start the MQTT listener
    startBoardListener();

    // Main game loop
    while (1) {
        displayBoard();

        // Autoplay mode - make moves automatically
        if (autoplay_enabled) {
            randomMove();
            usleep(autoplay_delay * 1000);  // Convert to microseconds
            continue;  // Skip manual input when in autoplay mode
        }

        // Manual input mode
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;  // Handle EOF (Ctrl+D)
        }

        input[strcspn(input, "\n")] = 0;

        if (input[0] == 'q' || input[0] == 'Q') {
            break;
        }
        else if (input[0] == 'r' || input[0] == 'R') {
            resetGame();
        }
        else if (input[0] == 'a' || input[0] == 'A') {
            toggleAutoplay();
        }
        else if (sscanf(input, "%d,%d", &row, &col) == 2) {
            if (row >= 1 && row <= 3 && col >= 1 && col <= 3) {
                makeMove(row, col);
            }
            else {
                printf("Invalid move! Row and column must be between 1 and 3.\n");
                sleep(1);
            }
        }
        else {
            printf("Invalid input! Enter 'row,col', 'r' to reset, 'a' to toggle autoplay, or 'q' to quit.\n");
            sleep(1);
        }
    }

    return 0;
}