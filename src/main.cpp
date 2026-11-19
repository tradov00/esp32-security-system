
#include <Arduino.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>

// =====================================================
// CONFIGURATION
// =====================================================

#define STATUS_PIN 2

const char CORRECT_PIN[] = "1234";

const unsigned long UPTIME_INTERVAL = 1000;
const unsigned long LOCKOUT_DURATION = 30000;
const unsigned long ALARM_DURATION = 10000;

const uint8_t MAX_PIN_ATTEMPTS = 3;

const uint8_t EVENT_QUEUE_SIZE = 8;
const uint8_t HISTORY_SIZE = 10;

const uint8_t INPUT_BUFFER_SIZE = 32;

// =====================================================
// FINITE STATE MACHINE
// =====================================================

enum SystemState {
    DISARMED,
    ARMED,
    ALARM_TRIGGERED
};

SystemState currentState = DISARMED;

// =====================================================
// ALARM TIMER
// =====================================================

unsigned long alarmStartTime = 0;

// =====================================================
// EVENT SYSTEM
// =====================================================

struct SystemEvent {
    SystemState state;
    unsigned long timestamp;
};

// Circular event queue
SystemEvent eventQueue[EVENT_QUEUE_SIZE];

uint8_t queueHead = 0;
uint8_t queueTail = 0;
uint8_t queueCount = 0;

// Event history
SystemEvent eventHistory[HISTORY_SIZE];

uint8_t historyIndex = 0;
uint8_t historyCount = 0;

// =====================================================
// SERIAL INPUT
// =====================================================

char inputBuffer[INPUT_BUFFER_SIZE];
uint8_t inputIndex = 0;

bool inputOverflow = false;

// =====================================================
// PIN AUTHENTICATION
// =====================================================

uint8_t failedPinAttempts = 0;

bool systemLocked = false;
unsigned long lockoutStartTime = 0;

// =====================================================
// TIMING
// =====================================================

unsigned long lastUptimePrint = 0;

// =====================================================
// FUNCTION DECLARATIONS
// =====================================================

void processSerialInput();
void processCommand(char* commandLine);

void printHelp();
void printStatus();
void printEventHistory();
void printUptime();

bool authenticatePIN(const char* enteredPIN);
bool isValidPINFormat(const char* pin);
bool isLockoutActive();

bool transitionTo(SystemState newState);
void updateGPIO();

void processAlarmTimer();

bool enqueueEvent(SystemState state);
bool dequeueEvent(SystemEvent& event);

void processPendingEvents();
void addToHistory(const SystemEvent& event);

void printStateName(SystemState state);
void printTimestamp(unsigned long timestamp);

char* getNextToken(char*& cursor);

// =====================================================
// SETUP
// =====================================================

void setup() {

    Serial.begin(115200);

    pinMode(STATUS_PIN, OUTPUT);

    currentState = DISARMED;

    updateGPIO();

    lastUptimePrint = millis();

    Serial.println();
    Serial.println("==============================");
    Serial.println("   ESP32 SECURITY SYSTEM");
    Serial.println("==============================");

    Serial.println("System initialized!");

    printHelp();

    Serial.println("Current system state: DISARMED");
    Serial.println("GPIO2 output: LOW");
    Serial.println();
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

    processSerialInput();

    processAlarmTimer();

    processPendingEvents();

    unsigned long currentTime = millis();

    if (currentTime - lastUptimePrint >= UPTIME_INTERVAL) {

        lastUptimePrint = currentTime;

        printUptime();
    }
}

// =====================================================
// SERIAL INPUT
// =====================================================

void processSerialInput() {

    while (Serial.available() > 0) {

        char incomingChar = Serial.read();

        if (incomingChar == '\r') {
            continue;
        }

        if (incomingChar == '\n') {

            if (inputOverflow) {

                Serial.println("Error: command is too long!");

            } else if (inputIndex > 0) {

                inputBuffer[inputIndex] = '\0';

                processCommand(inputBuffer);
            }

            inputIndex = 0;
            inputOverflow = false;

            continue;
        }

        // Backspace support
        if (incomingChar == '\b' || incomingChar == 127) {

            if (inputIndex > 0) {
                inputIndex--;
            }

            continue;
        }

        // Printable ASCII characters
        if (incomingChar >= 32 && incomingChar <= 126) {

            if (inputIndex < INPUT_BUFFER_SIZE - 1) {

                inputBuffer[inputIndex++] = incomingChar;

            } else {

                inputOverflow = true;
            }
        }
    }
}

