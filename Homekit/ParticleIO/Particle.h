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

#pragma once

#include <Arduino.h>
#include <nvs.h>

///////////////////////////////
// Particle.io Configuration
///////////////////////////////

const int MAX_API_KEY = 40;                 // Particle.io access token length (bytes)
const int MAX_DEVICE_ID = 24;               // Particle.io device ID length (bytes)
const int API_HTTP_TIMEOUT = 8000;          // Particle.io API HTTP request timeout (ms)
const int PARTICLE_FUNCTION_RETRY_COUNT = 1; // Number of retries for function calls on timeout
const int PARTICLE_FUNCTION_RETRY_DELAY_MS = 750; // Delay between retries (ms) when a timeout occurs
const int PARTICLE_TASK_STACK_SIZE = 10240; // stack size for async API call tasks (bytes)

const int PARTICLE_THROTTLE_SECONDS = 10;   // Minimum API request interval to same endpoint (seconds)
const bool PARTICLE_THROTTLE_ENABLED = true; // set to false to disable throttling
const int PARTICLE_THROTTLE_CACHE_SIZE = 10; // Increase to track more Particle.io API endpoints

///////////////////////////////
// Task Parameter Structures for Async Calls
///////////////////////////////

struct CallFunctionTaskParams {
  char apiKey[MAX_API_KEY + 1];
  char deviceId[MAX_DEVICE_ID + 1];
  char functionName[65];
  char functionArgument[1025];
  void (*callback)(int result, bool success, void *userData);
  void *userData;
};

struct GetVariableTaskParams {
  char apiKey[MAX_API_KEY + 1];
  char deviceId[MAX_DEVICE_ID + 1];
  char variableName[65];
  void (*callback)(const char *result, bool success, void *userData);
  void *userData;
};

///////////////////////////////

struct ParticleConfig {
  
  static ParticleConfig* instance;     // Instance pointer
  
  nvs_handle particleNVS;          // NVS handle for Particle configuration data
  
  struct {
    char apiKey[MAX_API_KEY + 1] = "";
    char deviceId[MAX_DEVICE_ID + 1] = "";
  } particleData;

  // Constructor
  ParticleConfig();
  
  /*
  // Destructor
  ~ParticleConfig();
  */

  // Initialize Particle CLI commands and setup connection callback (call after homeSpan.begin())
  void init();

  // Static connection callback - invoked when WiFi/Ethernet connects
  static void onConnection(int connectionCount);

  // Read a string from serial input
  char *readSerial(char *c, int max);

  // Validate credentials with Particle.io API
  boolean validateCredentials(const char *apiKey, const char *deviceId);

  // Serial configuration for Particle.io credentials
  void serialConfigure();

  // Display current configuration
  void displayConfig();

  // Clear stored credentials
  void clearConfig();

  // Call a function asynchronously (non-blocking)
  // callback: function called when complete - callback(result, success, userData)
  // userData: optional pointer passed to callback
  void callFunctionAsync(const char *functionName, const char *functionArgument,
                         void (*callback)(int result, bool success, void *userData),
                         void *userData = nullptr);

  // Get a variable asynchronously (non-blocking)
  // callback: function called when complete - callback(result, success, userData)
  // userData: optional pointer passed to callback
  void getVariableAsync(const char *variableName,
                        void (*callback)(const char *result, bool success, void *userData),
                        void *userData = nullptr);

private:
  // Check if credentials are configured
  boolean isConfigured();
  
  // Check API throttling for endpoint - returns true if call should proceed, false if throttled
  boolean checkThrottle(const char *endpointType, const char *endpointName);
  
  // Static task implementations for FreeRTOS
  static void callFunctionTaskImpl(void *params);
  static void getVariableTaskImpl(void *params);
  
  // API throttle tracking structure
  struct ThrottleEntry {
    char endpoint[80];  // "function:name" or "variable:name"
    unsigned long lastCall;
  };
  
  ThrottleEntry throttleCache[PARTICLE_THROTTLE_CACHE_SIZE];
  int throttleCacheSize = 0;
  
  // Static CLI callback functions
  static void cliSetup(const char *buf);
  static void cliView(const char *buf);
  static void cliClear(const char *buf);

}; // end of struct ParticleConfig

///////////////////////////////
