#include <SPI.h>
#include <hidboot.h>
#include <usbhub.h>
#include <SoftwareSerial.h>

// Initialize Software Serial for ESP32 communication
// Replace these pin numbers with your actual TX/RX pins
#define ESP32_RX 2  // Connect this to ESP32 TX
#define ESP32_TX 3  // Connect this to ESP32 RX
SoftwareSerial esp32Serial(ESP32_RX, ESP32_TX);  // RX, TX

// Initialize USB objects
USB Usb;
USBHub Hub(&Usb);
HIDBoot<USB_HID_PROTOCOL_KEYBOARD> HidKeyboard(&Usb);

// Buffer to store incoming RFID data
char uidBuffer[9];  // 8 characters + null terminator
int bufferIndex = 0;
bool cardPresent = false;
bool processingCard = false;

// Timing control
unsigned long lastCardCheck = 0;
const unsigned long CARD_CHECK_DELAY = 500; // Time between card presence checks

// Communication protocol constants
const char START_MARKER = '<';
const char END_MARKER = '>';

class KbdRptParser : public KeyboardReportParser {
  protected:
    void OnKeyDown(uint8_t mod, uint8_t key);
};

void KbdRptParser::OnKeyDown(uint8_t mod, uint8_t key) {
  // Only process input if we're ready for a new card
  if (!processingCard) {
    uint8_t c = OemToAscii(mod, key);
    
    if (c) {
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
        if (bufferIndex < 8) {
          uidBuffer[bufferIndex++] = c;
          uidBuffer[bufferIndex] = '\0';
          
          if (bufferIndex == 8) {
            cardPresent = true;
            processingCard = true;
          }
        }
      }
    }
  }
}

KbdRptParser Parser;

void sendToESP32(const char* uid) {
  // Send with start and end markers for reliable communication
  esp32Serial.print(START_MARKER);
  esp32Serial.print(uid);
  esp32Serial.println(END_MARKER);
  
  // Also print to main Serial for debugging
  Serial.print("Sent to ESP32: ");
  Serial.println(uid);
}

void processRFID() {
  // Only process if we have a card present
  if (!cardPresent) return;

  // Process the card
  Serial.print("Scanned UID: ");
  Serial.println(uidBuffer);

  // Send the UID to ESP32
  sendToESP32(uidBuffer);

  // Reset for next card
  bufferIndex = 0;
  cardPresent = false;
  uidBuffer[0] = '\0';
  
  // Add delay before allowing next read
  delay(1000);
  processingCard = false;
}

void setup() {
  // Initialize main Serial for debugging
  Serial.begin(115200);
  while (!Serial);
  
  // Initialize ESP32 Serial communication
  esp32Serial.begin(9600);  // Make sure this matches ESP32's baud rate
  
  Serial.println("RFID Reader Starting...");
  
  if (Usb.Init() == -1) {
    Serial.println("USB Host Shield did not initialize");
    while (1);
  }
  
  delay(200);
  Serial.println("USB Host Shield initialized");
  Serial.println("Ready to read RFID tags...");
  
  // Initialize buffers
  uidBuffer[0] = '\0';
  bufferIndex = 0;
  cardPresent = false;
  processingCard = false;
  
  HidKeyboard.SetReportParser(0, &Parser);
}

void loop() {
  Usb.Task();
  
  unsigned long currentTime = millis();
  
  // Only check for new cards after delay
  if (currentTime - lastCardCheck >= CARD_CHECK_DELAY) {
    lastCardCheck = currentTime;
    
    if (cardPresent) {
      processRFID();
    }
  }
}