// =====================================================
// COMMAND PROCESSING
// =====================================================

void processCommand(char* commandLine) {

    char* cursor = commandLine;

    char* command = getNextToken(cursor);
    char* argument = getNextToken(cursor);
    char* extraArgument = getNextToken(cursor);

    if (command == nullptr) {
        return;
    }

    // Convert command to uppercase
    for (uint8_t i = 0; command[i] != '\0'; i++) {
        command[i] = toupper((unsigned char)command[i]);
    }

    // HELP
    if (strcmp(command, "HELP") == 0) {

        if (argument != nullptr) {
            Serial.println("Usage: HELP");
            return;
        }

        printHelp();
        return;
    }

    // STATUS
    if (strcmp(command, "STATUS") == 0) {

        if (argument != nullptr) {
            Serial.println("Usage: STATUS");
            return;
        }

        printStatus();
        return;
    }

    // LOG
    if (strcmp(command, "LOG") == 0) {

        if (argument != nullptr) {
            Serial.println("Usage: LOG");
            return;
        }

        printEventHistory();
        return;
    }

    // TRIGGER
    if (strcmp(command, "TRIGGER") == 0) {

        if (argument != nullptr) {
            Serial.println("Usage: TRIGGER");
            return;
        }

        if (currentState == DISARMED) {

            Serial.println(
                "Cannot trigger alarm: system is DISARMED!"
            );

            return;
        }

        if (currentState == ALARM_TRIGGERED) {

            Serial.println("Alarm is already triggered!");

            return;
        }

        Serial.println("Simulated intrusion detected!");

        transitionTo(ALARM_TRIGGERED);

        return;
    }

    // ARM / DISARM
    if (strcmp(command, "ARM") == 0 ||
        strcmp(command, "DISARM") == 0) {

        if (argument == nullptr || extraArgument != nullptr) {

            Serial.println("Authentication required!");
            Serial.println("Usage: ARM <PIN> or DISARM <PIN>");

            return;
        }

        // Check lockout
        if (isLockoutActive()) {

            unsigned long elapsed = millis() - lockoutStartTime;

            unsigned long remaining =
                (LOCKOUT_DURATION - elapsed + 999) / 1000;

            Serial.print("System locked. Try again in ");
            Serial.print(remaining);
            Serial.println(" seconds.");

            return;
        }

        // Authenticate
        if (!authenticatePIN(argument)) {
            return;
        }

        if (strcmp(command, "ARM") == 0) {

            transitionTo(ARMED);

        } else {

            transitionTo(DISARMED);
        }

        return;
    }

    // Unknown command
    Serial.println("Unknown command! Type HELP for available commands.");
}

// =====================================================
// TOKEN PARSER
// =====================================================

char* getNextToken(char*& cursor) {

    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor == '\0') {
        return nullptr;
    }

    char* token = cursor;

    while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
        cursor++;
    }

    if (*cursor != '\0') {
        *cursor = '\0';
        cursor++;
    }

    return token;
}

// =====================================================
// PIN AUTHENTICATION
// =====================================================

bool authenticatePIN(const char* enteredPIN) {

    if (!isValidPINFormat(enteredPIN)) {

        Serial.println(
            "Invalid PIN format. PIN must contain exactly 4 digits."
        );

        return false;
    }

    if (strcmp(enteredPIN, CORRECT_PIN) == 0) {

        failedPinAttempts = 0;

        Serial.println("PIN accepted.");

        return true;
    }

    failedPinAttempts++;

    Serial.println("Incorrect PIN!");

    if (failedPinAttempts >= MAX_PIN_ATTEMPTS) {

        systemLocked = true;

        lockoutStartTime = millis();

        failedPinAttempts = 0;

        Serial.println("Too many incorrect PIN attempts!");
        Serial.println("System locked for 30 seconds.");

    } else {

        Serial.print("Attempts remaining: ");
        Serial.println(MAX_PIN_ATTEMPTS - failedPinAttempts);
    }

    return false;
}

