/**********************************************************************
  Filename    : Tic Tac Toe Game with ESP32 and MQTT
  Description : Play Tic Tac Toe using serial monitor or MQTT, display scores on LCD1602
**********************************************************************/
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define SDA 14                    // Define SDA pins
#define SCL 13                    // Define SCL pins

// WiFi credentials
const char* ssid = "";       // Replace with your WiFi name
const char* password = "";       // Replace with your WiFi password

// MQTT Broker settings
const char* mqtt_server = "";      // Replace with your broker IP
const int mqtt_port = 1883;                    // Default MQTT port
const char* mqtt_username = "";                // Optional: MQTT username if required
const char* mqtt_password = "";                // Optional: MQTT password if required
const char* clientID = "ESP32_TTT";            // Client ID for MQTT connection
const char* topic_sub = "TTT";                 // Topic to subscribe to for game control

// Initialize WiFi and MQTT client - GLOBAL DECLARATIONS
WiFiClient espClient;
PubSubClient client(espClient);

// LCD initialization
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Function declaration to prevent errors
String getBoardStateString();

// Game variables
char board[3][3] = {
  {' ', ' ', ' '},
  {' ', ' ', ' '},
  {' ', ' ', ' '}
};
char currentPlayer = 'X';
unsigned short int xWins = 0;
unsigned short int oWins = 0;
unsigned short int winCount = 0;
boolean gameOver = false;

// Callback function for MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  // Convert payload to string
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);
  
  // Process the message if game is not over
  if (!gameOver) {
    // Check if the message is in the format "row,col"
    int commaIndex = message.indexOf(',');
    if (commaIndex > 0) {
      int row = message.substring(0, commaIndex).toInt();
      int col = message.substring(commaIndex + 1).toInt();
      
      // Make the move (already converts to 0-indexed)
      makeMove(row - 1, col - 1);
    }
  }
  // If message is 'r' or 'R', reset the game
  else if (message.equals("r") || message.equals("R")) {
    resetGame();
  }
}

// Reconnect to MQTT broker when connection is lost
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    // Attempt to connect
    if (mqtt_username[0] == '\0') {
      // Connect without credentials
      if (client.connect(clientID)) {
        Serial.println("connected");

        // Subscribe to light control topic
        client.subscribe(topic_sub);
        Serial.println("Subscribed to: " + String(topic_sub));
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        delay(5000);
      }
    } else {
      // Connect with credentials
      if (client.connect(clientID, mqtt_username, mqtt_password)) {
        Serial.println("connected");

        // Subscribe to light control topic
        client.subscribe(topic_sub);
        Serial.println("Subscribed to: " + String(topic_sub));
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        delay(5000);
      }
    }
  }
}

void setup() {
  Wire.begin(SDA, SCL);           // attach the IIC pin
  if (!i2CAddrTest(0x27)) {
    lcd = LiquidCrystal_I2C(0x3F, 16, 2);
  }
  lcd.init();                     // LCD driver initialization
  lcd.backlight();                // Open the backlight
  
  // Initialize the display
  updateScores();
  
  Serial.begin(115200);
  Serial.println("Tic Tac Toe Game");
  Serial.println("Enter move as: row col (e.g., 1 2)");
  printBoard();

  // Connect to WiFi
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // Set MQTT server and callback function
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  // Initial connection to MQTT
  if (!client.connected()) {
    reconnect();
  }
  
  // Send game state on startup
  String boardState = getBoardStateString();
  client.publish("TTT/state", boardState.c_str());
}

void loop() {
  // If client is not connected, reconnect
  if (!client.connected()) {
    reconnect();
  }
  
  // Process MQTT messages
  client.loop();

  if (gameOver) {
    resetGame();
    return;
  }

  if (Serial.available() > 0) {
    int row = Serial.parseInt();
    int col = Serial.parseInt();
    
    
    // Clear any remaining characters in the buffer
    while (Serial.available() > 0) {
      Serial.read();
    }
    
    // Process the move
    makeMove(row - 1, col - 1); // Convert to 0-indexed
  }
}

