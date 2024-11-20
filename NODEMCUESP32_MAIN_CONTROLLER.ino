#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>

#define FIREBASE_HOST "locker-2acb5-default-rtdb.firebaseio.com"
#define API_KEY "AIzaSyD9Y1mQQ5-tRQjUFzTI_N8MGNVHUP0SHbM"
#define AUTH_TOKEN "zpGboPgbDYUw1GXdkOnmZmgdyIoxKyQEVaeSUaod"

WiFiManager wifiManager;
AsyncWebServer server(80);

const int ledPin = 2;
const int processingLed = 15;
const int AccessGrantedLed = 13;
const int AccessDeniedLed = 12;

#define ARDUINO_RX_PIN 16
#define ARDUINO_TX_PIN 17
HardwareSerial SerialFromArduino(1);

const char START_MARKER = '<';
const char END_MARKER = '>';
const int MAX_UID_LENGTH = 8;
char uidBuffer[MAX_UID_LENGTH + 1];
String formattedUID;
int uidIndex = 0;
bool receivingUID = false;

bool accessGranted = false;
bool commandSent = false;
bool firebaseReady = false;
unsigned long lastLockerCheck = 0;
const unsigned long lockerCheckInterval = 30000;

int deniedRFIDCount = 1; // Start counting from 1

void setup() {
    Serial.begin(115200);
    SerialFromArduino.begin(9600, SERIAL_8N1, ARDUINO_RX_PIN, ARDUINO_TX_PIN);
    pinMode(ledPin, OUTPUT);
    pinMode(processingLed, OUTPUT);
    pinMode(AccessGrantedLed, OUTPUT);
    pinMode(AccessDeniedLed, OUTPUT);

    wifiManager.autoConnect("Main SmartLocker");
    Serial.println("Connected to WiFi");

    Serial.print("NodeMCU ESP32 IP Address: ");
    Serial.println(WiFi.localIP());

    GetMainIpAddress();
    updateWiFiConnection();

    if (checkFirebaseConnection()) {
        firebaseReady = true;
        Serial.println("Firebase is ready!");
        blinkLED(AccessGrantedLed, 3);
        updateMainConnection("Online");

        // Check and delete any pending TimeOut entries in the History database
        checkAndDeletePendingHistory();
    }

    server.on("/command", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("command", true)) {
            String command = request->getParam("command", true)->value();
            handleIncomingCommand(command);
            request->send(200, "text/plain", "Command received: " + command);
        } else {
            request->send(400, "text/plain", "Command not provided");
        }
    });

    server.begin();
    Serial.println("ESP32 ready to receive RFID data...");
} 

void loop() {
    if (WiFi.status() == WL_CONNECTED && firebaseReady) {
        digitalWrite(processingLed, LOW);
        digitalWrite(AccessGrantedLed, LOW);
        digitalWrite(AccessDeniedLed, LOW);
        Serial.println("Scan Available");

        unsigned long currentMillis = millis();
        if (currentMillis - lastLockerCheck >= lockerCheckInterval) {
            Serial.println("Scan Unavailable at the Moment...");
            checkLockerConnections();
            lastLockerCheck = currentMillis;
        }

        receiveRFID();

        if (accessGranted && !commandSent) {
            if (TimeInTimeOut()) {
                commandSent = true;
            }
        }

        if (commandSent) {
            accessGranted = false;
            commandSent = false;
        }
    } else {
        Serial.println("WiFi not connected, opening WiFiManager...");
        digitalWrite(ledPin, LOW);
        wifiManager.startConfigPortal("Main SmartLocker");
    }
    delay(1000);
}

