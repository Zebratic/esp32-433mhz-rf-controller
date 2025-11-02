#!/bin/bash

# ESP-IDF Build Script
# This script sets up the ESP-IDF environment and builds the project

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== ESP32 433MHz Controller Build Script ===${NC}\n"

# Check if IDF_PATH is set
if [ -z "$IDF_PATH" ]; then
    echo -e "${YELLOW}ESP-IDF environment not set. Attempting to source...${NC}"

    # Try common ESP-IDF locations
    if [ -f "/opt/esp-idf/export.sh" ]; then
        echo "Found ESP-IDF at /opt/esp-idf"
        source "/opt/esp-idf/export.sh"
    elif [ -f "$HOME/esp/esp-idf/export.sh" ]; then
        echo "Found ESP-IDF at $HOME/esp/esp-idf"
        source "$HOME/esp/esp-idf/export.sh"
    else
        echo -e "${RED}Error: ESP-IDF not found!${NC}"
        echo "Please install ESP-IDF and set IDF_PATH, or run:"
        echo "  source /opt/esp-idf/export.sh"
        exit 1
    fi
fi

echo -e "${GREEN}IDF_PATH: $IDF_PATH${NC}\n"

# Parse command line arguments
ACTION=${1:-build}

case $ACTION in
    build)
        echo -e "${GREEN}Building project...${NC}"
        idf.py build
        ;;
    flash)
        PORT=${2:-/dev/ttyUSB0}
        echo -e "${GREEN}Building and flashing to $PORT...${NC}"
        idf.py -p $PORT flash
        ;;
    monitor)
        PORT=${2:-/dev/ttyUSB0}
        echo -e "${GREEN}Opening monitor on $PORT...${NC}"
        idf.py -p $PORT monitor
        ;;
    flash-monitor)
        PORT=${2:-/dev/ttyUSB0}
        echo -e "${GREEN}Building, flashing, and monitoring on $PORT...${NC}"
        idf.py -p $PORT flash monitor
        ;;
    clean)
        echo -e "${GREEN}Cleaning build...${NC}"
        idf.py fullclean
        ;;
    menuconfig)
        echo -e "${GREEN}Opening configuration menu...${NC}"
        idf.py menuconfig
        ;;
    *)
        echo "Usage: ./build.sh [action] [port]"
        echo ""
        echo "Actions:"
        echo "  build          - Build the project (default)"
        echo "  flash [port]   - Build and flash to device"
        echo "  monitor [port] - Open serial monitor"
        echo "  flash-monitor [port] - Flash and monitor"
        echo "  clean          - Clean build files"
        echo "  menuconfig     - Open ESP-IDF configuration"
        echo ""
        echo "Example:"
        echo "  ./build.sh flash-monitor /dev/ttyUSB0"
        exit 1
        ;;
esac
