#include "BluetoothSerial.h"  // BT: Include the Serial Bluetooth library
#include <TinyGPSPlus.h>      // Include TinyGPSPlus library for GPS parsing
#include <LiquidCrystal.h>    // Include LiquidCrystal library for LCD display

#define LED_BT 2  // BT: Internal LED (or LED on the pin D2) for the connection indication (connected solid/disconnected blinking)
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
// Pins for Display
#define RS 13
#define EN 14
#define D4 26
#define D5 25
#define D6 33
#define D7 32

const double EARTH_RADIUS_KM = 6371.0;   // Earth radius in kilometers
const double KM_TO_YARDS = 1093.613298;  // Conversion factor for kilometers to yards
const int GPS_BAUD_RATE = 9600;
const int MAX_RETRIES = 5;                     // Maximum number of retries for Bluetooth initialization
const unsigned long BT_CHECK_INTERVAL = 1000;  // 1 seconds
const unsigned long UPDATE_INTERVAL = 1000;    // 1 second update rate

bool ledBtState = false;                    // BT: Variable used to change the indication LED state
bool MasterConnected = false;               // BT: Variable to store the current connection state (true=connected/false=disconnected)
String device_name = "ESP32-BT-Slave";      // BT: Device name for the slave (client)
String MACadd = "1C:69:20:C6:5E:32";        // BT: Use the slave MAC address
double Master_latitude = 42.3202225;        // testing change back to 0.0
double Master_longitude = -83.234719;       // testing change back to 0.0
double my_lat = 0.0;                        // Variable to store the current GPS latitude
double my_lng = 0.0;                        // Variable to store the current GPS longitude
TinyGPSPlus gps;                            // Create an instance of the TinyGPSPlus library
HardwareSerial gpsSerial(1);                // Use hardware serial port 1 for GPS communication
char gpsData[256];                          // Fixed-size buffer for incoming GPS data
static double lastDist = -1.0;              // For updating the screen only when there is a change
static double lastDir = -1.0;               // For updating the screen only when there is a change
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);  // LCD Screen

// BT: Bluetooth availability check
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run make menuconfig to enable it.
#endif
// BT: Serial Bluetooth availability check
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Bluetooth not available or not enabled. It is only available for the ESP32 chip.
#endif

BluetoothSerial SerialBT;  // BT: Set the Bluetooth Serial Object

/**
 * @brief Callback function for Bluetooth status events.
 * 
 * This function handles events for Bluetooth connection status such as
 * when the master device connects or disconnects. It updates the 
 * `MasterConnected` variable accordingly.
 * 
 * @param event The Bluetooth event that occurred (e.g., connection open or close).
 * @param param Parameters associated with the event.
 * 
 * @return void
 */
void Bt_Status(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {  // BT: Checks if the SPP Server connection is open
    Serial.println("Master Connected");
    MasterConnected = true;  // BT: Server is connected to the slave
    digitalWrite(LED_BT, HIGH);             // Turn LED on
  } else if (event == ESP_SPP_CLOSE_EVT) {  // BT: Checks if the SPP connection is closed
    Serial.println("Master Disconnected");
    MasterConnected = false;  // BT: Server connection lost
    digitalWrite(LED_BT, LOW);  // Turn LED off
  }
}

/**
 * @brief Parses GPS data to extract latitude and longitude.
 * 
 * This function extracts and validates latitude and longitude from a 
 * formatted GPS data string. The function handles errors in the data format 
 * and ensures the extracted values are within valid ranges.
 * 
 * @param gpsData A string containing the GPS data in a specific format.
 * 
 * @return true If the GPS data is valid and successfully parsed.
 * @return false If there is an error parsing the GPS data.
 */
