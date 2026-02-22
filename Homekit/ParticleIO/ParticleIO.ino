/*********************************************************************************
 *  MIT License
 *  
 *  Copyright (c) 2020-2024 Gregg E. Berman, contributed by Xorlent 2026
 *  
 *  https://github.com/HomeSpan/HomeSpan
 *  https://github.com/Xorlent/Boron404X/Homekit/ParticleIO
 *  
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *  
 ********************************************************************************/

///////////////////////////////
//  Particle.io Gate Opener  //
///////////////////////////////
//
// This sketch demonstrates how to integrate Particle.io cloud services with HomeSpan
// to control a remote garage door/gate opener.  Companion code is included for a
// Particle Boron 404X LTE-M development board, see ParticleGateBoron404X.ino. 
//
// PARTICLE SETUP:
// - On first run, you will be automatically prompted to enter Particle configuration details
//
// CONFIGURATION FLOW:
// - homeSpan.begin() initializes HomeSpan
// - Define your accessories
// - particle.init() sets up CLI commands and connection callback (call after accessories defined)
// - A configuration prompt will appear in the serial monitor if Particle credentials do not exist
//
// USING PARTICLE APIs:
// - particle.callFunctionAsync(name, arg, callback)  - Call a device function
// - particle.getVariableAsync(name, callback)        - Get a device variable
//
// CLI COMMANDS (via Serial Monitor):
// - 'I' - (I)nput Particle.io credentials
// - 'G' - (G)et Particle.io configuration details
// - 'N' - (N)ull/remove Particle.io configuration details
///////////////////////////////

#include <HomeSpan.h>
#include "Particle.h"

ParticleConfig particle;  // Create global Particle configuration object

struct GateOpener : Service::GarageDoorOpener {
  
  Characteristic::CurrentDoorState *currentDoorState;
  Characteristic::TargetDoorState  *targetDoorState;
  Characteristic::ObstructionDetected *obstructionDetected;

  unsigned long commandSentTime = 0;  // Track when command was sent
  unsigned long lastObstructionCheck = 0;  // Track last obstruction poll
  unsigned long lastCheck = 0;  // Track last regular door state poll
  int previousDoorState = 1;  // Track actual door state before command (default: closed)
  int commandedTargetState = -1;  // Track the target state we commanded (handles contention w/multiple in-flight commands)
  bool commandInFlight = false;  // Prevent overlapping Particle function calls/retries

  GateOpener(const char *name) : Service::GarageDoorOpener() {
    
    currentDoorState = new Characteristic::CurrentDoorState(1);  // Initialize as closed
    targetDoorState  = new Characteristic::TargetDoorState(1);   // Initialize as closed
    obstructionDetected = new Characteristic::ObstructionDetected(false);
    
  } // end of GateOpener()

  ///////////////////////////////

  // Callback when Particle function call completes
  static void onGateCommand(int result, bool success, void *userData) {
    GateOpener *self = (GateOpener*)userData;
    
    if(success) {
      LOG1("Gate command returned: %d\n", result);
      // Actual door state will be updated by polling loop
    } else {
      LOG0("*** Failed to send gate command to Particle device\n");
      // Revert to previous actual door state on failure

      // Handle setVal/getVal from another thread
      homeSpanPAUSE;

      self->currentDoorState->setVal(self->previousDoorState);
      self->commandSentTime = 0;      // stop accelerated polling when command failed
      self->commandedTargetState = -1;
      self->commandInFlight = false;

      homeSpanRESUME;
    }

    // Clear in-flight flag on success as well
    self->commandInFlight = false;
  }

  ///////////////////////////////

  // Callback when door status variable is retrieved
  static void onDoorStatus(const char *result, bool success, void *userData) {
    GateOpener *self = (GateOpener*)userData;
    
    if(success) {
      LOG2("Door status from Particle: %s\n", result);
      
      // Parse result and update HomeKit characteristic
      // Expected values: "open" (0), "closed" (1), "opening" (2), "closing" (3)
      int newState = -1;
      
      if(strcmp(result, "open") == 0 || strcmp(result, "0") == 0) {
        newState = 0;  // Open
      } else if(strcmp(result, "closed") == 0 || strcmp(result, "1") == 0) {
        newState = 1;  // Closed
      } else if(strcmp(result, "opening") == 0 || strcmp(result, "2") == 0) {
        newState = 2;  // Opening
      } else if(strcmp(result, "closing") == 0 || strcmp(result, "3") == 0) {
        newState = 3;  // Closing
      }
      
      // Handle setVal/getVal from another thread
      homeSpanPAUSE;
      
      if(newState >= 0 && newState != self->currentDoorState->getVal()) {
        self->currentDoorState->setVal(newState);
        LOG1("Door state updated to: %d\n", newState);
        
        // Stop polling if we've reached the commanded target state
        if(self->commandedTargetState >= 0 && newState == self->commandedTargetState) {
          self->commandSentTime = 0;
          self->commandedTargetState = -1;
          self->commandInFlight = false;
          LOG2("Commanded target state reached - stopping high frequency polling cycle\n");
        }
        // If door state changed without a command (e.g., manually opened/closed),
        // sync targetDoorState to prevent HomeKit showing transitional states
        else if(self->commandedTargetState == -1 && (newState == 0 || newState == 1)) {
          self->targetDoorState->setVal(newState);
          LOG2("Door state changed without command - syncing target state to: %d\n", newState);
        }
      }

      homeSpanRESUME;
      
    } else {
      LOG0("*** Failed to get door status from Particle device\n");
    }
  }

