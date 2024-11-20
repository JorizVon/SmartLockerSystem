#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiManager.h>

const String lockerName = "Locker_3";  // Variable for easy editing of locker name
const String lockerPath = "/Lockers/" + lockerName+".json";  // Specify JSON format in the path
const String mainControllerPath = "/MainController.json";  // Path to MainController data

WiFiManager wifiManager;
WebServer server(80);  // Create a web server object that listens on port 80

// Replace with your Firebase project credentials
#define FIREBASE_HOST "locker-2acb5-default-rtdb.firebaseio.com"
#define API_KEY "AIzaSyD9Y1mQQ5-tRQjUFzTI_N8MGNVHUP0SHbM"

const char* ntpServer = "pool.ntp.org";
const long  utcOffsetInSeconds = 3600;  // Adjust for your timezone

WiFiUDP udp;
NTPClient timeClient(udp, ntpServer, utcOffsetInSeconds); 

String receivedCommand = "";

const int relayPin = 13;

// Define reed switch pin
const int reedSwitchPin = 14;

const int BuzzerPin = 2;
// Define ultrasonic sensor pins
const int triggerPin = 12;
const int echoPin = 15;

// Timing variables
unsigned long commandReceivedTime = 0;
const unsigned long unlockLedOnDuration = 3000;
bool commandReceived = false;
bool waitingForReedSwitchDeactivation = false;
bool reedSwitchDeactivated = false;
bool unexpectedReedSwitchChange = false;

// Track reed switch state
bool previousReedSwitchState = HIGH;

// Function to URL encode strings
String urlEncode(const String& str) {
  String encoded = "";
  char c;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encoded += "%20";
    } else if (c == '/') {
      encoded += "%2F";
    } else if (c == '?') {
      encoded += "%3F";
    } else if (c == '=') {
      encoded += "%3D";
    } else if (c == '&') {
      encoded += "%26";
    } else {
      encoded += c;
    }
  }
  return encoded;
}

