#include "BluetoothSerial.h"
#include "TinyGPS++.h"

#define RXD2 16
#define TXD2 17
#define GPS_BAUD 9600

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
BluetoothSerial SerialBT;

unsigned long previousMillisReconnect;
bool SlaveConnected = false;
int recatt = 0;

String myName = "ESP32-BT-Master";
String slaveName = "ESP32-BT-Slave";
String MACadd = "1C:69:20:C6:5E:32";  // Adjust as needed
uint8_t address[6] = { 0x1C, 0x69, 0x20, 0xC6, 0x5E, 0x32 };  // MAC address of the slave device

// Bluetooth status callback function
void Bt_Status(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_OPEN_EVT) {
    Serial.println("Client Connected");
    SlaveConnected = true;
    recatt = 0;
  }
  else if (event == ESP_SPP_CLOSE_EVT) {
    Serial.println("Client Disconnected");
    SlaveConnected = false;
  }
}

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);

  //Set up Bluetooth callback
  SerialBT.register_callback(Bt_Status);
  SerialBT.begin(myName, true);
  Serial.printf("The device \"%s\" started in master mode\n", myName.c_str());
  
  //Try connecting to the slave
 SlaveConnect();  
}

void SlaveConnect() {
  Serial.println("Connecting to slave BT device...");
  Serial.printf("Connecting to \"%s\" with MAC \"%s\"...\n", slaveName.c_str(), MACadd.c_str());
  SerialBT.connect(address);
}


void getGPSData() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
    delay(10);
    if (gps.location.isUpdated()) {
      String gpsData = "Latitude: " + String(gps.location.lat(), 8) + " Longitude: " + String(gps.location.lng(), 8);
      Serial.println(gpsData);  // Print GPS data to serial monitor
      if (SlaveConnected) {
        SerialBT.println(gpsData);  // Send GPS data to the slave via Bluetooth

      }
    }
  }
}

void loop() {
  // Check if connected to slave
  if (!SlaveConnected) {
    if (millis() - previousMillisReconnect >= 10000) {
      previousMillisReconnect = millis();
      recatt++;
      Serial.print("Reconnection attempt: ");
      Serial.println(recatt);
      
      if (recatt <= 5) {  // Limit the reconnection attempts to prevent overheating
        SlaveConnect();
      } else {
        Serial.println("Max reconnection attempts reached.");
      }
    }
  }

  // Get GPS data
  getGPSData();

  // Bluetooth data communication
  if (Serial.available()) {
    SerialBT.write(Serial.read());
  }

  if (SerialBT.available()) {
    Serial.write(SerialBT.read());
  }

  delay(1000);  // Send data every 10 seconds
}