void makeMove(int row, int col) {
    lcd.setCursor(10, 0);
    lcd.print("TURN:");
    lcd.print(currentPlayer);

    lcd.setCursor(8, 1);
    lcd.print("COORD:");
    lcd.print(col);
    lcd.print(row);
    

  // Check if move is valid
  if (row < 0 || row > 2 || col < 0 || col > 2) {
    Serial.println("Invalid move! Row and column must be 1, 2, or 3.");
    return;
  }
  
  if (board[row][col] != ' ') {
    Serial.println("That position is already taken!");
    return;
  }
  
  // Make the move
  board[row][col] = currentPlayer;
  
  // Publish the move to MQTT
  String moveMessage = String(row + 1) + "," + String(col + 1) + "," + String(currentPlayer);
  client.publish("TTT/moves", moveMessage.c_str());
  
  // Print the updated board
  printBoard();
  
  // Check if there's a winner
  if (checkWin()) {
    gameOver = true;
    if (currentPlayer == 'X') {
      xWins++;
    } else {
      oWins++;
    }
    winCount++; // update the total count

    if (winCount > 99){ // Reset the points once total wins is 100
      xWins = oWins = winCount = 0;
    }
    
    updateScores();
    
    Serial.print("Player ");
    Serial.print(currentPlayer);
    Serial.println(" wins!");
    Serial.println("Press 'r' to reset the game.");
    
    // Publish win notification
    String winMessage = String(currentPlayer) + " wins";
    client.publish("TTT/status", winMessage.c_str());
    return;
  }
  
  // Check for a draw
  if (checkDraw()) {
    gameOver = true;
    Serial.println("Game is a draw!");
    Serial.println("Press 'r' to reset the game.");
    
    // Publish draw notification
    client.publish("TTT/status", "draw");
    return;
  }
  
  // Switch players
  currentPlayer = (currentPlayer == 'X') ? 'O' : 'X';
  Serial.print("Player ");
  Serial.print(currentPlayer);
  Serial.println("'s turn.");
}

boolean checkWin() {
  // Check rows
  for (int i = 0; i < 3; i++) {
    if (board[i][0] != ' ' && board[i][0] == board[i][1] && board[i][1] == board[i][2]) {
      return true;
    }
  }
  
  // Check columns
  for (int i = 0; i < 3; i++) {
    if (board[0][i] != ' ' && board[0][i] == board[1][i] && board[1][i] == board[2][i]) {
      return true;
    }
  }
  
  // Check diagonals
  if (board[0][0] != ' ' && board[0][0] == board[1][1] && board[1][1] == board[2][2]) {
    return true;
  }
  
  if (board[0][2] != ' ' && board[0][2] == board[1][1] && board[1][1] == board[2][0]) {
    return true;
  }
  
  return false;
}

boolean checkDraw() {
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      if (board[i][j] == ' ') {
        return false;
      }
    }
  }
  return true;
}

void resetGame() {
  // Reset the board
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      board[i][j] = ' ';
    }
  }
  
  currentPlayer = 'X';
  gameOver = false;
  
  Serial.println("New game started!");
  Serial.println("Enter move as: row col (e.g., 1 2)");
  printBoard();
  
  // Publish reset notification
  client.publish("TTT/state", getBoardStateString().c_str());
  client.publish("TTT/status", "reset");
}

void updateScores() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("X: ");
  lcd.print(xWins);
  
  lcd.setCursor(0, 1);
  lcd.print("O: ");
  lcd.print(oWins);
  
  // Also publish scores to MQTT
  String scoreMessage = "X:" + String(xWins) + ",O:" + String(oWins);
  client.publish("TTT/score", scoreMessage.c_str());
}

String getBoardStateString() {
  String state = "";
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      state += board[i][j];
    }
  }
  return state;
}

void printBoard() {
  Serial.println("\nCurrent board:");
  Serial.println("  1 2 3");
  Serial.println(" -------");
  
  for (int i = 0; i < 3; i++) {
    Serial.print(i + 1);
    Serial.print("|");
    
    for (int j = 0; j < 3; j++) {
      Serial.print(board[i][j]);
      Serial.print("|");
    }
    
    Serial.println();
    Serial.println(" -------");
  }
}

bool i2CAddrTest(uint8_t addr) {
  Wire.beginTransmission(addr);
  if (Wire.endTransmission() == 0) {
    return true;
  }
  return false;
}
