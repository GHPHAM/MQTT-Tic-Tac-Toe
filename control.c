#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>

// Path to Mosquitto executables
char mosquittoPath[] = "C:\\Progra~2\\mosquitto\\mosquitto_pub.exe";
char mosquittoSub[] = "C:\\Progra~2\\mosquitto\\mosquitto_sub.exe";

// MQTT Configuration
#define MQTT_HOST ""
#define MQTT_TOPIC "TTT"

// Board state
char board[3][3] = {
    {' ', ' ', ' '},
    {' ', ' ', ' '},
    {' ', ' ', ' '}
};
char currentPlayer = 'X';

// Color codes for Windows console
#define COLOR_RED 12
#define COLOR_GREEN 10
#define COLOR_BLUE 9
#define COLOR_YELLOW 14
#define COLOR_WHITE 15

// Handles for the MQTT subscriber process and pipes
HANDLE mqtt_sub_process = NULL;
HANDLE mqtt_pipe_read = NULL;
BOOL listener_running = FALSE;

// Function prototypes
void displayBoard();
void setConsoleColor(int color);
void resetConsoleColor();
void publishMessage(const char *message);
void startBoardListener();
void stopBoardListener();
DWORD WINAPI mqttListenerThread(LPVOID arg);
void updateBoard(const char *topic, const char *message);
void makeMove(int row, int col);
void resetGame();

// Set console text color
void setConsoleColor(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

// Reset console text color to default
void resetConsoleColor() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, COLOR_WHITE);
}

// Clear the console screen
void clearScreen() {
    system("cls");
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
    printf("Or 'r' to reset, 'q' to quit\n\n");
}

// Publish a message to the MQTT broker
void publishMessage(const char *message) {
    char command[512];
    printf("Sending: %s\n", message);

    // Create the command string
    snprintf(command, sizeof(command),
             "\"%s\" -h %s -t %s -m \"%s\"",
             mosquittoPath, MQTT_HOST, MQTT_TOPIC, message);

    // Execute the command using Windows API
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Start the child process
    if (!CreateProcess(NULL,   // No module name (use command line)
                      command,  // Command line
                      NULL,     // Process handle not inheritable
                      NULL,     // Thread handle not inheritable
                      FALSE,    // Set handle inheritance to FALSE
                      CREATE_NO_WINDOW, // No console window
                      NULL,     // Use parent's environment block
                      NULL,     // Use parent's starting directory
                      &si,      // Pointer to STARTUPINFO structure
                      &pi))     // Pointer to PROCESS_INFORMATION structure
    {
        printf("CreateProcess failed (%d).\n", GetLastError());
        return;
    }

    // Wait for the process to complete
    WaitForSingleObject(pi.hProcess, 1000);

    // Close process and thread handles
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Sleep briefly to allow time for the message to be processed
    Sleep(100);
}

// Thread function to read from the pipe and process MQTT messages
DWORD WINAPI mqttListenerThread(LPVOID arg) {
    char buffer[1024];
    DWORD bytesRead;
    static char leftover[1024] = "";

    while (listener_running) {
        // Read data from the pipe
        if (!ReadFile(mqtt_pipe_read, buffer, sizeof(buffer)-1, &bytesRead, NULL) || bytesRead == 0) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                break; // Pipe has been closed
            }
            Sleep(100);
            continue;
        }

        buffer[bytesRead] = '\0';

        // Combine with any leftover data from previous reads
        char fullBuffer[2048];
        sprintf(fullBuffer, "%s%s", leftover, buffer);

        // Process complete lines
        char *lineStart = fullBuffer;
        char *lineEnd;
        leftover[0] = '\0';

        while ((lineEnd = strchr(lineStart, '\n')) != NULL) {
            *lineEnd = '\0';  // Terminate this line

            // Skip empty lines
            if (strlen(lineStart) > 0) {
                // Parse the line: format is "topic message"
                char *space = strchr(lineStart, ' ');
                if (space) {
                    *space = '\0';  // Split topic and message
                    char *topic = lineStart;
                    char *message = space + 1;

                    // Process the message
                    updateBoard(topic, message);
                }
            }

            lineStart = lineEnd + 1;  // Move to start of next line
        }

        // Save any incomplete line for next time
        if (*lineStart) {
            strcpy(leftover, lineStart);
        }

        Sleep(100);  // Don't monopolize CPU
    }

    return 0;
}