// =====================================================
// PIN FORMAT VALIDATION
// =====================================================

bool isValidPINFormat(const char* pin) {

    if (strlen(pin) != 4) {
        return false;
    }

    for (uint8_t i = 0; i < 4; i++) {

        if (!isdigit((unsigned char)pin[i])) {
            return false;
        }
    }

    return true;
}

// =====================================================
// LOCKOUT MANAGEMENT
// =====================================================

bool isLockoutActive() {

    if (!systemLocked) {
        return false;
    }

    unsigned long elapsed = millis() - lockoutStartTime;

    if (elapsed >= LOCKOUT_DURATION) {

        systemLocked = false;
        failedPinAttempts = 0;

        Serial.println(
            "PIN lockout expired. Commands enabled again."
        );

        return false;
    }

    return true;
}

// =====================================================
// FINITE STATE MACHINE
// =====================================================

bool transitionTo(SystemState newState) {

    // Prevent unnecessary transitions
    if (newState == currentState) {

        Serial.print("System already ");

        printStateName(currentState);

        Serial.println("!");

        return false;
    }

    // Validate allowed transitions
    bool transitionAllowed = false;

    switch (currentState) {

        case DISARMED:

            if (newState == ARMED) {
                transitionAllowed = true;
            }

            break;

        case ARMED:

            if (newState == DISARMED ||
                newState == ALARM_TRIGGERED) {

                transitionAllowed = true;
            }

            break;

        case ALARM_TRIGGERED:

            if (newState == DISARMED ||
                newState == ARMED) {

                transitionAllowed = true;
            }

            break;
    }

    if (!transitionAllowed) {

        Serial.println("Error: invalid state transition!");

        return false;
    }

    // Update state
    currentState = newState;

    // Start alarm timer when entering ALARM_TRIGGERED
    if (newState == ALARM_TRIGGERED) {

        alarmStartTime = millis();

        Serial.println("Alarm activated!");
        Serial.println("Alarm will reset automatically after 10 seconds.");
    }

    // Update hardware output
    updateGPIO();

    // Record state change
    enqueueEvent(currentState);

    return true;
}

// =====================================================
// ALARM TIMER
// =====================================================

void processAlarmTimer() {

    // Timer only runs while alarm is triggered
    if (currentState != ALARM_TRIGGERED) {
        return;
    }

    unsigned long elapsed = millis() - alarmStartTime;

    if (elapsed >= ALARM_DURATION) {

        Serial.println();
        Serial.println("Alarm duration expired!");
        Serial.println("Automatically returning to ARMED state.");

        transitionTo(ARMED);
    }
}

// =====================================================
// GPIO CONTROL
// =====================================================

void updateGPIO() {

    if (currentState == DISARMED) {

        digitalWrite(STATUS_PIN, LOW);

    } else {

        // ARMED and ALARM_TRIGGERED
        digitalWrite(STATUS_PIN, HIGH);
    }
}

// =====================================================
// EVENT QUEUE
// =====================================================

bool enqueueEvent(SystemState state) {

    if (queueCount >= EVENT_QUEUE_SIZE) {

        Serial.println("Error: event queue is full!");

        return false;
    }

    eventQueue[queueTail].state = state;

    eventQueue[queueTail].timestamp = millis() / 1000;

    queueTail = (queueTail + 1) % EVENT_QUEUE_SIZE;

    queueCount++;

    return true;
}

bool dequeueEvent(SystemEvent& event) {

    if (queueCount == 0) {
        return false;
    }

    event = eventQueue[queueHead];

    queueHead = (queueHead + 1) % EVENT_QUEUE_SIZE;

    queueCount--;

    return true;
}

// =====================================================
// EVENT PROCESSING
// =====================================================

void processPendingEvents() {

    SystemEvent event;

    while (dequeueEvent(event)) {

        addToHistory(event);

        Serial.print("[");

        printTimestamp(event.timestamp);

        Serial.print("] STATE CHANGED: ");

        printStateName(event.state);

        Serial.println();
    }
}

// =====================================================
// EVENT HISTORY
// =====================================================

void addToHistory(const SystemEvent& event) {

    eventHistory[historyIndex] = event;

    historyIndex = (historyIndex + 1) % HISTORY_SIZE;

    if (historyCount < HISTORY_SIZE) {
        historyCount++;
    }
}