void checkAndDeletePendingHistory() {
    if (WiFi.status() == WL_CONNECTED && firebaseReady) {
        String url = "https://" + String(FIREBASE_HOST) + "/History.json?auth=" + String(AUTH_TOKEN);
        HTTPClient http;
        http.begin(url);
        http.setTimeout(10000);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println("Received History data:");
            Serial.println(payload);

            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, payload);

            if (error) {
                Serial.print("Failed to parse JSON: ");
                Serial.println(error.c_str());
                return;
            }

            // Iterate through the History entries
            for (JsonPair entry : doc.as<JsonObject>()) {
                const char* tag = entry.key().c_str(); // Fix here: Get the key as a C-string
                const char* timeOut = entry.value()["TimeOut"];

                if (timeOut && String(timeOut).equalsIgnoreCase("pending")) {
                    // If TimeOut is "pending", delete this entry
                    String deleteUrl = "https://" + String(FIREBASE_HOST) + "/History/" + tag + ".json?auth=" + String(AUTH_TOKEN);
                    HTTPClient deleteHttp;
                    deleteHttp.begin(deleteUrl);
                    int deleteCode = deleteHttp.sendRequest("DELETE"); // Fix here: Use sendRequest to perform DELETE

                    if (deleteCode == HTTP_CODE_OK) {
                        Serial.println("Deleted pending history entry: " + String(tag));
                    } else {
                        Serial.println("Failed to delete pending history entry: " + String(tag));
                    }
                    deleteHttp.end();
                }
            }
        } else {
            Serial.print("Failed to read History data: ");
            Serial.println(http.errorToString(httpCode).c_str());
        }
        http.end();
    } else {
        Serial.println("WiFi not connected or Firebase unavailable.");
    }
}

String convertToHexFormat(const char* uid) {
    String hexUID = "";
    // Iterate through the string and process it in groups of two digits
    for (int i = 0; i < strlen(uid); i += 2) {
        // Get the next two characters (representing a byte)
        char byte[3] = {uid[i], uid[i + 1], '\0'}; // Ensure to properly terminate the string
        int byteValue = strtol(byte, NULL, 10);  // Convert the pair of digits to integer
        char hex[3];
        sprintf(hex, "%02X", byteValue);  // Convert the byte value to hexadecimal
        hexUID += hex;  // Append to the resulting hex string
    }
    return hexUID;
}

// Define the function to handle RFID reception
void receiveRFID() {
    while (SerialFromArduino.available() > 0) {
        char receivedChar = SerialFromArduino.read();

        if (receivedChar == START_MARKER) {
            receivingUID = true;
            uidIndex = 0;
        } else if (receivedChar == END_MARKER) {
            receivingUID = false;
            uidBuffer[uidIndex] = '\0';

            // Convert the UID to hex format and store it in the global formattedUID
            formattedUID = convertToHexFormat(uidBuffer);

            // Print the received and converted RFID UID
            Serial.print("Received RFID UID: ");
            Serial.println(formattedUID);

            // Check access with the converted UID
            checkAccessGranted(formattedUID);

            uidIndex = 0;
        } else if (receivingUID) {
            if (uidIndex < MAX_UID_LENGTH) {
                uidBuffer[uidIndex++] = receivedChar;
            }
        }
    }
}

// Other includes and setup code remain the same.

void checkAccessGranted(const String& uid) {
    if (WiFi.status() == WL_CONNECTED && firebaseReady) {
        String url = "https://" + String(FIREBASE_HOST) + "/Students.json?auth=" + String(AUTH_TOKEN);
        HTTPClient http;
        http.begin(url);
        http.setTimeout(10000);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println("Received JSON data:");
            Serial.println(payload);

            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, payload);

            if (error) {
                Serial.print("Failed to parse JSON: ");
                Serial.println(error.c_str());
                return;
            }

            bool accessGranted = false;
            String deniedRFID;

            for (JsonPair student : doc.as<JsonObject>()) {
                const char* rfid = student.value()["RFID"];
                deniedRFID = String(rfid);

                if (rfid && String(rfid).equalsIgnoreCase(uid)) {
                    accessGranted = true;
                    break;
                }
            }

            if (accessGranted) {
                digitalWrite(AccessGrantedLed, HIGH);
                Serial.println("Access Granted. Checking for available locker...");
                delay(2000);
                digitalWrite(AccessGrantedLed, LOW);

                if (TimeInTimeOut()) {
                    Serial.println("Locker successfully allocated.");
                } else {
                    Serial.println("No available locker at the moment.");
                }
            } else {
                digitalWrite(AccessDeniedLed, HIGH);
                delay(3000);
                digitalWrite(AccessDeniedLed, LOW);
                Serial.println("Access Denied");
                logDeniedRFID(formattedUID.c_str());
            }
        } else {
            Serial.print("Failed to read RFID data: ");
            Serial.println(http.errorToString(httpCode).c_str());
        }
        http.end();
    } else {
        Serial.println("WiFi not connected or Firebase unavailable.");
    }
}

