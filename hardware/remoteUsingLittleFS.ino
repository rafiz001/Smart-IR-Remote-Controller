#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <LittleFS.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define IR_RECV_PIN D3
#define IR_SEND_PIN D8

#define MAX_REMOTES 10
#define MAX_RAW_LEN 150
#define REMOTES_FILE "/remotes.dat"

// IR Signal structure - RAW data only
struct IRSignal {
  uint16_t rawLen;
  uint16_t rawData[MAX_RAW_LEN];
  char name[20];
  bool valid;
};

uint8_t sw[4] = {D5, D6, D7, D0};
bool PreviousState[4] = {1, 1, 1, 1};

IRrecv irrecv(IR_RECV_PIN, 1024, 50, true);
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

// LittleFS Functions
void clearAllData() {
  Serial.println("\n=================================");
  Serial.println("CLEARING ALL DATA...");
  Serial.println("=================================");
  
  // Delete the remotes file
  if (LittleFS.exists(REMOTES_FILE)) {
    LittleFS.remove(REMOTES_FILE);
    Serial.println("✓ Remotes file deleted");
  }
  
  // Reset runtime data
  remoteCount = 0;
  selectedIndex = 0;
  scrollOffset = 0;
  
  // Clear remotes array
  for (int i = 0; i < MAX_REMOTES; i++) {
    remotes[i].valid = false;
    remotes[i].rawLen = 0;
  }
  
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
  Serial.println("║ CLEAR   - Delete all saved remotes║");
  Serial.println("║ LIST    - Show all saved remotes  ║");
  Serial.println("║ INFO    - Show storage usage       ║");
  Serial.println("║ BACKUP  - Export all remotes       ║");
  Serial.println("║ RESTORE - Import remote data       ║");
  Serial.println("║ HELP    - Show this menu           ║");
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

void showStorageInfo() {
  FSInfo fs_info;
  LittleFS.info(fs_info);
  
  int usedByRemotes = remoteCount * sizeof(IRSignal);
  float percentUsed = (fs_info.usedBytes * 100.0) / fs_info.totalBytes;
  
  Serial.println("\n=================================");
  Serial.println("STORAGE INFORMATION:");
  Serial.println("=================================");
  Serial.print("Total Size: ");
  Serial.print(fs_info.totalBytes);
  Serial.println(" bytes");
  Serial.print("Used: ");
  Serial.print(fs_info.usedBytes);
  Serial.print(" bytes (");
  Serial.print(percentUsed, 1);
  Serial.println("%)");
  Serial.print("Free: ");
  Serial.print(fs_info.totalBytes - fs_info.usedBytes);
  Serial.println(" bytes");
  Serial.print("Remotes Saved: ");
  Serial.print(remoteCount);
  Serial.print("/");
  Serial.println(MAX_REMOTES);
  Serial.print("Size per Remote: ");
  Serial.print(sizeof(IRSignal));
  Serial.println(" bytes");
  Serial.print("Remotes File Size: ");
  if (LittleFS.exists(REMOTES_FILE)) {
    File file = LittleFS.open(REMOTES_FILE, "r");
    Serial.print(file.size());
    Serial.println(" bytes");
    file.close();
  } else {
    Serial.println("0 bytes (no file)");
  }
  Serial.println("=================================\n");
}

void backupAllRemotes() {
  Serial.println("\n=================================");
  Serial.println("BACKUP DATA - COPY EVERYTHING BELOW");
  Serial.println("=================================");
  
  for (int i = 0; i < remoteCount; i++) {
    Serial.println("---BEGIN REMOTE---");
    Serial.print("NAME:");
    Serial.println(remotes[i].name);
    Serial.print("RAWLEN:");
    Serial.println(remotes[i].rawLen);
    Serial.print("RAWDATA:");
    for (int j = 0; j < remotes[i].rawLen; j++) {
      Serial.print(remotes[i].rawData[j]);
      if (j < remotes[i].rawLen - 1) {
        Serial.print(",");
      }
    }
    Serial.println();
    Serial.println("---END REMOTE---");
  }
  
  Serial.println("=================================");
  Serial.println("BACKUP COMPLETE");
  Serial.println("=================================\n");
}

void restoreRemote() {
  if (remoteCount >= MAX_REMOTES) {
    Serial.println("✗ Error: Maximum remotes reached. Delete some remotes first.");
    return;
  }
  
  Serial.println("\n=================================");
  Serial.println("RESTORE REMOTE DATA");
  Serial.println("=================================");
  Serial.println("Paste the remote data in format:");
  Serial.println("NAME:Remote 1");
  Serial.println("RAWLEN:67");
  Serial.println("RAWDATA:9000,4500,600,550,...");
  Serial.println("=================================");
  Serial.println("Waiting for input (60s timeout)...\n");
  
  String name = "";
  int rawLen = 0;
  bool hasName = false;
  bool hasRawLen = false;
  bool hasRawData = false;
  
  unsigned long startWait = millis();
  
  while (millis() - startWait < 60000) { // 60 second timeout
    if (Serial.available() > 0) {
      String line = Serial.readStringUntil('\n');
      line.trim();
      
      if (line.startsWith("NAME:")) {
        name = line.substring(5);
        name.trim();
        if (name.length() > 19) {
          name = name.substring(0, 19);
        }
        hasName = true;
        Serial.print("✓ Name: ");
        Serial.println(name);
        
      } else if (line.startsWith("RAWLEN:")) {
        rawLen = line.substring(7).toInt();
        if (rawLen > MAX_RAW_LEN) {
          Serial.println("✗ Error: Raw length exceeds maximum");
          return;
        }
        hasRawLen = true;
        Serial.print("✓ Raw Length: ");
        Serial.println(rawLen);
        
      } else if (line.startsWith("RAWDATA:")) {
        if (!hasName || !hasRawLen) {
          Serial.println("✗ Error: NAME and RAWLEN must be provided first");
          return;
        }
        
        String dataStr = line.substring(8);
        dataStr.trim();
        
        // Parse comma-separated values
        int dataIndex = 0;
        int startPos = 0;
        
        for (int i = 0; i <= dataStr.length(); i++) {
          if (i == dataStr.length() || dataStr.charAt(i) == ',') {
            if (dataIndex >= rawLen) break;
            
            String numStr = dataStr.substring(startPos, i);
            numStr.trim();
            remotes[remoteCount].rawData[dataIndex] = numStr.toInt();
            dataIndex++;
            startPos = i + 1;
          }
        }
        
        if (dataIndex < rawLen) {
          Serial.print("✗ Warning: Only received ");
          Serial.print(dataIndex);
          Serial.print(" values out of ");
          Serial.println(rawLen);
          rawLen = dataIndex; // Adjust to actual count
        }
        
        hasRawData = true;
        Serial.print("✓ Raw Data: ");
        Serial.print(dataIndex);
        Serial.println(" values parsed");
        
        // Save the remote
        strcpy(remotes[remoteCount].name, name.c_str());
        remotes[remoteCount].rawLen = rawLen;
        remotes[remoteCount].valid = true;
        remoteCount++;
        
        saveRemotesToFS();
        
        Serial.println("\n✓ Remote restored successfully!");
        Serial.print("Total remotes: ");
        Serial.println(remoteCount);
        
        if (currentMode == MENU_MODE) {
          displayMenu();
        }
        return;
      }
      
      startWait = millis(); // Reset timeout on activity
    }
    delay(10);
  }
  
  Serial.println("✗ Timeout - Operation cancelled.\n");
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
            clearAllData();
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
      showStorageInfo();
      
    } else if (command == "BACKUP") {
      backupAllRemotes();
      
    } else if (command == "RESTORE") {
      restoreRemote();
      
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
  Serial.println("║  IR REMOTE MANAGER v2.0 STARTED   ║");
  Serial.println("║       (LittleFS Version)           ║");
  Serial.println("╚════════════════════════════════════╝");
  
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(sw[i], INPUT_PULLUP);
  }
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed!");
    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("Storage Error!");
    display.display();
    while (1) {
      delay(1000);
    }
  }
  Serial.println("LittleFS mounted successfully");
  
  loadRemotesFromFS();
  
  irrecv.enableIRIn();
  
  displayMenu();
  
  printSerialMenu();
}

