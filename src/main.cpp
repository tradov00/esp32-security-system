
#include <Arduino.h>
#include <ctype.h>
#include <string.h>

// ==========================================
// CONFIGURATION
// ==========================================

constexpr unsigned long UPTIME_INTERVAL_MS = 1000;

constexpr size_t COMMAND_BUFFER_SIZE = 32;
constexpr size_t EVENT_QUEUE_SIZE = 8;
constexpr size_t LOG_SIZE = 10;

constexpr int STATUS_PIN = 2;


// ==========================================
// SYSTEM STATE
// ==========================================

enum SystemState {
    DISARMED,
    ARMED
};

SystemState currentState = DISARMED;


// ==========================================
// EVENT DEFINITIONS
// ==========================================

enum EventType {
    EVENT_NONE,
    EVENT_ARM,
    EVENT_DISARM,
    EVENT_STATUS_REQUEST,
    EVENT_LOG_REQUEST,
    EVENT_HELP_REQUEST
};

struct Event {
    EventType type;
};


// ==========================================
// CIRCULAR EVENT QUEUE
// ==========================================

Event eventQueue[EVENT_QUEUE_SIZE];

size_t queueHead = 0;
size_t queueTail = 0;
size_t queueCount = 0;


bool enqueueEvent(EventType eventType) {

    if (queueCount >= EVENT_QUEUE_SIZE) {
        return false;
    }

    eventQueue[queueTail].type = eventType;

    queueTail = (queueTail + 1) % EVENT_QUEUE_SIZE;

    queueCount++;

    return true;
}


bool dequeueEvent(Event &event) {

    if (queueCount == 0) {
        return false;
    }

    event = eventQueue[queueHead];

    queueHead = (queueHead + 1) % EVENT_QUEUE_SIZE;

    queueCount--;

    return true;
}


// ==========================================
// EVENT HISTORY
// ==========================================

struct LogEntry {
    unsigned long timestamp;
    SystemState state;
};

LogEntry eventLog[LOG_SIZE];

size_t logNextIndex = 0;
size_t logCount = 0;


// ==========================================
// UART COMMAND BUFFER
// ==========================================

char commandBuffer[COMMAND_BUFFER_SIZE];

size_t commandIndex = 0;

bool commandOverflow = false;


// ==========================================
// FUNCTION DECLARATIONS
// ==========================================

void readSerialCommands();

void handleCompletedCommand();

void processCommand(const char* command);

void processEvents();

void handleEvent(const Event &event);

void printStatus();

void printHelp();

void printTimestamp(unsigned long totalSeconds);

void addLogEntry(SystemState state);

void printEventHistory();


// ==========================================
// SETUP
// ==========================================

void setup() {

    Serial.begin(115200);

    pinMode(STATUS_PIN, OUTPUT);

    digitalWrite(STATUS_PIN, LOW);

    Serial.println();

    Serial.println("==============================");
    Serial.println("   ESP32 SECURITY SYSTEM");
    Serial.println("==============================");

    Serial.println("System initialized!");

    printHelp();

    printStatus();
}


// ==========================================
// MAIN LOOP
// ==========================================

void loop() {

    // Non-blocking uptime tracking
    static unsigned long previousMillis = 0;

    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= UPTIME_INTERVAL_MS) {

        previousMillis = currentMillis;

        Serial.print("System uptime: ");
        Serial.print(currentMillis / 1000);
        Serial.println(" seconds");
    }

    // Read incoming UART commands
    readSerialCommands();

    // Process pending events
    processEvents();
}


// ==========================================
// UART INPUT PROCESSING
// ==========================================

void readSerialCommands() {

    while (Serial.available() > 0) {

        char receivedChar = Serial.read();

        // Ignore carriage return
        if (receivedChar == '\r') {
            continue;
        }

        // Newline indicates end of command
        if (receivedChar == '\n') {

            handleCompletedCommand();

            continue;
        }

        // Store character in fixed buffer
        if (!commandOverflow) {

            if (commandIndex < COMMAND_BUFFER_SIZE - 1) {

                commandBuffer[commandIndex] = receivedChar;

                commandIndex++;

            } else {

                commandOverflow = true;
            }
        }
    }
}


// ==========================================
// HANDLE COMPLETED UART COMMAND
// ==========================================

void handleCompletedCommand() {

    if (commandOverflow) {

        Serial.println("Error: Command too long!");

    } else {

        // Terminate C string
        commandBuffer[commandIndex] = '\0';

        // Convert command to uppercase
        for (size_t i = 0; i < commandIndex; i++) {

            commandBuffer[i] =
                toupper((unsigned char)commandBuffer[i]);
        }

        // Process non-empty command
        if (commandIndex > 0) {

            processCommand(commandBuffer);
        }
    }

    // Reset command buffer
    commandIndex = 0;

    commandOverflow = false;

    commandBuffer[0] = '\0';
}


// ==========================================
// CONVERT COMMAND INTO EVENT
// ==========================================

