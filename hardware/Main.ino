#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <EEPROM.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define IR_RECV_PIN D3
#define IR_SEND_PIN D8

#define MAX_REMOTES 10  // Reduced from 20
#define MAX_RAW_LEN 150 // Reduced from 200
#define EEPROM_SIZE 4096 // Increased from 2048

// IR Signal structure - RAW data only
struct IRSignal {
  uint16_t rawLen;
  uint16_t rawData[MAX_RAW_LEN];
  char name[20];
  bool valid;
};

uint8_t sw[4] = {D5, D6, D7, D0};
bool PreviousState[4] = {1, 1, 1, 1};

IRrecv irrecv(IR_RECV_PIN,1024, 50, true);
IRsend irsend(IR_SEND_PIN);
decode_results results;

IRSignal remotes[MAX_REMOTES];
int remoteCount = 0;
int selectedIndex = 0;
int scrollOffset = 0;
int visibleItems = 4;

enum Mode {
  MENU_MODE,
  RECEIVING_MODE,
  SENDING_MODE
};

Mode currentMode = MENU_MODE;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void clearAllEEPROM() {
  Serial.println("\n=================================");
  Serial.println("CLEARING ALL EEPROM DATA...");
  Serial.println("=================================");
  
  // Clear all EEPROM
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  
  // Reset runtime data
  remoteCount = 0;
  selectedIndex = 0;
  scrollOffset = 0;
  
  // Clear remotes array
  for (int i = 0; i < MAX_REMOTES; i++) {
    remotes[i].valid = false;
    remotes[i].rawLen = 0;
  }
  
  Serial.println("✓ EEPROM cleared successfully!");
  Serial.println("✓ All saved remotes deleted");
  Serial.println("=================================\n");
  
  // Update display
  if (currentMode == MENU_MODE) {
    displayMenu();
  }
}

void printSerialMenu() {
  Serial.println("\n╔════════════════════════════════════╗");
  Serial.println("║   IR REMOTE MANAGER - COMMANDS     ║");
  Serial.println("╠════════════════════════════════════╣");
  Serial.println("║ CLEAR  - Delete all saved remotes ║");
  Serial.println("║ LIST   - Show all saved remotes   ║");
  Serial.println("║ INFO   - Show EEPROM usage         ║");
  Serial.println("║ HELP   - Show this menu            ║");
  Serial.println("╚════════════════════════════════════╝\n");
}

void listAllRemotes() {
  Serial.println("\n=================================");
  Serial.println("SAVED REMOTES:");
  Serial.println("=================================");
  
  if (remoteCount == 0) {
    Serial.println("No remotes saved.");
  } else {
    for (int i = 0; i < remoteCount; i++) {
      Serial.print(i + 1);
      Serial.print(". ");
      Serial.print(remotes[i].name);
      Serial.print(" - Raw Length: ");
      Serial.println(remotes[i].rawLen);
    }
  }
  
  Serial.println("=================================\n");
}

void showEEPROMInfo() {
  int usedSize = sizeof(int) + (remoteCount * sizeof(IRSignal));
  int freeSize = EEPROM_SIZE - usedSize;
  float percentUsed = (usedSize * 100.0) / EEPROM_SIZE;
  
  Serial.println("\n=================================");
  Serial.println("EEPROM INFORMATION:");
  Serial.println("=================================");
  Serial.print("Total Size: ");
  Serial.print(EEPROM_SIZE);
  Serial.println(" bytes");
  Serial.print("Used: ");
  Serial.print(usedSize);
  Serial.print(" bytes (");
  Serial.print(percentUsed, 1);
  Serial.println("%)");
  Serial.print("Free: ");
  Serial.print(freeSize);
  Serial.println(" bytes");
  Serial.print("Remotes Saved: ");
  Serial.print(remoteCount);
  Serial.print("/");
  Serial.println(MAX_REMOTES);
  Serial.print("Size per Remote: ");
  Serial.print(sizeof(IRSignal));
  Serial.println(" bytes");
  Serial.println("=================================\n");
}

void handleSerialCommand() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toUpperCase();
    
    if (command == "CLEAR") {
      Serial.println("\n⚠ WARNING: This will delete ALL saved remotes!");
      Serial.println("Type 'YES' to confirm or anything else to cancel:");
      
      // Wait for confirmation
      unsigned long startWait = millis();
      while (millis() - startWait < 10000) { // 10 second timeout
        if (Serial.available() > 0) {
          String confirm = Serial.readStringUntil('\n');
          confirm.trim();
          confirm.toUpperCase();
          
          if (confirm == "YES") {
            clearAllEEPROM();
          } else {
            Serial.println("✗ Operation cancelled.\n");
          }
          return;
        }
      }
      Serial.println("✗ Timeout - Operation cancelled.\n");
      
    } else if (command == "LIST") {
      listAllRemotes();
      
    } else if (command == "INFO") {
      showEEPROMInfo();
      
    } else if (command == "HELP") {
      printSerialMenu();
      
    } else if (command.length() > 0) {
      Serial.print("Unknown command: ");
      Serial.println(command);
      Serial.println("Type 'HELP' for available commands.\n");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════╗");
  Serial.println("║  IR REMOTE MANAGER v1.0 STARTED   ║");
  Serial.println("╚════════════════════════════════════╝");
  
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(sw[i], INPUT_PULLUP);
  }
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadRemotesFromEEPROM();
  
  irrecv.enableIRIn();
  
  displayMenu();
  
  printSerialMenu();
}

