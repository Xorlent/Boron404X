# HomeSpan Particle.io Gate Opener example

This project demonstrates integration of Particle.io cloud services with HomeSpan to control Particle.io devices via Apple HomeKit. The Particle implementation follows existing HomeSpan conventions to simplify setup.  Particle.io API calls are non-blocking to ensure proper HomeSpan operation, even with high latency LTE-M Particle.io devices.  The [Boron404X Starter Kit](https://store.particle.io/collections/all-products/products/boron-lte-cat-m1-noram-starter-kit-with-ethersim-brn404xkit) includes a SIM that is free of activation or service fees.  For simple use cases, the included 100,000 monthly data operations are enough to use the device with no monthly charge -- and no credit card is required.

## Required Hardware

- **ESP32 Dev Board**: This device will run the ParticleIO.ino sketch and serve as the bridge between Apple Homekit and the Particle.io device
- **Particle Boron404X Starter Kit**: This device will run the ParticleGateBoron404X.ino sketch, acting as the cellular-connected gate opener in this demo project

## Files

- **ParticleIO.ino**: Main sketch demonstrating a gate opener
- **Boron404X/ParticleGateBoron404X.ino**: Particle Boron 404X gate opener companion code
- **Particle.h**: Particle header (place in the same folder as your .ino file)
- **Particle.cpp**: Particle code (place in the same folder as your .ino file)

## Arduino Libraries

- **Homespan by Gregg**: Apple Homekit library

## Quick Start

### 1. Include and Initialize

```cpp
#include <HomeSpan.h>
#include "Particle.h"

ParticleConfig particle;  // Global instance

void setup() {
  Serial.begin(115200);
  
  homeSpan.begin();
  
  // Create your accessories here
  
  particle.init();  // Register CLI commands and connection callback
  homeSpan.autoPoll();  // Start threaded HomeSpan polling
}
```

The `particle.init()` method automatically:
- Registers HomeSpan CLI commands (see below)
- Sets up a connection callback
- If uninitialized, triggers a Particle.io serial configuration prompt on network connection

### 2. First Run

After WiFi connects and HomeSpan's HTTP server initializes, you'll be automatically prompted to enter your Particle.io credentials.

- Create an access token at https://docs.particle.io/reference/cloud-apis/access-tokens/#create-a-token-browser-based-
- Retrieve your device ID at https://console.particle.io/devices

## CLI Commands

Custom commands added to HomeSpan's Serial Monitor CLI interface:

- **'I'** - (I)nput Particle.io configuration details (reconfigure)
- **'G'** - (G)et Particle.io configuration details (view current)
- **'N'** - (N)ull/remove Particle.io configuration details (delete stored credentials)

## API Methods

### Init()

```cpp
void init()           // Initialize Particle (call at the end of setup())
```

#### callFunctionAsync()

```cpp
void callFunctionAsync(const char *functionName, 
                      const char *functionArgument,
                      void (*callback)(int result, bool success, void *userData),
                      void *userData = nullptr);
```

**Example:**
```cpp
void onGateCommand(int result, bool success, void *userData) {
  if(success) {
    LOG1("Gate command returned: %d\n", result);
  } else {
    LOG0("*** Failed to send gate command to Particle device\n");
  }
}

boolean update() override {
  int target = targetDoorState->getNewVal();
  
  particle.callFunctionAsync("setDoor", 
                            target == 0 ? "open" : "close",
                            onGateCommand,
                            this);
  
  currentDoorState->setVal(target == 0 ? 2 : 3);  // Opening or Closing
  return true;
}
```

#### getVariableAsync()

```cpp
void getVariableAsync(const char *variableName,
                     void (*callback)(const char *result, bool success, void *userData),
                     void *userData = nullptr);
```

**Example:**
```cpp
void onDoorStatus(const char *result, bool success, void *userData) {
  if(success) {
    LOG2("Door status from Particle: %s\n", result);
    // Parse result: "open" (0), "closed" (1), "opening" (2), "closing" (3)
  } else {
    LOG0("*** Failed to get door status from Particle device\n");
  }
}

particle.getVariableAsync("doorState", onDoorStatus, this);
```

## API Limits and Validation

### Particle.io API field limits

- **Function/Variable Names**: 64 characters
- **Function Arguments**: 1024 characters
- **Variable Data**: 1024 characters

### API Throttling

Per-endpoint throttling prevents accidental code loops that would likely cause API lockout or exhaust API credits:

- **Default**: 10 seconds minimum between calls to same endpoint
- **Scope**: Per-endpoint (function:setDoor vs variable:doorState tracked separately)
- **Behavior**: Calls within throttle window are rejected with error callback
- **Cache Size**: Tracks up to 10 unique endpoints

**Configurable settings in Particle.h:**
```cpp
const int PARTICLE_FUNCTION_RETRY_COUNT = 1;    // Number of retries for function calls on timeout
const int PARTICLE_FUNCTION_RETRY_DELAY_MS = 750; // Delay between retries (ms)
const int PARTICLE_THROTTLE_SECONDS = 10;       // Minimum API request interval (seconds)
const bool PARTICLE_THROTTLE_ENABLED = true;    // Set to false to disable throttling
const int PARTICLE_THROTTLE_CACHE_SIZE = 10;    // Max unique endpoints to track
```

## Requirements

- Arduino development environment set up according to HomeSpan documentation
- Compatible ESP32 device
- Particle.io account and device

## Example Particle Boron 404X Device Code

The companion Particle device code is included in the **Boron404X/** folder. Upload **ParticleGateBoron404X.ino** to your Particle device via https://build.particle.io or Particle Workbench.

**Particle Device Functions and Variables:**
- **Function:** `setDoor(String arg)` - Accepts "open" or "close" commands
- **Variable:** `doorState` - Returns 0 (open), 1 (closed), 2 (opening), or 3 (closing)
- **Variable:** `obstruction` - Returns true if door failed to reach target state within timer

## Troubleshooting

**Credentials not saving:**
- Check NVS partition exists in the ESP32 partition table
- Verify NVS namespace opens successfully (check serial messages)

**API calls failing:**
- Check internet connectivity
- Confirm the Particle device is online
- Validate function/variable names match the Particle device code

**Throttle errors:**
- Check for unintended repeated calls in your code
- Increase `PARTICLE_THROTTLE_SECONDS` or disable throttling
- Increase `PARTICLE_THROTTLE_CACHE_SIZE` if using more than 10 Particle.io API endpoints