bool TimeInTimeOut() {
    bool successfulScan = false;
    bool rfidFound = false;
    digitalWrite(processingLed, HIGH);
    
    // Construct URL for Firebase request
    String url = "https://" + String(FIREBASE_HOST) + "/Lockers.json?auth=" + String(AUTH_TOKEN);
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("Received locker data:");
        Serial.println(payload);

        // Reduce the DynamicJsonDocument size based on payload length
        const size_t capacity = JSON_OBJECT_SIZE(10) + payload.length() * 1.1;
        DynamicJsonDocument doc(capacity);
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
            Serial.print("Failed to parse JSON: ");
            Serial.println(error.c_str());
            http.end();
            return false;
        }

        // Step 1: Check if any locker has the same RFID (TIME OUT scenario)
        for (JsonPair locker : doc.as<JsonObject>()) {
            const char* ipAddress = locker.value()["ipAddress"];
            const char* occupant = locker.value()["occupant"];
            const char* connectionStatus = locker.value()["connectionStatus"];

            if (occupant && String(occupant).equalsIgnoreCase(formattedUID.c_str())) {
                rfidFound = true;

                if (connectionStatus && String(connectionStatus) == "Connected") {
                    // Send "TIME OUT" command if the locker is connected
                    digitalWrite(AccessGrantedLed, HIGH);
                    delay(100);
                    sendCommandToESP32CAM(ipAddress, "TIME OUT");
                    Serial.println("TIME OUT command sent to locker with matching RFID.");
                    successfulScan = true;
                    digitalWrite(processingLed, LOW);
                } else {
                    digitalWrite(processingLed, LOW);
                    // Deny TIME OUT if the locker is disconnected
                    for (int i = 0; i < 3; i++) {
                        digitalWrite(AccessDeniedLed, HIGH);
                        delay(200);
                        digitalWrite(AccessDeniedLed, LOW);
                        delay(200);
                    }
                    Serial.println("Access Denied: Locker is disconnected.");
                }
                break; // Exit after handling the RFID
            }
        }

        // Step 2: If no same RFID was found, look for an available locker (TIME IN scenario)
        if (!rfidFound) {
            for (JsonPair locker : doc.as<JsonObject>()) {
                const char* ipAddress = locker.value()["ipAddress"];
                const char* occupant = locker.value()["occupant"];
                const char* connectionStatus = locker.value()["connectionStatus"];

                if (occupant && String(occupant) == "none" && 
                    connectionStatus && String(connectionStatus) == "Connected") {
                    // Available locker found and is connected, send "TIME IN" command
                    delay(100);
                    bool timeInSuccess = sendCommandToESP32CAM(ipAddress, "TIME IN");
                    digitalWrite(processingLed, LOW);
                    
                    

                    if (timeInSuccess) {
                        updateLockerOccupant(locker.key().c_str(), formattedUID.c_str());
                        Serial.println("TIME IN command successful, locker occupant updated.");
                        digitalWrite(processingLed, LOW);
                        digitalWrite(AccessGrantedLed, HIGH);
                        delay(2000);
                    } else {
                        updateLockerOccupant(locker.key().c_str(), "none");
                        Serial.println("TIME IN command failed. Resetting occupant to 'none'.");
                        digitalWrite(processingLed, LOW);
                        digitalWrite(AccessDeniedLed, HIGH);
                        delay(2000);
                    }

                    successfulScan = true;
                    break; // Exit after handling the TIME IN
                }
            }

            // No available locker case
            if (!successfulScan) {
                digitalWrite(AccessDeniedLed, HIGH);
                delay(100);
                digitalWrite(AccessDeniedLed, LOW);
                Serial.println("No available locker at the moment");
                digitalWrite(processingLed, LOW);
                for (int i = 0; i < 3; i++) {
                        digitalWrite(AccessDeniedLed, HIGH);
                        delay(200);
                        digitalWrite(AccessDeniedLed, LOW);
                        delay(200);
                }
            }
        }
    } else {
        Serial.print("Failed to read locker data: ");
        Serial.println(http.errorToString(httpCode).c_str());
    }

    http.end();
    return successfulScan;
}

