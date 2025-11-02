# ESP32 433MHz RF Controller

An ESP32-based 433MHz RF signal receiver and transmitter with a modern web interface for capturing, storing, and retransmitting RF signals.

![Signal History](images/signals-history.png)

## Features

- **Real-time Signal Monitoring**: Automatically captures and displays 433MHz RF signals with live updates
- **Signal History Tracking**: Tracks all detected signals with relative timestamps (e.g., "2m 1s ago") and detection counts
- **Signal Management**: Save, edit, and delete captured signals with custom names
- **Direct Signal Transmission**: Replay signals instantly without saving, or transmit from your saved library
- **Manual Signal Entry**: Create and transmit custom signals manually
- **Advanced Noise Filtering**: Intelligent filtering to reduce false positives and noise
- **Modern Web Interface**: Dark mode, responsive UI with minimalistic, blocky design
- **RESTful API**: Complete API for programmatic control
- **Persistent Storage**: Signals saved to non-volatile storage (NVS) survive reboots
- **Settings Management**: Configurable parameters via web interface

## Hardware Requirements

- ESP32 DevKit (any variant)
- 433MHz RF Receiver Module
- 433MHz RF Transmitter Module
- Breadboard and jumper wires (optional)

## GPIO Connections

![ESP32 Wiring Diagram](images/esp32-diagram.png)

- **RF Receiver**: GPIO 4
- **RF Transmitter**: GPIO 2

Refer to the wiring diagram above for visual connection guide.

## Setup and Compilation

### Prerequisites

1. **Install ESP-IDF v5.5**
   - Follow the [official ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
   - Ensure `idf.py` is available in your PATH

2. **Clone the Repository**
   ```bash
   git clone <repository-url>
   cd esp-433mhz-transmitter
   ```

### Configuration

1. **Configure WiFi Credentials**
   - Copy the example WiFi config file:
     ```bash
     cp main/wifi_config.h.example main/wifi_config.h
     ```
   - Edit `main/wifi_config.h` with your WiFi credentials:
     ```c
     #define WIFI_SSID      "YourWiFiSSID"
     #define WIFI_PASS      "YourWiFiPassword"
     #define MAX_RETRY      10
     ```
   - Note: `wifi_config.h` is git-ignored to protect your credentials

### Build and Flash

1. **Build the Project**
   ```bash
   ./build.sh build
   ```

2. **Flash to ESP32**
   ```bash
   ./build.sh flash
   ```
   Or manually:
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
   Replace `/dev/ttyUSB0` with your ESP32's serial port.

3. **Access the Web Interface**
   - The ESP32 will print its IP address to the serial monitor
   - Open a browser and navigate to `http://<esp32-ip-address>`
   - Example: `http://192.168.1.100`

## Web Interface

The web interface provides four main tabs:

### Monitor Tab
- Real-time display of captured RF signals
- Shows signal code, hex representation, bit length, protocol, and pulse length
- Relative timestamps showing when signals were detected
- Quick replay and save buttons for each detected signal

### Signals Tab
![Saved Signals](images/saved-signals.png)
- View all saved signals with their names
- Edit signal names
- Delete signals
- Transmit saved signals

### Manual Tab
![Manual Signal Entry](images/manual-signals.png)
- Manually create signals by entering code, bit length, protocol, and pulse length
- Test transmission of custom signals

### Settings Tab
![Settings](images/settings.png)
- Configure various settings
- Settings are saved to browser's local storage

## API Documentation

The ESP32 provides a RESTful API for programmatic control. All endpoints return JSON responses.

**Available Endpoints:**
- `GET /api/info` - Get device information and status
- `GET /api/signal-history` - Get all tracked signals and latest detected signal
- `GET /api/signals` - Get all saved signals
- `POST /api/signals` - Save a new signal
- `PUT /api/signals/{index}` - Update a signal's name
- `DELETE /api/signals/{index}` - Delete a saved signal
- `POST /api/transmit` - Transmit a signal directly (without saving)
- `POST /api/transmit/{index}` - Transmit a saved signal by index
- `POST /api/transmit/name/{name}` - Transmit a saved signal by name
- `POST /api/clear-tracking` - Clear all tracked signal history
- `POST /api/settings` - Save application settings

For detailed API documentation with request/response examples, see the **API** tab in the web interface.

## Development

### Project Structure

```
esp-433mhz-transmitter/
├── main/
│   ├── main.c              # Main application code
│   ├── CMakeLists.txt     # Build configuration
│   ├── wifi_config.h      # WiFi credentials (git-ignored)
│   └── web/               # Embedded web interface
│       ├── css/           # Stylesheets
│       ├── js/            # JavaScript modules
│       └── tabs/          # Tab HTML templates
├── components/
│   └── rc_switch/         # RC switch library
├── images/               # Documentation images
├── build.sh              # Build script
└── README.md             # This file
```

### Building

The `build.sh` script handles ESP-IDF environment setup and building:

```bash
./build.sh build    # Build the project
./build.sh flash    # Flash to ESP32
./build.sh monitor  # Open serial monitor
```

## License

Open-source project. Refer to the LICENSE file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.