  ///////////////////////////////

  // Callback when obstruction variable is retrieved
  static void onObstructionStatus(const char *result, bool success, void *userData) {
    GateOpener *self = (GateOpener*)userData;
    
    if(success) {
      LOG2("Obstruction status from Particle: %s\n", result);
      
      // Parse result and update HomeKit characteristic
      bool isObstructed = false;
      
      if(strcmp(result, "true") == 0 || strcmp(result, "1") == 0) {
        isObstructed = true;
      } else if(strcmp(result, "false") == 0 || strcmp(result, "0") == 0) {
        isObstructed = false;
      }
      
      // Handle setVal/getVal from another thread
      homeSpanPAUSE;
      
      if(isObstructed != self->obstructionDetected->getVal()) {
        self->obstructionDetected->setVal(isObstructed);
        LOG1("Obstruction detected: %s\n", isObstructed ? "YES" : "NO");
      }

      homeSpanRESUME;
      
    } else {
      LOG0("*** Failed to get obstruction status from Particle device\n");
    }
  }

  ///////////////////////////////

  boolean update() override {

    // Block new commands while a previous function call (or retry) is still running
    if(commandInFlight) {
      LOG1("Ignoring new door request while prior command is in-flight\n");
      targetDoorState->setVal(commandedTargetState >= 0 ? commandedTargetState : currentDoorState->getVal());
      return false;
    }
    
    // Get the target state from HomeKit
    int target = targetDoorState->getNewVal();
    
    LOG1("HomeKit requested door state: %s\n", target == 0 ? "OPEN" : "CLOSED");
    
    // Save current state before changing it
    previousDoorState = currentDoorState->getVal();
    
    // Set intermediate state immediately
    currentDoorState->setVal(target == 0 ? 2 : 3);  // Opening or Closing
    
    // Start obstruction polling and save the commanded target state
    commandSentTime = millis();
    lastObstructionCheck = 0;  // Reset so first check happens at 5 seconds
    commandedTargetState = target;  // Save target to check against later
    commandInFlight = true;
    
    particle.callFunctionAsync("setDoor", 
                              target == 0 ? "open" : "close",
                              onGateCommand,
                              this);
    
    return true;  // HomeKit gets immediate response
    
  } // end of update()

  ///////////////////////////////

  void loop() override {
    
    // Poll for gate/door state and obstruction status after a command was sent
    if(commandSentTime > 0) {
      unsigned long elapsed = millis() - commandSentTime;
      
      // We expect gate/door to complete task within roughly 30 seconds
      if(elapsed <= 32000 && elapsed >= 5000) {
        // Request door state and obstruction status at about 5, 17, 29 seconds
        if(millis() - lastObstructionCheck > 11000) {
          lastObstructionCheck = millis();
          lastCheck = millis();  // Reset doorState polling timer to avoid duplicate calls
          
          particle.getVariableAsync("doorState", onDoorStatus, this);
          delay(1000);  // Increase delay to 1 second to ensure first request completes
          particle.getVariableAsync("obstruction", onObstructionStatus, this);
          
          LOG2("Polling for door state and obstruction from Particle...\n");
        }
      } else {
        // stop high rate polling
        commandSentTime = 0;
        commandedTargetState = -1;
        commandInFlight = false;
      }
    }

    // Poll Particle device every 36 seconds for actual gate/door state
    // On the Particle.io free plan, this will consume about 75k of your 100k data operations budget
    if(millis() - lastCheck > 36000) {
      lastCheck = millis();
      
      particle.getVariableAsync("doorState", onDoorStatus, this);
      
      LOG2("Polling door status from Particle...\n");
    }

  } // end of loop()

}; // end of struct GateOpener

//////////////////////////////////////

void setup() {
  
  Serial.begin(115200);

  homeSpan.begin();

  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Name("Gate Opener");
      new Characteristic::Identify();
    new GateOpener("Gate Opener");

  // Initialize Particle
  particle.init();
  homeSpan.autoPoll();
} // end of setup()

//////////////////////////////////////

void loop(){
  
} // end of loop()
