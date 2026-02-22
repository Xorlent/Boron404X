// Boron 404X garage door/gate opener example
// Wait 5 minutes following a re-flash before attempting to use the device
#include "Particle.h"

SYSTEM_MODE(SEMI_AUTOMATIC);
//SYSTEM_THREAD(ENABLED); // Needed only for OS < 6.2.  ParticleOS 6.2.1 or higher is strongly recommended for best stability.

//SerialLogHandler logHandler(LOG_LEVEL_INFO); // Serial logging feature

uint16_t doorSensor = D5; // Wire to a normally closed switch that opens when the door is shut (pin -> switch -> ground)
uint16_t doorSwitch = D6; // Wire to the door actuator, usually a 3.3v relay or solid state switch to provide dry contacts for the opener

int doorState = 0;  // 0=open, 1=closed, 2=opening, 3=closing
int doorTimer = 12; // Give door/gate time (seconds) to complete a cycle.
bool obstruction = false; // Last command was unsuccessful within the time alloted by doorTimer

int cycleTime = 1000; // Particle message processing interval (ms)
unsigned long lastCycleMs = 0; // Keeps track of the last time we processed Particle messages within loop()
unsigned long reconnectWait = 720000; // Time to wait for the OS to reconnect to the cloud.  11 minute minimum per Particle documentation (ms)

// Obstruction/transition checking variables
unsigned long commandStartTime = 0; // Time when the door command was initiated
bool checkingCompletion = false; // Flag to indicate we're checking for command completion
int targetDoorState = 0; // The expected final door state (0=open, 1=closed)

int setDoor(String arg) { // Door command function
  if (arg == "close") {
    digitalWrite(doorSwitch, HIGH);
    doorState = 3;  // Closing
    delay(200);
    digitalWrite(doorSwitch, LOW);
    
    // Start the gate/door transition check
    commandStartTime = millis();
    checkingCompletion = true;
    targetDoorState = 1; // Expecting closed state
    obstruction = false; // Reset obstruction flag
    
    return 1; // Return success
  }
  else if (arg == "open") {
    digitalWrite(doorSwitch, HIGH);
    doorState = 2;  // Opening
    delay(200);
    digitalWrite(doorSwitch, LOW);
    
    // Start the gate/door transition check
    commandStartTime = millis();
    checkingCompletion = true;
    targetDoorState = 0; // Expecting open state
    obstruction = false; // Reset obstruction flag
    
    return 1; // Return success
  }
  return 0; // Received an invalid function argument
}

int readDoor() { // Used for our Particle variable doorState calls, sampling input to get current value or returning in-process command state
  if (doorState > 1){ // We are currently performing a command on the door/gate, return appropriate state since we are in transition
    return doorState;
  }
  else {
    int doorVal = digitalRead(doorSensor);
    return doorVal;
  }
}

void setup() {

  Watchdog.init(WatchdogConfiguration().timeout(600s));
  Watchdog.start();

  pinMode(doorSensor, INPUT_PULLUP);
  pinMode(doorSwitch, OUTPUT);

  // Define available callable function and state variables
  Particle.function("setDoor", setDoor);
  Particle.variable("doorState", readDoor);
  Particle.variable("obstruction", obstruction);

  Particle.connect(); // Perform initial cloud connection
  
}

void loop() {

  Watchdog.refresh(); // Let the hardware watchdog know we are still successfully processing instructions
  
  // Obstruction checking loop
  if (checkingCompletion) {
    unsigned long elapsedTime = (millis() - commandStartTime) / 1000;
    
    // Check if door has reached target state
    int currentSensorState = digitalRead(doorSensor);
    
    if (targetDoorState == 1 && currentSensorState == 1) {
      // Successfully closed
      doorState = 1;
      obstruction = false;
      checkingCompletion = false;
    }
    else if (targetDoorState == 0 && currentSensorState == 0) {
      // Successfully opened
      doorState = 0;
      obstruction = false;
      checkingCompletion = false;
    }
    else if (elapsedTime >= doorTimer) {
      // Timer expired without reaching target state
      doorState = currentSensorState;
      obstruction = true;
      checkingCompletion = false;
    }
  }
  
  if (Particle.connected() && (unsigned long)(millis() - lastCycleMs > cycleTime)){
    Particle.process(); // Process any incoming messages
    lastCycleMs = millis();
  }
  else if (!Particle.connected() && (unsigned long)(millis() - lastCycleMs > reconnectWait)){ // PartcleOS lost connection, wait reconnectWait then restart device if necessary
    System.reset(RESET_NO_WAIT);
  }

}
