#!/bin/bash
# Tic-Tac-Toe Game Controller via MQTT
# For Windows with Mosquitto client

# Configuration
MQTT_HOST=""
MQTT_TOPIC="TTT"
MOSQUITTO_PUB="C:\\Program Files (x86)\\mosquitto\\mosquitto_pub.exe"
MOSQUITTO_SUB="C:\\Program Files (x86)\\mosquitto\\mosquitto_sub.exe"

# Function to publish a message
publish_message() {
    message="$1"
    echo "Sending: $message"
    powershell.exe -Command "& '$MOSQUITTO_PUB' -h $MQTT_HOST -t '$MQTT_TOPIC' -m '$message'"
}

# Function to monitor game state
monitor_game() {
    echo "Monitoring game state... (Press Ctrl+C to stop)"
    powershell.exe -Command "& '$MOSQUITTO_SUB' -h $MQTT_HOST -t '$MQTT_TOPIC/#' -v"
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
    publish_message "r"
    echo "Game reset."

    while true; do
        random_move
    done
}

# Main menu
show_menu() {
    clear
    echo "==========================="
    echo "Tic-Tac-Toe MQTT Controller"
    echo "==========================="
    echo "1. Make a move"
    echo "2. Reset game"
    echo "3. Monitor game state"
    echo "4. Randomize placement"
    echo "5. Exit"
    echo "==========================="
    echo -n "Enter your choice: "
}

# Make a move
make_move() {
    echo "Enter row,column coordinates (e.g. 2,3): "
    read coords
    publish_message "$coords"
    echo "Move sent: $coords"
    echo "Press Enter to continue..."
    read
}

# Main loop
while true; do
    show_menu
    read choice

    case $choice in
        1)
            make_move
            ;;
        2)
            publish_message "r"
            echo "Game reset command sent"
            echo "Press Enter to continue..."
            read
            ;;
        3)
            monitor_game
            echo "Press Enter to continue..."
            read
            ;;
        4)
            play_random_game
            ;;
        5)
            echo "Exiting..."
            exit 0
            ;;
        *)
            echo "Invalid option. Press Enter to continue..."
            read
            ;;
    esac
done