bool gps_parse(String gpsData) {
  if (gpsData.isEmpty()) {
    Serial.println("Error: Emptry GPS Data");
    return false;
  }
  // Validate if the data contains the necessary labels
  if (gpsData.indexOf("Latitude:") == -1 || gpsData.indexOf("Longitude:") == -1) {
    Serial.println("Error: Invalid GPS data format.");
    return false;
  }
  if (gpsData.length() > 0) {      // Only proceed if the data is non-empty
    if (gpsData.length() > 128) {  // Prevent excessively long strings
      Serial.println("Error: GPS data too long.");
      return false;
    }
    Serial.print("Received GPS Data: ");
    Serial.println(gpsData);  // Print the received GPS data to the serial monitor

    // Find the start and end positions of the latitude value int latStart = gpsData.indexOf("Latitude:") + 9;  // Start after "Latitude:"
    int latStart = gpsData.indexOf("Latitude:") + 9;    // The start of the latitude number (after "Latitude:")
    int latEnd = gpsData.indexOf("Longitude:") - 1;     // The end of the latitude value, before "Longitude:"
    int lonStart = gpsData.indexOf("Longitude:") + 10;  // The start of the longitude number (after "Longitude:")
    int lonEnd = gpsData.length();                      // The end of the longitude value (end of the string)

    if (latStart == -1 || latEnd == -1 || lonStart == -1 || latStart >= latEnd || lonStart <= latEnd) {
      Serial.println("Error: Unable to parse latitude and longitude.");
      return false;
    }

    // Use String.substring() to extract latitude and longitude
    String latStr = gpsData.substring(latStart, latEnd);
    String lonStr = gpsData.substring(lonStart);

    // Remove any extra spaces or newlines
    latStr.trim();
    lonStr.trim();

    // Ensure that the extracted strings are not empty or excessively long
    if (latStr.length() == 0 || lonStr.length() == 0) {
      Serial.println("Error: Empty latitude or longitude extracted.");
      return false;
    }

    if (latStr.length() > 15 || lonStr.length() > 15) {
      Serial.println("Error: Extracted latitude or longitude is too long.");
      return false;
    }

    // Convert extracted strings to double
    Master_latitude = latStr.toDouble();
    Master_longitude = lonStr.toDouble();

    // Validate range for latitude
    if (Master_latitude < -90.0 || Master_latitude > 90.0) {
      Serial.println("Error: Latitude out of range.");
      return false;
    }

    // Validate range for longitude
    if (Master_longitude < -180.0 || Master_longitude > 180.0) {
      Serial.println("Error: Longitude out of range.");
      return false;
    }

    // Print the parsed latitude and longitude values for debugging
    Serial.printf("Parsed Latitude: %.11lf, Longitude: %.11lf\n", Master_latitude, Master_longitude);  // Print with 8 decimal places
    return true;
  }
  return false;
}

/**
 * @brief Retrieves the current GPS data from the GPS module.
 * 
 * This function reads data from the GPS module and updates the `my_lat` 
 * and `my_lng` variables with the latest coordinates if the data is valid.
 * 
 * @return void
 */
void getGPSData() {
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());  // Send data to the GPS object for decoding
  }

  // Once data is processed, save the latitude and longitude to my_lat and my_lng
  if (gps.location.isUpdated()) {  // Check if a new location update is available
    my_lat = gps.location.lat();   // Get the latitude from the GPS module
    my_lng = gps.location.lng();   // Get the longitude from the GPS module

    // Print the current GPS coordinates
    Serial.print("Current Latitude: ");
    Serial.println(my_lat, 11);  // Print latitude with 11 decimal places
    Serial.print("Current Longitude: ");
    Serial.println(my_lng, 11);  // Print longitude with 11 decimal places
  } else {
    Serial.println("Warning: No GPS data available or signal is weak.");
  }
}

// Helper function to convert degrees to radians
inline double degreesToRadians(double degrees) {
  return degrees * M_PI / 180.0;
}

// Helper function to convert radians to degrees
inline double radiansToDegrees(double radians) {
  return radians * 180.0 / M_PI;
}

/**
 * @brief Calculates the distance between two GPS coordinates.
 * 
 * This function uses the Haversine formula to compute the distance between 
 * two sets of GPS coordinates in yards.
 * 
 * @param lat1 Latitude of the first point.
 * @param lon1 Longitude of the first point.
 * @param lat2 Latitude of the second point.
 * @param lon2 Longitude of the second point.
 * 
 * @return double The distance between the two points in yards.
 */
double distance(double lat1, double lon1, double lat2, double lon2) {
  if (lat1 == lat2 && lon1 == lon2) {
    return 0.0;
  }
  // Convert latitude and longitude from degrees to radians
  double lat1_rad = degreesToRadians(lat1);
  double lon1_rad = degreesToRadians(lon1);
  double lat2_rad = degreesToRadians(lat2);
  double lon2_rad = degreesToRadians(lon2);
  // Differences in coordinates
  double dlat = lat2_rad - lat1_rad;
  double dlon = lon2_rad - lon1_rad;
  // Haversine formula
  double a = sin(dlat / 2.0) * sin(dlat / 2.0) + cos(lat1_rad) * cos(lat2_rad) * sin(dlon / 2.0) * sin(dlon / 2.0);
  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

  return EARTH_RADIUS_KM * c * KM_TO_YARDS;  // Return distance in yards
}