void updateMainConnection(const String& status) {
    String url = "https://" + String(FIREBASE_HOST) + "/MainController/mainConnection.json?auth=" + String(AUTH_TOKEN);
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    String jsonData = "\"" + status + "\"";
    int httpCode = http.PUT(jsonData);

    if (httpCode == HTTP_CODE_OK) {
        Serial.println("MainConnection updated to: " + status);
    } else {
        Serial.print("Failed to update MainConnection: ");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
}

void handleIncomingCommand(const String& command) {
    Serial.print("Handling command: ");
    Serial.println(command);
    
    if (command == "DISCONNECT") {
        WiFi.disconnect();
        delay(1000);
        wifiManager.resetSettings(); 
        delay(1000);
        Serial.println("Disconnect command received");
        wifiManager.startConfigPortal("Main SmartLocker");
    }
}

void updateWiFiConnection() {
    String currentSSID = WiFi.SSID();
    String url = "https://" + String(FIREBASE_HOST) + "/MainController/WifiConnection.json?auth=" + String(AUTH_TOKEN);
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    String jsonData = "\"" + currentSSID + "\"";
    int httpCode = http.PUT(jsonData);

    if (httpCode == HTTP_CODE_OK) {
        Serial.print("WiFiConnection updated to: ");
        Serial.println(currentSSID);
    } else {
        Serial.print("Failed to update WiFiConnection: ");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
}

void GetMainIpAddress() {
    String currentIP = WiFi.localIP().toString();
    String url = "https://" + String(FIREBASE_HOST) + "/MainController/ipAddress.json?auth=" + String(AUTH_TOKEN);
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    String jsonData = "\"" + currentIP + "\"";
    int httpCode = http.PUT(jsonData);

    if (httpCode == HTTP_CODE_OK) {
        Serial.print("IP Address updated to: ");
        Serial.println(currentIP);
    } else {
        Serial.print("Failed to update IP Address: ");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
}

bool sendCommandToESP32CAM(const char* ipAddress, const char* command) {
    WiFiClient client;
    const int httpPort = 80;

    Serial.print("Connecting to ");
    Serial.println(ipAddress);

    if (!client.connect(ipAddress, httpPort)) {
        Serial.println("Connection to server failed");
        return false; // Return false on failure
    }

    String url = "/command";
    String data = "command=" + String(command);
    Serial.print("Sending command: ");
    Serial.println(command);

    client.print(String("POST ") + url + " HTTP/1.1\r\n" +
                 "Host: " + ipAddress + "\r\n" +
                 "Content-Type: application/x-www-form-urlencoded\r\n" +
                 "Content-Length: " + data.length() + "\r\n" +
                 "Connection: close\r\n\r\n" + data);

    bool success = false;
    while (client.connected()) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            Serial.println(line);
            if (line.indexOf("HTTP/1.1 200 OK") >= 0) {
                success = true; // Connection successful
                break;
            }
        }
    }
    client.stop();
    return success; // Return true if the command was sent successfully
}

void updateLockerOccupant(const String& lockerKey, const char* occupant) {
    String path = "/Lockers/" + lockerKey + "/occupant.json?auth=" + String(AUTH_TOKEN);
    String url = "https://" + String(FIREBASE_HOST) + path;

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "\"" + String(occupant) + "\""; // Prepare JSON data
    int httpCode = http.PUT(jsonData);

    if (httpCode == HTTP_CODE_OK) {
        Serial.println("Occupant updated successfully.");
    } else {
        Serial.print("Failed to update occupant: ");
        Serial.println(http.errorToString(httpCode).c_str());
    }

    http.end();
}

void checkLockerConnections() {
    String url = "https://" + String(FIREBASE_HOST) + "/Lockers.json?auth=" + String(AUTH_TOKEN);
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("Received locker data:");
        Serial.println(payload);

        DynamicJsonDocument doc(1028); // Adjust size as needed
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
            Serial.print("Failed to parse JSON: ");
            Serial.println(error.c_str());
            return;
        }

        // Loop through each locker in the database
        for (JsonPair locker : doc.as<JsonObject>()) {
            const char* ipAddress = locker.value()["ipAddress"];
            const char* lockerKey = locker.key().c_str();

            bool isConnected = checkESP32CAMConnection(ipAddress); // Check connection status

            // Update Firebase with connection status
            updateLockerConnection(lockerKey, isConnected ? "Connected" : "Disconnected");
        }
    } else {
        Serial.print("Failed to read locker data: ");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
}