// Function to update the lock status of a specific locker in Firebase
void updateLockStatus(const String& lockerId, const String& status) {
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/Lockers/" + lockerId + ".json?auth=" + API_KEY;
  String jsonData = "{\"lockStatus\":\"" + status + "\"}";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.PATCH(jsonData);

  if (httpResponseCode > 0) {
    Serial.print("Lock Status Updated. HTTP Response Code: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("Error Code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

void updateIPAddress(const String& lockerId) {
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/Lockers/" + lockerId + ".json?auth=" + API_KEY;
  
  // Get the local IP address of the ESP32
  String ipAddress = WiFi.localIP().toString();
  String jsonData = "{\"ipAddress\":\"" + ipAddress + "\"}";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.PATCH(jsonData);

  if (httpResponseCode > 0) {
    Serial.print("IP Address Updated: ");
    Serial.println(ipAddress);
    Serial.print("HTTP Response Code: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("Error Code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

// Function to update the condition of a specific locker in Firebase
void updateCondition(const String& lockerId, const String& condition) {
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/Lockers/" + lockerId + ".json?auth=" + API_KEY;
  String jsonData = "{\"condition\":\"" + condition + "\"}";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.PATCH(jsonData);

  if (httpResponseCode > 0) {
    Serial.print("Condition Updated: ");
    Serial.println(condition);
    Serial.print("HTTP Response Code: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("Error Code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

// Function to update the occupant of a specific locker in Firebase
void updateOccupant(const String& lockerId, const String& occupant) {
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/Lockers/" + lockerId + ".json?auth=" + API_KEY;
  String jsonData = "{\"occupant\":\"" + occupant + "\"}";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.PATCH(jsonData);

  if (httpResponseCode > 0) {
    Serial.print("Occupant Updated: ");
    Serial.println(occupant);
    Serial.print("HTTP Response Code: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("Error Code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

// Function to get the condition of a specific locker from Firebase
String getCondition(const String& lockerId) {
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/Lockers/" + lockerId + ".json?auth=" + API_KEY;

  http.begin(url);
  int httpResponseCode = http.GET();

  String condition = "";

  if (httpResponseCode > 0) {
    String payload = http.getString();
    Serial.println("Payload received: " + payload);

    // Use ArduinoJson to parse the payload
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      condition = doc["condition"].as<String>();
      Serial.println("Extracted Condition: " + condition);
    } else {
      Serial.print("JSON deserialization failed: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.print("Error Code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return condition;
}

// Function to get the distance limit from Firebase
int getDistanceLimit(const String& lockerId) {
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/Lockers/" + lockerId + ".json?auth=" + API_KEY;

  http.begin(url);
  int httpResponseCode = http.GET();

  int distancelimit = 0;

  if (httpResponseCode > 0) {
    String payload = http.getString();
    int distanceStart = payload.indexOf("\"distancelimit\":") + 16;
    int distanceEnd = payload.indexOf(",", distanceStart);
    distancelimit = payload.substring(distanceStart, distanceEnd).toInt();

    Serial.println("Distance Limit: " + String(distancelimit));
    
    http.end();
  } else {
    Serial.print("Error Code: ");
    Serial.println(httpResponseCode);
    http.end();
  }

  return distancelimit;
}


String getNewHistoryId() {
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/History.json?auth=" + API_KEY;

  // Fetch current test history
  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, payload);

    // Determine the highest history ID and increment it
    int maxId = 0;
    for (JsonObject::iterator it = doc.as<JsonObject>().begin(); it != doc.as<JsonObject>().end(); ++it) {
      // Convert key to string and then to integer
      String key = it->key().c_str();
      int id = key.toInt();
      if (id > maxId) {
        maxId = id;
      }
    }
    
    // Increment the maximum ID and pad to 5 digits
    String newId = String(maxId + 1);
    newId = String("00000" + newId).substring(newId.length());

    http.end();
    return newId;
  } else {
    Serial.print("Error Code: ");
    Serial.println(httpResponseCode);
    http.end();
    return "00001"; // Default value in case of error
  }
}

// Function to get the occupant from a specific locker in Firebase
String getOccupant(const String& lockerId) {
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/Lockers/" + lockerId + ".json?auth=" + API_KEY;

  http.begin(url);
  int httpResponseCode = http.GET();

  String occupant = "";

  if (httpResponseCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);

    occupant = doc["occupant"].as<String>();
  } else {
    Serial.print("Error Code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return occupant;
}

// Function to get the current time in the desired format
String getCurrentTime() {
  time_t now = time(nullptr);  // Get the current time in seconds since the epoch
  struct tm* timeInfo = localtime(&now);  // Convert to local time format

  char timestamp[20];  // Buffer for storing formatted time string
  strftime(timestamp, sizeof(timestamp), "%m-%d-%Y %H:%M:%S", timeInfo);  // Format the time

  return String(timestamp);  // Return the formatted time as a String
}
// Function to get student details from Firebase based on RFID
String getStudentDetails(const String& rfid, String& department, String& name, String& sex) {
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/Students.json?auth=" + API_KEY;
  
  http.begin(url);
  int httpResponseCode = http.GET();

  String studentId = ""; // Store the student ID here

  if (httpResponseCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      for (JsonObject::iterator it = doc.as<JsonObject>().begin(); it != doc.as<JsonObject>().end(); ++it) {
        JsonObject student = it->value().as<JsonObject>();
        String studentRFID = student["RFID"].as<String>();

        if (studentRFID == rfid) {
          studentId = it->key().c_str(); // Get the student's ID (e.g., 22-0234)
          department = student["Department"].as<String>();
          name = student["Name"].as<String>();
          sex = student["Sex"].as<String>();
          break;
        }
      }
    } else {
      Serial.print("JSON deserialization failed: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.print("Error Code: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();
  return studentId;
}

// Function to generate history
void generateHistory(const String& lockerId, const String& receivedCommand) {
  static bool processHistory = false;

  // Check if we're already processing history
  if (processHistory) {
    Serial.println("Currently processing a command. Please wait...");
    return;  // Exit if already processing
  }

  // Set the flag to indicate that processing has started
  processHistory = true;

  String occupantRFID = getOccupant(lockerId);
  if (occupantRFID.isEmpty()) {
    Serial.println("Error: Occupant RFID not found.");
    processHistory = false;  // Reset flag
    return;
  }

  String department, name, sex;
  String studentId = getStudentDetails(occupantRFID, department, name, sex);
  if (receivedCommand == "TIME IN") {
    Serial.println(studentId);
    if (studentId.isEmpty()) {
      Serial.println("Error: Student details not found.");
      processHistory = false;  // Reset flag
      return;
    }

    String historyId = getNewHistoryId();

    HTTPClient http;
    String url = "https://" + String(FIREBASE_HOST) + "/History/" + historyId + ".json?auth=" + API_KEY;

    DynamicJsonDocument doc(1024);
    doc["Department"] = department;
    doc["LockerName"] = lockerId;
    doc["Name"] = name;
    doc["Sex"] = sex;
    doc["StudentID"] = studentId;
    doc["TimeIn"] = getCurrentTime();
    doc["TimeOut"] = "pending";

    String jsonData;
    serializeJson(doc, jsonData);

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.PUT(jsonData);

    if (httpResponseCode > 0) {
      Serial.print("History Created. HTTP Response Code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("Error Code: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  }
  else if (receivedCommand == "TIME OUT") {
    HTTPClient http;
    String url = "https://" + String(FIREBASE_HOST) + "/History.json?auth=" + API_KEY;

    Serial.print("GET URL: ");
    Serial.println(url);

    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
      String payload = http.getString();
      Serial.println("GET Payload: ");
      Serial.println(payload);

      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.print("Deserialization Error: ");
        Serial.println(error.c_str());
        processHistory = false;  // Reset flag
        return;
      }

      bool updated = false;
      for (JsonObject::iterator it = doc.as<JsonObject>().begin(); it != doc.as<JsonObject>().end(); ++it) {
        JsonObject history = it->value().as<JsonObject>();
        String studentIdInHistory = history["StudentID"].as<String>();
        String timeOut = history["TimeOut"].as<String>();

        Serial.print("Checking StudentID: ");
        Serial.print(studentIdInHistory);
        Serial.print(", TimeOut: ");
        Serial.println(timeOut);

        if (studentIdInHistory == studentId && timeOut == "pending") {
          String historyId = it->key().c_str();
          history["TimeOut"] = getCurrentTime();
          

          String updateUrl = "https://" + String(FIREBASE_HOST) + "/History/" + historyId + ".json?auth=" + API_KEY;
          String updateJsonData;
          serializeJson(history, updateJsonData);  // Serialize only the updated object

          Serial.print("Update URL: ");
          Serial.println(updateUrl);
          Serial.print("Update JSON Data: ");
          Serial.println(updateJsonData);

          http.begin(updateUrl);
          http.addHeader("Content-Type", "application/json");
          int updateResponseCode = http.PATCH(updateJsonData);

          if (updateResponseCode > 0) {
            Serial.print("History Updated. HTTP Response Code: ");
            Serial.println(updateResponseCode);
            updated = true;
          } else {
            Serial.print("Update Error Code: ");
            Serial.println(updateResponseCode);
          }

          http.end();
          break;  // Exit loop after updating the first matching record
        }
      }

      if (!updated) {
        Serial.println("No pending history found for TIME OUT.");
      }
    } else {
      Serial.print("GET Error Code: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  }

  // Reset the flags after processing the command
  processHistory = false;
  commandReceived = false;  // Ready to receive the next command
}

void handleCommand() {
  receivedCommand = server.arg("command"); // Update the global variable

  if (receivedCommand == "TIME IN") {
 // Turn on the LED
    digitalWrite(relayPin, HIGH); // Activate relay
    commandReceived = true;
    waitingForReedSwitchDeactivation = true;
    commandReceivedTime = millis();
    Serial.println("TIME IN RECEIVED");
    server.send(200, "text/plain", "Command received: TIME IN");
  } 
  else if (receivedCommand == "TIME OUT") {
 // Turn off the LED
    digitalWrite(relayPin, HIGH); // Deactivate relay
    commandReceived = true;
    waitingForReedSwitchDeactivation = true;
    commandReceivedTime = millis();
    Serial.println("TIME OUT RECEIVED");
    server.send(200, "text/plain", "Command received: TIME OUT");
  } 
  else {
    server.send(400, "text/plain", "Invalid command");
  }
}

// Function to read distance from the ultrasonic sensor
long readDistance() {
  long duration, distance;
  
  // Clear the trigger pin
  digitalWrite(triggerPin, LOW);
  delayMicroseconds(2);
  
  // Set the trigger pin high for 10 microseconds
  digitalWrite(triggerPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(triggerPin, LOW);
  
  // Read the echo pin
  duration = pulseIn(echoPin, HIGH);
  
  // Calculate the distance (in cm)
  distance = (duration / 2) / 29.1;
  
  return distance;
}
void updateLockerData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // Firebase endpoint
    String url = String("https://") + FIREBASE_HOST + lockerPath;
    http.begin(url);

    // Prepare JSON payload
    String payload = "{\"condition\":\"VACANT\",";
    payload += "\"distancelimit\":10,";
    payload += "\"connectionStatus\":\"Connected\",";
    payload += "\"ipAddress\":\"" + WiFi.localIP().toString() + "\",";
    payload += "\"lockStatus\":\"unlocked\",";
    payload += "\"occupant\":\"none\"}";

    // Send HTTP PUT request
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.PUT(payload);

    if (httpResponseCode > 0) {
      Serial.printf("HTTP Response code: %d\n", httpResponseCode);
      Serial.println("Success");
      String response = http.getString();
      Serial.println("Response: " + response);
      Serial.print(url);
    } else {
      Serial.printf("Error code: %d\n", httpResponseCode);
      Serial.println("Fail");
    }

    // End the connection
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

void clearWifiAndRestart() {
  // Disconnect from the current Wi-Fi
  WiFi.disconnect(true);
  delay(1000);

  // Clear saved Wi-Fi credentials
  wifiManager.resetSettings();

  Serial.println("Wi-Fi credentials cleared. Restarting...");

  // Restart the ESP32
  ESP.restart();
}
void setup() {
  // Initialize Serial communication
  Serial.begin(115200);
  // Initialize pin modes
  pinMode(relayPin, OUTPUT);
  pinMode(BuzzerPin, OUTPUT);
  pinMode(reedSwitchPin, INPUT_PULLUP); // Reed switch pin
  pinMode(triggerPin, OUTPUT); // Ultrasonic trigger pin
  pinMode(echoPin, INPUT);    // Ultrasonic echo pin

  // Initialize pin states
  digitalWrite(relayPin, LOW);
  digitalWrite(BuzzerPin, LOW);
   // Ensure onboard LED is off initially

  // Boot up indicatio // Turn on the onboard LED
  delay(1000); // Keep it on for 1 second
   // Turn off the onboard LED

  // Attempt to connect to saved Wi-Fi credentials
  WiFi.begin();
  int connectionTimeout = 20;  // Timeout in seconds
  int connectionAttempts = 0;

  while (WiFi.status() != WL_CONNECTED && connectionAttempts < connectionTimeout) {
    delay(1000);
    Serial.print(".");
    connectionAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to Wi-Fi");
    Serial.println("IP Address: " + WiFi.localIP().toString());

    // Update Firebase with connection status
    updateLockerData();

    // Initialize NTP (Network Time Protocol) with UTC+8 (Philippines)
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    // Wait for time to be set
    while (!time(nullptr)) {
      Serial.print(".");
      delay(1000);
    }
    Serial.println();

    // Setup HTTP server
    server.on("/command", HTTP_POST, handleCommand); // Register the handler
    server.begin();
    Serial.println("HTTP server started");

  } else {
    Serial.println("\nFailed to connect. Starting WiFiManager...");
    wifiManager.autoConnect(lockerName.c_str());
    Serial.println("Connected to Wi-Fi!");
    Serial.println("IP Address: " + WiFi.localIP().toString());

    // Update Firebase with connection status
    updateLockerData();

    // Initialize NTP (Network Time Protocol) with UTC+8 (Philippines)
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("Success");

    // Wait for time to be set
    while (!time(nullptr)) {
      Serial.print(".");
      delay(1000);
    }
    Serial.println();

    // Setup HTTP server
    server.on("/command", HTTP_POST, handleCommand); // Register the handler
    Serial.println("Success");
    server.begin();
    Serial.println("HTTP server started");
  }
  server.begin();
}

void loop() {
  server.handleClient();

  long distance = readDistance();
  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.println(" cm");

  int distancelimit = getDistanceLimit("Locker_3");

  int reedSwitchState = digitalRead(reedSwitchPin);
  if (reedSwitchState == LOW) {
    updateLockStatus("Locker_3", "locked");
  } else {
    updateLockStatus("Locker_3", "unlocked");

    if (!commandReceived && previousReedSwitchState == LOW) {
      unexpectedReedSwitchChange = true;
    }
  }

  if (unexpectedReedSwitchChange) {
    Serial.println("Unexpected reed switch deactivation detected!");
    digitalWrite(BuzzerPin, HIGH);
    delay(3000);
    digitalWrite(BuzzerPin, LOW);
    unexpectedReedSwitchChange = false;
  }

  if (commandReceived) {
    String lockerId = "Locker_3";
    String occupantRFID = getOccupant(lockerId);
    String department, name, sex;
    String studentDetails = getStudentDetails(occupantRFID, department, name, sex);
    Serial.print("COOMMAND ACCEPTED");

    delay(3000);
    digitalWrite(relayPin, LOW);

    if (waitingForReedSwitchDeactivation) {
      if (reedSwitchState == HIGH) {
        reedSwitchDeactivated = true;
        waitingForReedSwitchDeactivation = false;
      }
    }

    if (reedSwitchDeactivated && reedSwitchState == LOW) {
      if (distance < distancelimit) {
        updateCondition("Locker_3", "OCCUPIED");
      } else {
        updateCondition("Locker_3", "VACANT");
      }
      // Reset flags
      reedSwitchDeactivated = false;

      String condition = getCondition("Locker_3");
      if (condition == "VACANT") {
        generateHistory(lockerId, "TIME OUT");
        updateOccupant("Locker_3", "none");
      } else if (condition == "OCCUPIED") {
        generateHistory(lockerId, receivedCommand);
        Serial.println("The Student Timed in");
      }
    }

    previousReedSwitchState = reedSwitchState;
  } else {
    previousReedSwitchState = reedSwitchState;
  }
}