/**
 * @brief Maps a bearing (angle) to a cardinal compass direction.
 * 
 * This function converts a bearing (angle) in degrees to a cardinal direction 
 * (e.g., North, East, South, West).
 * 
 * @param bearing The bearing in degrees.
 * 
 * @return char* A string representing the compass direction.
 */
char* getCompassDirection(double bearing) {
  if (bearing >= 348.75 || bearing < 11.25) return "N";          // North
  else if (bearing >= 11.25 && bearing < 33.75) return "NNE";    // North-NorthEast
  else if (bearing >= 33.75 && bearing < 56.25) return "NE";     // NorthEast
  else if (bearing >= 56.25 && bearing < 78.75) return "ENE";    // East-NorthEast
  else if (bearing >= 78.75 && bearing < 101.25) return "E";     // East
  else if (bearing >= 101.25 && bearing < 123.75) return "ESE";  // East-SouthEast
  else if (bearing >= 123.75 && bearing < 146.25) return "SE";   // SouthEast
  else if (bearing >= 146.25 && bearing < 168.75) return "SSE";  // South-SouthEast
  else if (bearing >= 168.75 && bearing < 191.25) return "S";    // South
  else if (bearing >= 191.25 && bearing < 213.75) return "SSW";  // South-SouthWest
  else if (bearing >= 213.75 && bearing < 236.25) return "SW";   // SouthWest
  else if (bearing >= 236.25 && bearing < 258.75) return "WSW";  // West-SouthWest
  else if (bearing >= 258.75 && bearing < 281.25) return "W";    // West
  else if (bearing >= 281.25 && bearing < 303.75) return "WNW";  // West-NorthWest
  else if (bearing >= 303.75 && bearing < 326.25) return "NW";   // NorthWest
  else if (bearing >= 326.25 && bearing < 348.75) return "NNW";  // North-NorthWest
  return "X";                                                    // Default case, should never be hit
}

/**
 * @brief Calculates the bearing (direction) from one GPS coordinate to another.
 * 
 * This function calculates the direction (bearing) from the current GPS position 
 * to the master device's coordinates.
 * 
 * @param lat1 Latitude of the first point (current position).
 * @param lon1 Longitude of the first point (current position).
 * @param lat2 Latitude of the second point (master device).
 * @param lon2 Longitude of the second point (master device).
 * 
 * @return double The bearing in degrees from the current position to the master device.
 */
double direction(double lat1, double lon1, double lat2, double lon2) {
  double phi = log(tan(lat2 / 2 + M_PI / 4) / tan(lat1 / 2 + M_PI / 4));
  double lon = abs(lon1 - lon2);
  double bearing = atan2(lon, phi);
  return radiansToDegrees(bearing);  // Returns bearing in degrees
}

/**
 * @brief Updates the LCD display with the current distance and direction.
 * 
 * This function calculates the distance and direction between the master device
 * and the current GPS position, and updates the LCD with the latest data. The
 * display is updated only if there is a change in either distance or direction.
 * 
 * @return void
 */
void updateDisplay() {
  double dist = distance(Master_latitude, Master_longitude, my_lat, my_lng);
  Serial.printf("Distance to Master: %.2f yards\n", dist);
  double dir = direction(Master_latitude, Master_longitude, my_lat, my_lng);
  char* dirStr = getCompassDirection(dir);             // Convert bearing to cardinal direction
  Serial.printf("Direction to Master: %s\n", dirStr);  // Print the cardinal direction

  // Only call display if there's a change in distance or direction
  if (dist != lastDist || dir != lastDir) {
    display(dist, dir, dirStr);  // Update the display with new data
  }
}

/**
 * @brief Displays distance and direction on the LCD screen.
 * 
 * This function formats and displays the distance and direction values on 
 * the LCD screen. It only updates the display if the values have changed.
 * 
 * @param dist The distance in yards.
 * @param dir The direction in degrees.
 * @param cardinal The cardinal direction string.
 * 
 * @return void
 */
