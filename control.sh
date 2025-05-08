#!/bin/bash
# Tic-Tac-Toe Game Controller via MQTT
# For Windows with Mosquitto client

# Configuration
MQTT_HOST=""
MQTT_TOPIC="TTT"
MOSQUITTO_PUB="C:\\Program Files (x86)\\mosquitto\\mosquitto_pub.exe"
MOSQUITTO_SUB="C:\\Program Files (x86)\\mosquitto\\mosquitto_sub.exe"

# Board state variables
BOARD_STATE="         "  # 9 spaces representing empty board
CURRENT_PLAYER="X"      # Start with X

# ANSI color codes for prettier output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Function to publish a message
publish_message() {
    message="$1"
    echo "Sending: $message"
    powershell.exe -Command "& '$MOSQUITTO_PUB' -h $MQTT_HOST -t '$MQTT_TOPIC' -m '$message'"
}

# Function to display the board
display_board() {
    clear
    echo -e "${YELLOW}==========================="
    echo -e "Tic-Tac-Toe Game Board"
    echo -e "===========================${NC}"
    echo
    echo -e "Current Player: ${GREEN}$CURRENT_PLAYER${NC}"
    echo
    echo "    1   2   3"
    echo "  +-----------+"
    echo -e "1 | ${RED}${BOARD_STATE:0:1}${NC} | ${RED}${BOARD_STATE:1:1}${NC} | ${RED}${BOARD_STATE:2:1}${NC} |"
    echo "  |-----------|"
    echo -e "2 | ${RED}${BOARD_STATE:3:1}${NC} | ${RED}${BOARD_STATE:4:1}${NC} | ${RED}${BOARD_STATE:5:1}${NC} |"
    echo "  |-----------|"
    echo -e "3 | ${RED}${BOARD_STATE:6:1}${NC} | ${RED}${BOARD_STATE:7:1}${NC} | ${RED}${BOARD_STATE:8:1}${NC} |"
    echo "  +-----------+"
    echo
}

# Function to update board state
update_board() {
    local topic="$1"
    local message="$2"

    case "$topic" in
        "$MQTT_TOPIC/board")
            # Update the board state
            BOARD_STATE="$message"
            display_board
            ;;
        "$MQTT_TOPIC/player")
            # Update current player
            CURRENT_PLAYER="$message"
            display_board
            ;;
        "$MQTT_TOPIC/status")
            # Game status updates
            if [[ "$message" == *"wins"* ]]; then
                echo -e "${GREEN}Player $message!${NC}"
                echo "Press Enter to continue..."
                read
            elif [[ "$message" == "draw" ]]; then
                echo -e "${BLUE}Game ended in a draw!${NC}"
                echo "Press Enter to continue..."
                read
            elif [[ "$message" == "reset" ]]; then
                echo -e "${YELLOW}Game has been reset.${NC}"
            fi
            ;;
        "$MQTT_TOPIC/score")
            # Score updates
            echo -e "${YELLOW}Score updated: $message${NC}"
            ;;
        "$MQTT_TOPIC/board_formatted")
            # Just for debugging - we're using our own display function
            # echo -e "Formatted board:\n$message"
            ;;
        *)
            # Other messages - just for monitoring
            if [[ "$topic" == "$MQTT_TOPIC/moves" ]]; then
                echo -e "${BLUE}Move made: $message${NC}"
            fi
            ;;
    esac
}

# Function to listen for board updates in background
start_board_listener() {
    # Kill any existing listeners
    if [ -f .mqtt_listener_pid ]; then
        kill $(cat .mqtt_listener_pid) 2>/dev/null || true
        rm .mqtt_listener_pid
    fi

    # Start a background listener that updates our variables
    {
        powershell.exe -Command "& '$MOSQUITTO_SUB' -h $MQTT_HOST -t '$MQTT_TOPIC/#' -v" | while read -r line; do
            # Parse topic and message from the line
            topic=$(echo "$line" | cut -d' ' -f1)
            message=$(echo "$line" | cut -d' ' -f2-)

            # Update the game state
            update_board "$topic" "$message"
        done
    } &

    # Save the PID for later cleanup
    echo $! > .mqtt_listener_pid

    # Initial board display
    update_board "$topic" "$message"
    display_board
}