void loadRemotesFromEEPROM() {
  int addr = 0;
  
  // Read remote count
  EEPROM.get(addr, remoteCount);
  addr += sizeof(int);
  
  // Validate remote count
  if (remoteCount < 0 || remoteCount > MAX_REMOTES) {
    Serial.println("Invalid remote count, resetting EEPROM");
    remoteCount = 0;
    return;
  }
  
  // Check if we have enough EEPROM space
  int requiredSize = sizeof(int) + (remoteCount * sizeof(IRSignal));
  if (requiredSize > EEPROM_SIZE) {
    Serial.println("EEPROM overflow detected, resetting");
    remoteCount = 0;
    return;
  }
  
  // Read each remote
  for (int i = 0; i < remoteCount; i++) {
    EEPROM.get(addr, remotes[i]);
    addr += sizeof(IRSignal);
    
    // Validate loaded data
    if (remotes[i].rawLen > MAX_RAW_LEN) {
      Serial.print("Invalid rawLen for remote ");
      Serial.println(i);
      remotes[i].rawLen = MAX_RAW_LEN;
    }
  }
  
  Serial.print("Loaded ");
  Serial.print(remoteCount);
  Serial.println(" remotes from EEPROM");
  Serial.print("EEPROM used: ");
  Serial.print(requiredSize);
  Serial.print("/");
  Serial.println(EEPROM_SIZE);
}

void saveRemotesToEEPROM() {
  int addr = 0;
  
  // Calculate required size
  int requiredSize = sizeof(int) + (remoteCount * sizeof(IRSignal));
  if (requiredSize > EEPROM_SIZE) {
    Serial.println("ERROR: Not enough EEPROM space!");
    return;
  }
  
  // Write remote count
  EEPROM.put(addr, remoteCount);
  addr += sizeof(int);
  
  // Write each remote
  for (int i = 0; i < remoteCount; i++) {
    EEPROM.put(addr, remotes[i]);
    addr += sizeof(IRSignal);
  }
  
  EEPROM.commit();
  
  Serial.print("Saved ");
  Serial.print(remoteCount);
  Serial.println(" remotes to EEPROM");
  Serial.print("EEPROM used: ");
  Serial.print(requiredSize);
  Serial.print("/");
  Serial.println(EEPROM_SIZE);
}

void displayMenu() {
  display.clearDisplay();
  
  int itemHeight = 16;
  int totalItems = remoteCount + 1;
  
  if (selectedIndex < scrollOffset) {
    scrollOffset = selectedIndex;
  } else if (selectedIndex >= scrollOffset + visibleItems) {
    scrollOffset = selectedIndex - visibleItems + 1;
  }
  
  for (int i = 0; i < visibleItems; i++) {
    int itemIndex = i + scrollOffset;
    if (itemIndex >= totalItems) break;
    
    int yPos = i * itemHeight;
    
    if (itemIndex == selectedIndex) {
      display.fillRect(0, yPos, SCREEN_WIDTH, itemHeight, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    
    int textY = yPos + (itemHeight - 8) / 2;
    display.setCursor(6, textY);
    
    if (itemIndex < remoteCount) {
      display.println(remotes[itemIndex].name);
    } else {
      display.println("+ Add Remote");
    }
    
    if (itemIndex == selectedIndex) {
      display.drawRect(0, yPos, SCREEN_WIDTH, itemHeight, SSD1306_WHITE);
    }
  }
  
  drawScrollIndicators(totalItems);
  display.display();
}

void drawScrollIndicators(int totalItems) {
  if (scrollOffset > 0) {
    display.fillTriangle(SCREEN_WIDTH - 8, 2, 
                        SCREEN_WIDTH - 2, 2, 
                        SCREEN_WIDTH - 5, 6, SSD1306_WHITE);
  }
  
  if (scrollOffset + visibleItems < totalItems) {
    display.fillTriangle(SCREEN_WIDTH - 8, SCREEN_HEIGHT - 2, 
                        SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, 
                        SCREEN_WIDTH - 5, SCREEN_HEIGHT - 6, SSD1306_WHITE);
  }
}

void displayReceivingMode() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  
  display.setCursor(10, 10);
  display.println("IR RECEIVING MODE");
  
  display.setCursor(10, 30);
  display.println("Point remote at");
  display.setCursor(10, 40);
  display.println("sensor and press");
  display.setCursor(10, 50);
  display.println("any button...");
  
  display.display();
}

void displayReceivedSignal() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  
  display.setCursor(10, 10);
  display.println("Signal Received!");
  
  display.setCursor(10, 30);
  display.println("Saved as:");
  display.setCursor(10, 40);
  display.println(remotes[remoteCount - 1].name);
  
  display.display();
  delay(2000);
}