void loadRemotesFromFS() {
  if (!LittleFS.exists(REMOTES_FILE)) {
    Serial.println("No saved remotes file found");
    remoteCount = 0;
    return;
  }
  
  File file = LittleFS.open(REMOTES_FILE, "r");
  if (!file) {
    Serial.println("Failed to open remotes file for reading");
    remoteCount = 0;
    return;
  }
  
  // Read remote count
  file.read((uint8_t*)&remoteCount, sizeof(int));
  
  // Validate remote count
  if (remoteCount < 0 || remoteCount > MAX_REMOTES) {
    Serial.println("Invalid remote count in file, resetting");
    remoteCount = 0;
    file.close();
    return;
  }
  
  // Read each remote
  for (int i = 0; i < remoteCount; i++) {
    size_t bytesRead = file.read((uint8_t*)&remotes[i], sizeof(IRSignal));
    
    if (bytesRead != sizeof(IRSignal)) {
      Serial.print("Error reading remote ");
      Serial.println(i);
      remoteCount = i; // Truncate to valid remotes
      break;
    }
    
    // Validate loaded data
    if (remotes[i].rawLen > MAX_RAW_LEN) {
      Serial.print("Invalid rawLen for remote ");
      Serial.println(i);
      remotes[i].rawLen = MAX_RAW_LEN;
    }
  }
  
  file.close();
  
  Serial.print("Loaded ");
  Serial.print(remoteCount);
  Serial.println(" remotes from LittleFS");
}