# Function to stop the background listener
stop_board_listener() {
    if [ -f .mqtt_listener_pid ]; then
        kill $(cat .mqtt_listener_pid) 2>/dev/null || true
        rm .mqtt_listener_pid
    fi
}

# Function to monitor game state (full output mode)
monitor_game() {
    echo "Monitoring game state... (Press Enter to stop)"
    powershell.exe -Command "& '$MOSQUITTO_SUB' -h $MQTT_HOST -t '$MQTT_TOPIC/#' -v"
    echo "Press Enter to continue..."
    read
}

# Global position list and index
declare -a positions
current_index=0

# Function to generate and shuffle board positions
generate_board_positions() {
    positions=()
    for row in {1..3}; do
        for col in {1..3}; do
            positions+=("$row,$col")
        done
    done
    # Shuffle the positions
    positions=($(printf "%s\n" "${positions[@]}" | shuf))
    current_index=0
}

# Function to make a random (non-repeating) move
random_move() {
    # If all moves used, reset for next round
    if [ $current_index -ge 9 ]; then
        echo "All positions played. Restarting board..."
        generate_board_positions
    fi
    coords="${positions[$current_index]}"
    publish_message "$coords"
    echo "Random move sent: $coords"
    current_index=$((current_index + 1))
    sleep 1
}

# Function to play random game
play_random_game() {
    echo "Starting random game. Press Ctrl+C to stop."
    generate_board_positions
    publish_message "r"
    echo "Game reset."
    sleep 1

    # Start the board listener
    start_board_listener

    while true; do
        random_move
        sleep 1  # Give time to see the board after each move
    done
}

# Main menu
show_menu() {
    echo -e "${YELLOW}==========================="
    echo "Tic-Tac-Toe MQTT Controller"
    echo -e "===========================${NC}"
    echo "1. Play with board display"
    echo "2. Reset game"
    echo "3. Randomize placement"
    echo "4. Exit"
    echo -e "${YELLOW}===========================${NC}"
    echo -n "Enter your choice: "
}

# Make a move
make_move() {
    echo "Enter row,column coordinates (e.g. 2,3): "
    read coords
    publish_message "$coords"
    echo "Move sent: $coords"
    sleep 1  # Give time for the board to update
}

# Interactive play with board display
interactive_play() {
    # Start the board listener
    start_board_listener

    echo "Interactive play mode. Press Ctrl+C to stop."
    echo "Enter moves as row,column (e.g. 2,3) or 'r' to reset:"

    while true; do
        read -p "> " input

        if [[ "$input" == "q" ]]; then
            break
        elif [[ "$input" == "r" ]]; then
            publish_message "r"
            echo "Game reset."
        else
            publish_message "$input"
        fi

        # Sleep briefly to allow board update
        sleep 0.5
    done

    # Stop the board listener when done
    stop_board_listener
}

# Cleanup on exit
cleanup() {
    stop_board_listener
    echo -e "\n${YELLOW}Exiting...${NC}"
    exit 0
}

# Register cleanup handler
trap cleanup EXIT INT TERM

# Initial request for board state
echo "Initializing board state..."
start_board_listener

# Main loop
while true; do
    stop_board_listener  # Stop any existing listeners
    display_board        # Show the current board
    show_menu
    read choice
    case $choice in
        1)
            interactive_play
            ;;
        2)
            publish_message "r"
            echo "Game reset command sent"
            sleep 1  # Give time for the board to update
            ;;
        3)
            play_random_game
            ;;
        4)
            echo "Exiting..."
            exit 0
            ;;
        *)
            echo "Invalid option. Press Enter to continue..."
            read
            ;;
    esac
done