// Start the MQTT subscriber process
void startBoardListener() {
    if (listener_running) {
        return;
    }

    // Create pipe for reading subscriber output
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE childStdoutRead = NULL;
    HANDLE childStdoutWrite = NULL;

    if (!CreatePipe(&childStdoutRead, &childStdoutWrite, &sa, 0)) {
        printf("CreatePipe failed (%d)\n", GetLastError());
        return;
    }

    // Ensure the read handle to the pipe isn't inherited
    if (!SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        printf("SetHandleInformation failed (%d)\n", GetLastError());
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        return;
    }

    // Create the child process - mosquitto_sub
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = childStdoutWrite;
    si.hStdOutput = childStdoutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    ZeroMemory(&pi, sizeof(pi));

    char command[512];
    snprintf(command, sizeof(command),
             "\"%s\" -h %s -t \"%s/#\" -v",
             mosquittoSub, MQTT_HOST, MQTT_TOPIC);

    // Start the child process
    if (!CreateProcess(NULL,   // No module name (use command line)
                      command,  // Command line
                      NULL,     // Process handle not inheritable
                      NULL,     // Thread handle not inheritable
                      TRUE,     // Set handle inheritance to TRUE
                      CREATE_NO_WINDOW, // No console window
                      NULL,     // Use parent's environment block
                      NULL,     // Use parent's starting directory
                      &si,      // Pointer to STARTUPINFO structure
                      &pi))     // Pointer to PROCESS_INFORMATION structure
    {
        printf("CreateProcess failed (%d).\n", GetLastError());
        CloseHandle(childStdoutRead);
        CloseHandle(childStdoutWrite);
        return;
    }

    // Close unnecessary handles
    CloseHandle(childStdoutWrite);

    // Store global handles for later cleanup
    mqtt_sub_process = pi.hProcess;
    mqtt_pipe_read = childStdoutRead;

    // Start a thread that reads from the pipe
    listener_running = TRUE;

    // Create a thread to read the pipe
    HANDLE thread = CreateThread(
        NULL,                   // default security attributes
        0,                      // default stack size
        mqttListenerThread,     // thread function
        NULL,                   // no thread parameter
        0,                      // default creation flags
        NULL                    // receive thread identifier
    );

    if (thread == NULL) {
        printf("CreateThread failed (%d).\n", GetLastError());
        stopBoardListener();
        return;
    }

    // We don't need the thread handle
    CloseHandle(thread);

    printf("MQTT subscriber started\n");
    displayBoard();
}

// Stop the MQTT subscriber process
void stopBoardListener() {
    if (!listener_running) {
        return;
    }

    listener_running = FALSE;

    // Terminate the mosquitto_sub process
    if (mqtt_sub_process != NULL) {
        TerminateProcess(mqtt_sub_process, 0);
        CloseHandle(mqtt_sub_process);
        mqtt_sub_process = NULL;
    }

    // Close the pipe
    if (mqtt_pipe_read != NULL) {
        CloseHandle(mqtt_pipe_read);
        mqtt_pipe_read = NULL;
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
    }
    // Check for current player updates
    else {
        snprintf(subTopic, sizeof(subTopic), "%s/player", MQTT_TOPIC);
        if (strcmp(topic, subTopic) == 0) {
            // Update current player
            currentPlayer = message[0];
            displayBoard();
        }
        // Check for game status updates
        else {
            snprintf(subTopic, sizeof(subTopic), "%s/status", MQTT_TOPIC);
            if (strcmp(topic, subTopic) == 0) {
                // Game status updates
                if (strstr(message, "wins") != NULL) {
                    setConsoleColor(COLOR_GREEN);
                    printf("Player %s!\n", message);
                    resetConsoleColor();
                    printf("Press Enter to continue...");
                    getchar();
                    displayBoard();
                }
                else if (strcmp(message, "draw") == 0) {
                    setConsoleColor(COLOR_BLUE);
                    printf("Game ended in a draw!\n");
                    resetConsoleColor();
                    printf("Press Enter to continue...");
                    getchar();
                    displayBoard();
                }
                else if (strcmp(message, "reset") == 0) {
                    setConsoleColor(COLOR_YELLOW);
                    printf("Game has been reset.\n");
                    resetConsoleColor();
                    displayBoard();
                }
            }
            // Check for move updates
            else if (strstr(topic, "/moves") != NULL) {
                setConsoleColor(COLOR_BLUE);
                printf("Move made: %s\n", message);
                resetConsoleColor();
            }
        }
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
    Sleep(500);  // Give time for the board to update
}

// Main function
int main(int argc, char *argv[]) {
    char input[20];
    int row, col;

    // Set console title
    SetConsoleTitle("Tic-Tac-Toe MQTT Client");

    // Start the MQTT listener
    startBoardListener();

    // Main game loop
    while (1) {
        displayBoard();
        printf("> ");
        fgets(input, sizeof(input), stdin);

        // Remove newline character
        input[strcspn(input, "\n")] = 0;

        // Check for quit command
        if (input[0] == 'q' || input[0] == 'Q') {
            break;
        }
        // Check for reset command
        else if (input[0] == 'r' || input[0] == 'R') {
            resetGame();
        }
        // Process move command
        else if (sscanf(input, "%d,%d", &row, &col) == 2) {
            // Validate input
            if (row >= 1 && row <= 3 && col >= 1 && col <= 3) {
                makeMove(row, col);
            }
            else {
                printf("Invalid move! Row and column must be between 1 and 3.\n");
                Sleep(1000);
            }
        }
        else {
            printf("Invalid input! Enter 'row,col', 'r' to reset, or 'q' to quit.\n");
            Sleep(1000);
        }
    }

    // Clean up before exiting
    stopBoardListener();
    printf("Thanks for playing!\n");

    return 0;
}