void processCommand(const char* command) {

    EventType eventType = EVENT_NONE;

    if (strcmp(command, "ARM") == 0) {

        eventType = EVENT_ARM;

    } else if (strcmp(command, "DISARM") == 0) {

        eventType = EVENT_DISARM;

    } else if (strcmp(command, "STATUS") == 0) {

        eventType = EVENT_STATUS_REQUEST;

    } else if (strcmp(command, "LOG") == 0) {

        eventType = EVENT_LOG_REQUEST;

    } else if (strcmp(command, "HELP") == 0) {

        eventType = EVENT_HELP_REQUEST;

    } else {

        Serial.println("Unknown command! Type HELP for available commands.");

        return;
    }

    // Add event to queue
    if (!enqueueEvent(eventType)) {

        Serial.println("Error: Event queue is full!");
    }
}


// ==========================================
// PROCESS PENDING EVENTS
// ==========================================

void processEvents() {

    Event currentEvent;

    while (dequeueEvent(currentEvent)) {

        handleEvent(currentEvent);
    }
}


// ==========================================
// HANDLE INDIVIDUAL EVENT
// ==========================================

void handleEvent(const Event &event) {

    switch (event.type) {

        case EVENT_ARM:

            // Check if system is already armed
            if (currentState == ARMED) {

                Serial.println("System already ARMED!");

                break;
            }

            currentState = ARMED;

            digitalWrite(STATUS_PIN, HIGH);

            addLogEntry(ARMED);

            printTimestamp(millis() / 1000);

            Serial.println("SYSTEM ARMED");

            break;


        case EVENT_DISARM:

            // Check if system is already disarmed
            if (currentState == DISARMED) {

                Serial.println("System already DISARMED!");

                break;
            }

            currentState = DISARMED;

            digitalWrite(STATUS_PIN, LOW);

            addLogEntry(DISARMED);

            printTimestamp(millis() / 1000);

            Serial.println("SYSTEM DISARMED");

            break;


        case EVENT_STATUS_REQUEST:

            printStatus();

            break;


        case EVENT_LOG_REQUEST:

            printEventHistory();

            break;


        case EVENT_HELP_REQUEST:

            printHelp();

            break;


        default:

            Serial.println("Unknown event!");

            break;
    }
}


// ==========================================
// PRINT HELP
// ==========================================

void printHelp() {

    Serial.println();
    Serial.println("========== AVAILABLE COMMANDS ==========");

    Serial.println("ARM     - Activate the security system");

    Serial.println("DISARM  - Deactivate the security system");

    Serial.println("STATUS  - Display current system status");

    Serial.println("LOG     - Display event history");

    Serial.println("HELP    - Display available commands");

    Serial.println("========================================");
    Serial.println();
}


// ==========================================
// ADD EVENT TO HISTORY
// ==========================================

void addLogEntry(SystemState state) {

    eventLog[logNextIndex].timestamp = millis() / 1000;

    eventLog[logNextIndex].state = state;

    // Move to next position
    logNextIndex = (logNextIndex + 1) % LOG_SIZE;

    // Increase count until buffer is full
    if (logCount < LOG_SIZE) {

        logCount++;
    }
}


// ==========================================
// PRINT EVENT HISTORY
// ==========================================

void printEventHistory() {

    Serial.println();
    Serial.println("========== EVENT HISTORY ==========");

    if (logCount == 0) {

        Serial.println("No events recorded yet!");

    } else {

        // Find oldest entry
        size_t oldestIndex =
            (logNextIndex + LOG_SIZE - logCount) % LOG_SIZE;

        // Print entries in chronological order
        for (size_t i = 0; i < logCount; i++) {

            size_t index = (oldestIndex + i) % LOG_SIZE;

            printTimestamp(eventLog[index].timestamp);

            if (eventLog[index].state == ARMED) {

                Serial.println("SYSTEM ARMED");

            } else {

                Serial.println("SYSTEM DISARMED");
            }
        }
    }

    Serial.println("===================================");
    Serial.println();
}


// ==========================================
// PRINT TIMESTAMP
// ==========================================

void printTimestamp(unsigned long totalSeconds) {

    unsigned long hours = totalSeconds / 3600;

    unsigned int minutes = (totalSeconds / 60) % 60;

    unsigned int seconds = totalSeconds % 60;

    Serial.print("[");

    if (hours < 10) {
        Serial.print("0");
    }

    Serial.print(hours);
    Serial.print(":");

    if (minutes < 10) {
        Serial.print("0");
    }

    Serial.print(minutes);
    Serial.print(":");

    if (seconds < 10) {
        Serial.print("0");
    }

    Serial.print(seconds);

    Serial.print("] ");
}


// ==========================================
// DISPLAY CURRENT SYSTEM STATUS
// ==========================================

void printStatus() {

    Serial.print("Current system state: ");

    if (currentState == ARMED) {

        Serial.println("ARMED");

    } else {

        Serial.println("DISARMED");
    }

    Serial.print("GPIO2 output: ");

    if (currentState == ARMED) {

        Serial.println("HIGH");

    } else {

        Serial.println("LOW");
    }
}