void displaySendingMode() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  
  display.setCursor(10, 20);
  display.println("Sending IR signal");
  display.setCursor(10, 40);
  display.println(remotes[selectedIndex].name);
  
  display.display();
}

void enterReceivingMode() {
  currentMode = RECEIVING_MODE;
  irrecv.enableIRIn();
  displayReceivingMode();
  
  unsigned long startTime = millis();
  bool signalReceived = false;
  
  while (millis() - startTime < 30000 && !signalReceived) {
    if (irrecv.decode(&results)) {
      if (remoteCount < MAX_REMOTES) {
        // Store ONLY raw data
        remotes[remoteCount].valid = true;
        
        // Store raw data properly - limit to MAX_RAW_LEN
        remotes[remoteCount].rawLen = min(results.rawlen, (uint16_t)MAX_RAW_LEN);
        
        // Copy raw timing data
        for (uint16_t i = 0; i < remotes[remoteCount].rawLen; i++) {
          remotes[remoteCount].rawData[i] = results.rawbuf[i+1] * kRawTick;
        }
        
        // Generate name
        sprintf(remotes[remoteCount].name, "Remote %d", remoteCount + 1);
        
        remoteCount++;
        saveRemotesToEEPROM();
        
        signalReceived = true;
        displayReceivedSignal();
        
        Serial.println("IR Signal captured (RAW mode)");
        Serial.print("Raw length: ");
        Serial.println(remotes[remoteCount-1].rawLen);
        for(int i=0;i<150;i++)
        {Serial.print(remotes[remoteCount-1].rawData[i]);
        Serial.print(" ");}
      } else {
        display.clearDisplay();
        display.setCursor(10, 25);
        display.println("Memory Full!");
        display.display();
        delay(2000);
        signalReceived = true;
      }
      
      irrecv.resume();
    }
    
    delay(100);
  }
  
  if (!signalReceived) {
    display.clearDisplay();
    display.setCursor(10, 25);
    display.println("Timeout - No signal");
    display.display();
    delay(2000);
  }
  
  currentMode = MENU_MODE;
  selectedIndex = 0;
  scrollOffset = 0;
  displayMenu();
}

void sendIRSignal(int index) {
  if (index < 0 || index >= remoteCount) return;
  
  currentMode = SENDING_MODE;
  displaySendingMode();
  
  irrecv.disableIRIn();
  irsend.begin();
  IRSignal* signal = &remotes[index];
  
  // Always send using raw data
  irsend.sendRaw(signal->rawData, signal->rawLen, 38);
  for(int i=0; i<=150; i++)
  {Serial.print(signal->rawData[i]);
  Serial.print(" ");}

  Serial.print("Sent IR signal: ");
  Serial.println(signal->name);
  Serial.print("Raw length: ");
  Serial.println(signal->rawLen);
  
  delay(1000);
  
  irrecv.enableIRIn();
  currentMode = MENU_MODE;
  displayMenu();
}

void upButtonPressed() {
  int totalItems = remoteCount + 1;
  selectedIndex--;
  if (selectedIndex < 0) selectedIndex = totalItems - 1;
  displayMenu();
}

void downButtonPressed() {
  int totalItems = remoteCount + 1;
  selectedIndex++;
  if (selectedIndex >= totalItems) selectedIndex = 0;
  displayMenu();
}

void selectButtonPressed() {
  if (selectedIndex < remoteCount) {
    sendIRSignal(selectedIndex);
  }
}

void addButtonPressed() {
  int totalItems = remoteCount + 1;
  if (selectedIndex == totalItems - 1) {
    enterReceivingMode();
  }
}

void loop() {
  // Handle serial commands
  handleSerialCommand();
  
  if (currentMode == MENU_MODE) {
    for (uint8_t i = 0; i < 4; i++) {
      if (digitalRead(sw[i]) != PreviousState[i] && digitalRead(sw[i]) == 0) {
        PreviousState[i] = 0;
        
        switch(i) {
          case 0:
            upButtonPressed();
            break;
          case 1:
            downButtonPressed();
            break;
          case 2:
            selectButtonPressed();
            break;
          case 3:
            addButtonPressed();
            break;
        }
      } else if (digitalRead(sw[i]) != PreviousState[i] && digitalRead(sw[i]) == 1) {
        PreviousState[i] = 1;
      }
    }
  }
  
  delay(100);
}