void printEventHistory() {

    Serial.println();
    Serial.println("========== EVENT HISTORY ==========");

    if (historyCount == 0) {

        Serial.println("No events recorded.");

    } else {

        uint8_t oldestIndex =
            (historyIndex + HISTORY_SIZE - historyCount) % HISTORY_SIZE;

        for (uint8_t i = 0; i < historyCount; i++) {

            uint8_t index =
                (oldestIndex + i) % HISTORY_SIZE;

            Serial.print("[");

            printTimestamp(eventHistory[index].timestamp);

            Serial.print("] STATE CHANGED: ");

            printStateName(eventHistory[index].state);

            Serial.println();
        }
    }

    Serial.println("===================================");
    Serial.println();
}

// =====================================================
// STATE NAME
// =====================================================

void printStateName(SystemState state) {

    switch (state) {

        case DISARMED:
            Serial.print("DISARMED");
            break;

        case ARMED:
            Serial.print("ARMED");
            break;

        case ALARM_TRIGGERED:
            Serial.print("ALARM_TRIGGERED");
            break;

        default:
            Serial.print("UNKNOWN");
            break;
    }
}

// =====================================================
// SYSTEM STATUS
// =====================================================

void printStatus() {

    Serial.println();
    Serial.println("========== SYSTEM STATUS ==========");

    Serial.print("System state: ");

    printStateName(currentState);

    Serial.println();

    Serial.print("GPIO2 output: ");

    if (digitalRead(STATUS_PIN) == HIGH) {
        Serial.println("HIGH");
    } else {
        Serial.println("LOW");
    }

    if (currentState == ALARM_TRIGGERED) {

        unsigned long elapsed = millis() - alarmStartTime;

        unsigned long remaining =
            (ALARM_DURATION - elapsed + 999) / 1000;

        Serial.print("Alarm time remaining: ");
        Serial.print(remaining);
        Serial.println(" seconds");
    }

    Serial.print("Failed PIN attempts: ");
    Serial.println(failedPinAttempts);

    if (systemLocked) {

        unsigned long elapsed = millis() - lockoutStartTime;

        unsigned long remaining =
            (LOCKOUT_DURATION - elapsed + 999) / 1000;

        Serial.print("PIN lockout: ACTIVE (");
        Serial.print(remaining);
        Serial.println(" seconds remaining)");

    } else {

        Serial.println("PIN lockout: INACTIVE");
    }

    Serial.println("===================================");
    Serial.println();
}

// =====================================================
// HELP
// =====================================================

void printHelp() {

    Serial.println();
    Serial.println("========== AVAILABLE COMMANDS ==========");

    Serial.println("ARM <PIN>     - Activate the security system");
    Serial.println("DISARM <PIN>  - Deactivate the security system");
    Serial.println("TRIGGER       - Simulate an intrusion event");
    Serial.println("STATUS        - Display current system status");
    Serial.println("LOG           - Display event history");
    Serial.println("HELP          - Display available commands");

    Serial.println("========================================");
    Serial.println();
}

// =====================================================
// UPTIME
// =====================================================

void printUptime() {

    unsigned long uptimeSeconds = millis() / 1000;

    unsigned long hours = uptimeSeconds / 3600;
    unsigned long minutes = (uptimeSeconds % 3600) / 60;
    unsigned long seconds = uptimeSeconds % 60;

    Serial.print("Uptime: ");

    if (hours < 10) Serial.print("0");
    Serial.print(hours);

    Serial.print(":");

    if (minutes < 10) Serial.print("0");
    Serial.print(minutes);

    Serial.print(":");

    if (seconds < 10) Serial.print("0");
    Serial.println(seconds);
}

// =====================================================
// TIMESTAMP
// =====================================================

void printTimestamp(unsigned long timestamp) {

    unsigned long hours = timestamp / 3600;
    unsigned long minutes = (timestamp % 3600) / 60;
    unsigned long seconds = timestamp % 60;

    if (hours < 10) Serial.print("0");
    Serial.print(hours);

    Serial.print(":");

    if (minutes < 10) Serial.print("0");
    Serial.print(minutes);

    Serial.print(":");

    if (seconds < 10) Serial.print("0");
    Serial.print(seconds);
}