// Function to check if the ESP32-CAM is connected by sending a basic HTTP request
bool checkESP32CAMConnection(const char* ipAddress) {
    WiFiClient client;
    const int httpPort = 80;

    Serial.print("Checking connection to ");
    Serial.println(ipAddress);

    if (!client.connect(ipAddress, httpPort)) {
        Serial.println("Connection failed");
        return false; // Return false if unable to connect
    }

    // If connected, we assume the ESP32-CAM is reachable
    Serial.println("Connection successful");
    client.stop(); // Close connection
    return true;
}

void updateLockerConnection(const String& lockerKey, const char* connectionStatus) {
    String path = "/Lockers/" + lockerKey + "/connectionStatus.json?auth=" + String(AUTH_TOKEN);
    String url = "https://" + String(FIREBASE_HOST) + path;

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "\"" + String(connectionStatus) + "\""; // Prepare JSON data
    int httpCode = http.PUT(jsonData);

    if (httpCode == HTTP_CODE_OK) {
        Serial.print("Locker connection updated to ");
        Serial.println(connectionStatus);
    } else {
        Serial.print("Failed to update connection status: ");
        Serial.println(http.errorToString(httpCode).c_str());
    }

    http.end();
}

bool checkFirebaseConnection() {
    String url = "https://" + String(FIREBASE_HOST) + "/.json?auth=" + String(AUTH_TOKEN);
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    http.end();
    
    return (httpCode == HTTP_CODE_OK);
}

void logDeniedRFID(const String& rfid) {
    String RFIDtag = formattedUID.c_str();
    // First, we need to check if the RFID already exists as a tag in the DeniedRFID node.
    String url = "https://" + String(FIREBASE_HOST) + "/DeniedRFID/" + RFIDtag + ".json?auth=" + String(AUTH_TOKEN);
    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000); // Set timeout to 10 seconds

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();

        // If the tag exists, the payload will not be empty
        if (payload != "null") {
            Serial.println("RFID already denied: " + rfid);
        } else {
            // If the RFID is not found, we proceed to add it with a value of "RFID"
            String postUrl = "https://" + String(FIREBASE_HOST) + "/DeniedRFID/" + RFIDtag + ".json?auth=" + String(AUTH_TOKEN);
            http.begin(postUrl);
            http.addHeader("Content-Type", "application/json");

            String jsonData = "\"RFID\"";  // Value to store under the RFID tag
            int postCode = http.PUT(jsonData);

            if (postCode == HTTP_CODE_OK) {
                Serial.println("Denied RFID added with RFID: " + RFIDtag);
            } else {
                Serial.print("Failed to log Denied RFID: ");
                Serial.println(http.errorToString(postCode).c_str());
            }
        }
    } else {
        Serial.print("Failed to check DeniedRFID: ");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
}

void blinkLED(int ledPin, int times) {
    for (int i = 0; i < times; i++) {
        digitalWrite(ledPin, HIGH);
        delay(100);
        digitalWrite(ledPin, LOW);
        delay(100);
    }
}