void display(double dist, double dir, char* cardinal) {
  // Create string buffers to hold the formatted distance and direction
  char distStr[10];
  char dirStr[10];

  // Convert dist (distance) and dir (direction) to strings
  dtostrf(dist, 3, 2, distStr);  // Convert distance to string with 2 decimal places
  dtostrf(dir, 3, 0, dirStr);    // Convert direction to string with no decimals

  // Check if the distance has changed before updating
  if (dist != lastDist) {
    // Position the cursor to the top left of the screen and print the distance
    lcd.setCursor(0, 0);  // (col, row)
    lcd.print(distStr);   // Print the formatted distance
    lcd.print(" yards");  // Appends "yards" after the distance
    lastDist = dist;      // Update the last distance value
  }

  // Check if the direction has changed before updating
  if (dir != lastDir) {
    // Position the cursor to the bottom left of the screen and print the direction
    lcd.setCursor(0, 1);  // (col, row)
    lcd.print("Dir: ");   // Print "Dir:" to indicate direction
    lcd.print(cardinal);
    lcd.print(" ");
    lcd.print(dirStr);  // Print the formatted direction
    lcd.print(" deg  ");  // Appends "deg" after the bearing
    lastDir = dir;      // Update the last direction value
  }
}

/**
 * @brief Updates GPS data by fetching new coordinates.
 * 
 * This function calls `getGPSData()` and updates the current GPS position
 * if new data is available.
 * 
 * @return void
 */
void updateGPS() {
  getGPSData();
  if (gps.location.isValid()) {
    my_lat = gps.location.lat();
    my_lng = gps.location.lng();
  }
}

/**
 * @brief Initializes the Bluetooth communication and attempts to connect.
 * 
 * Tries to initialize the Bluetooth communication with a given device name
 * and MAC address. It retries the initialization up to a maximum number of 
 * retries. If initialization fails, the device is restarted.
 * 
 * @return void
 */
void initBluetooth() {
  bool btInitialized = false;

  // Attempt to initialize Bluetooth with a limited number of retries
  for (int retry = 0; retry < MAX_RETRIES; retry++) {
    if (SerialBT.begin(device_name)) {
      Serial.println("Bluetooth initialized successfully.");
      btInitialized = true;
      break;
    } else {
      Serial.printf("Bluetooth initialization failed. Retry %d/%d...\n", retry + 1, MAX_RETRIES);
      delay(1000);  // Wait 1 second before retrying
    }
  }

  if (!btInitialized) {
    Serial.println("Bluetooth initialization failed after maximum retries. Restarting...");
    ESP.restart();  // Restart the ESP32 if Bluetooth fails to initialize
  }

  // BT: Define the Bt_Status callback
  SerialBT.register_callback(Bt_Status);
  Serial.printf("The device with name \"%s\" and MAC address \"%s\" is started.\nNow you can pair it with Bluetooth!\n", device_name.c_str(), MACadd.c_str());
}

/**
 * @brief Attempts to reconnect to Bluetooth if disconnected.
 * 
 * This function tries to reconnect to the Bluetooth master device if the 
 * connection is lost. It retries up to a specified number of times.
 * 
 * @return void
 */
void reconnectBluetooth() {
  Serial.println("Attempting to reconnect to Bluetooth...");

  // Try reconnecting to Bluetooth if not connected
  if (!SerialBT.hasClient()) {
    // Try to reconnect a few times (3 retries)
    const int MAX_RETRIES = 3;
    bool connected = false;

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
      Serial.println("Reinitializing Bluetooth...");
      if (SerialBT.begin(device_name)) {
        Serial.println("Bluetooth reconnected.");
        connected = true;
        break;
      } else {
        Serial.printf("Failed to reconnect (Attempt %d/%d)\n", attempt + 1, MAX_RETRIES);
        delay(1000);  // Wait 1 second before retrying
      }
    }

    if (connected) {
      MasterConnected = true;      // Mark as connected
      digitalWrite(LED_BT, HIGH);  // Turn on the Bluetooth connection LED
    } else {
      Serial.println("Bluetooth reconnect failed. Please check your Bluetooth connection.");
      MasterConnected = false;    // Ensure we don't proceed without a connection
      digitalWrite(LED_BT, LOW);  // Turn off the LED indicating disconnected state
    }
  }
}