void saveRemotesToFS() {
  File file = LittleFS.open(REMOTES_FILE, "w");
  if (!file) {
    Serial.println("Failed to open remotes file for writing");
    return;
  }
  
  // Write remote count
  file.write((uint8_t*)&remoteCount, sizeof(int));
  
  // Write each remote
  for (int i = 0; i < remoteCount; i++) {
    size_t bytesWritten = file.write((uint8_t*)&remotes[i], sizeof(IRSignal));
    
    if (bytesWritten != sizeof(IRSignal)) {
      Serial.print("Error writing remote ");
      Serial.println(i);
      break;
    }
  }
  
  file.close();
  
  Serial.print("Saved ");
  Serial.print(remoteCount);
  Serial.println(" remotes to LittleFS");
  Serial.print("File size: ");
  
  File checkFile = LittleFS.open(REMOTES_FILE, "r");
  Serial.print(checkFile.size());
  Serial.println(" bytes");
  checkFile.close();
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
        saveRemotesToFS();
        
        signalReceived = true;
        displayReceivedSignal();
        
        Serial.println("IR Signal captured (RAW mode)");
        Serial.print("Raw length: ");
        Serial.println(remotes[remoteCount-1].rawLen);
        Serial.print("Raw data: ");
        for(int i = 0; i < remotes[remoteCount-1].rawLen && i < 150; i++) {
          Serial.print(remotes[remoteCount-1].rawData[i]);
          Serial.print(" ");
        }
        Serial.println();
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
  
  Serial.print("Sent IR signal: ");
  Serial.println(signal->name);
  Serial.print("Raw length: ");
  Serial.println(signal->rawLen);
  Serial.print("Raw data: ");
  for(int i = 0; i < signal->rawLen && i < 150; i++) {
    Serial.print(signal->rawData[i]);
    Serial.print(" ");
  }
  Serial.println();
  
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