/**
 * @brief Initializes hardware components and configurations.
 * 
 * This function configures the serial communication, Bluetooth, GPS, and LCD 
 * components at the start of the program.
 * 
 * @return void
 */
void setup() {
  Serial.begin(115200);       // Sets the data rate for serial data transmission, allowing communication with the Serial Monitor
  pinMode(LED_BT, OUTPUT);    // Set LED pin as output to indicate Bluetooth connection status
  digitalWrite(LED_BT, LOW);  // Start with LED off, indicating no Bluetooth connection

  // Initialize Bluetooth module
  initBluetooth();  // Calls the initBluetooth function to set up Bluetooth communication.

  // Initialize the GPS serial communication
  gpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);  // Initialize GPS module with RX and TX pins 16 and 17
  Serial.println("Initialization complete.");                          // Log completion of setup

  //Initialize the size of the display. 16 Columns (0-15) 2 Rows (0-1)
  lcd.begin(16, 2);            // Initialize the LCD display with 16 columns and 2 rows
  lcd.clear();                 // Clear any previous content on the display
  display(10.00, 90.00, "Z");  // Call the display function with empty data  (0.0, 0.0) for initial view
}

/**
 * @brief Main loop for checking Bluetooth and GPS status.
 * 
 * This function continuously checks the Bluetooth connection status and 
 * retrieves GPS data. It also updates the LCD display with the latest 
 * information.
 * 
 * @return void
 */
void loop() {
  static unsigned long lastUpdateTime = 0;  // Tracks the last time the loop executed specific tasks
  unsigned long lastBtCheck = 0;            // Variable to track when to check Bluetooth status

  // Periodically check Bluetooth status every BT_CHECK_INTERVAL (5 seconds).
  if (millis() - lastBtCheck >= BT_CHECK_INTERVAL) {
    lastBtCheck = millis();  // Update the last Bluetooth check time
    if (!MasterConnected) {  // If Bluetooth is disconnected, attempt to reconnect
      reconnectBluetooth();  // Call the reconnection function if disconnected
    }
  }

  // Check if it's time to perform an update
  if (millis() - lastUpdateTime >= UPDATE_INTERVAL) {
    lastUpdateTime = millis();  // Reset the last update time

    getGPSData();  // Fetches GPS data from getGPSData from the GPS module

    // If Bluetooth is connected, handle incoming data
    if (MasterConnected) {
      // BT: Check if data is available from the Bluetooth
      if (SerialBT.available()) {
        Serial.println(gpsData);
        size_t maxLen = sizeof(gpsData) - 1;  // Max buffer size, leaving space for null-terminator
        if (SerialBT.available() > maxLen) {  // If incoming data is larger than the buffer size, handle error
          Serial.println("Error: Incoming data too large.");
          SerialBT.flush();  // Clear the Bluetooth buffer to discard the oversized data
          return;
        }
        int len = SerialBT.readBytesUntil('\n', gpsData, maxLen);  // Read GPS data until newline character or buffer is full
        gpsData[len] = '\0';                                       // Null-terminate the string

        if (len > 0) {                        // If the received data length is greater than 0 (valid data)
          if (!gps_parse(String(gpsData))) {  // Ensure gps_parse returns success
            Serial.println("Error: Failed to parse GPS data.");
          }
        } else {
          Serial.println("Error: Received empty or invalid GPS data.");
        }
      }
    } else {
      Serial.println("Waiting for Master to connect...");  // Print message if Bluetooth is disconnected
    }

    // Check Master GPS coordinates are available before calculating distance
    if (Master_latitude == 0.0 && Master_longitude == 0.0) {
      Serial.println("Warning: Master GPS coordinates are not set.");  // Alert if Master GPS data is not set
      return;                                                          // Exit the loop if Master coordinates are missing
    }

    // Proceed only if GPS data is valid
    if (!gps.location.isValid()) {
      Serial.println("Warning: GPS data is invalid or not updated.");  // Alert if GPS data is invalid
      return;                                                          // Exit if the GPS location is invalid.
    }

    // Update the LCD display with the current distance and direction.
    updateDisplay();

    // Log additional data to the serial monitor
    Serial.printf("Current Location:\t Lat: %.11lf, Long: %.11lf\n", my_lat, my_lng);
    Serial.printf("Master Location:\t Lat: %.11lf, Long: %.11lf\n", Master_latitude, Master_longitude);
  }
}
