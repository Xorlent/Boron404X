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

#include "Particle.h"
#include <HomeSpan.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_err.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

///////////////////////////////

// Initialize static instance pointer
ParticleConfig* ParticleConfig::instance = nullptr;

///////////////////////////////

ParticleConfig::ParticleConfig() {
  instance = this;  // Store instance
  
  esp_err_t err = nvs_open("PARTICLE", NVS_READWRITE, &particleNVS);
  
  if(err != ESP_OK) {
    LOG0("\n*** ERROR: Failed to open NVS namespace PARTICLE (error %d)\n", err);
    LOG0("*** Particle.io credentials cannot be loaded. You will be prompted to enter them.\n\n");
    particleData.apiKey[0] = '\0';
    particleData.deviceId[0] = '\0';
    return;
  }
  
  size_t len = 0;
  // Try to load existing Particle credentials from NVS
  if(nvs_get_blob(particleNVS, "PDATA", NULL, &len) == ESP_OK) {
    // Validate data size
    if(len == sizeof(particleData)) {
      nvs_get_blob(particleNVS, "PDATA", &particleData, &len);
      // Null-terminate strings
      particleData.apiKey[MAX_API_KEY] = '\0';
      particleData.deviceId[MAX_DEVICE_ID] = '\0';
      LOG0("\n*** Particle.io credentials loaded successfully\n");
    } else {
      LOG0("\n*** WARNING: Stored Particle.io data unreadable.\n");
      LOG0("*** You will be prompted to re-enter your Particle.io device details.\n\n");
      // Ensure data is empty so isConfigured() returns false and user is prompted
      particleData.apiKey[0] = '\0';
      particleData.deviceId[0] = '\0';
    }
  }
}

///////////////////////////////
/*
ParticleConfig::~ParticleConfig() {
  nvs_close(particleNVS);  // Close NVS handle
}
*/
///////////////////////////////

void ParticleConfig::init() {
  // Register custom CLI commands
  new SpanUserCommand('I', "- (I)nput Particle.io configuration details", cliSetup);
  new SpanUserCommand('G', "- (G)et Particle.io configuration details", cliView);
  new SpanUserCommand('N', "- (N)ULL/remove Particle.io configuration details", cliClear);
  
  // Register connection callback to trigger configuration when network is ready
  homeSpan.setConnectionCallback(onConnection);
}

///////////////////////////////

void ParticleConfig::onConnection(int connectionCount) {
  LOG2("Particle connection callback triggered (count: %d)\n", connectionCount);
  
  // Only trigger setup on first connection
  if(connectionCount == 1) {
    // Check if Particle.io credentials are configured
    if(!instance->isConfigured()) {
      LOG0("\n*** Particle.io configuration details not found ***\n");
      LOG0("*** Starting Particle.io setup... ***\n\n");
      instance->serialConfigure();
    } else {
      instance->displayConfig();
    }
  }
}

///////////////////////////////

void ParticleConfig::cliSetup(const char *buf) {
  // Check if WiFi is connected before attempting configuration
  if(WiFi.status() != WL_CONNECTED) {
    LOG0("\n*** ERROR: WiFi not connected - cannot configure Particle.io credentials ***\n");
    LOG0("*** Type 'W' to configure and connect to WiFi ***\n\n");
    return;
  }
  
  LOG0("\n*** Reconfiguring Particle.io credentials ***\n");
  instance->clearConfig();
  instance->serialConfigure();
}

///////////////////////////////

void ParticleConfig::cliView(const char *buf) {
  instance->displayConfig();
}

///////////////////////////////

void ParticleConfig::cliClear(const char *buf) {
  LOG0("\n*** Clearing Particle.io configuration ***\n");
  
  if(!instance->isConfigured()) {
    LOG0("*** No configuration found to clear ***\n\n");
    return;
  }
  
  // Display what will be cleared
  LOG0("Current configuration:\n");
  LOG0("  Device ID:     %s\n", instance->particleData.deviceId);
  LOG0("  Access Token: <configured>\n");
  
  // Confirm deletion
  char confirm[2];
  LOG0("\n>>> Confirm deletion (y/n): ");
  instance->readSerial(confirm, 1);
  
  if(confirm[0] == 'y' || confirm[0] == 'Y') {
    LOG0("(yes)\n");
    instance->clearConfig();
    LOG0("*** Particle.io configuration CLEARED. Enter 'I' if you want to enter new Particle.io configuration details. ***\n\n");
  } else {
    LOG0("(no)\n*** Configuration deletion cancelled ***\n\n");
  }
}

///////////////////////////////

char *ParticleConfig::readSerial(char *c, int max) {

  if(homeSpan.getSerialInputDisable()){
    c[0]='\0';
    return(c);
  }
  
  int i = 0;
  char buf;

  while(1) {

    while(!Serial.available())
      homeSpan.resetWatchdog();
    
    buf = Serial.read();
    
    if(buf == '\n') {          // Exit on newline and null terminate
      c[i] = '\0';
      return(c);
    }

    if(buf != '\r') {          // Save up to CR
      if(i < max) {
        c[i] = buf;  
        i++;
      }
    }
  
  }
}

///////////////////////////////

boolean ParticleConfig::validateCredentials(const char *apiKey, const char *deviceId) {
  
  // Check if WiFi is connected
  if(WiFi.status() != WL_CONNECTED) {
    LOG0("\n*** ERROR: WiFi not connected - cannot validate credentials ***\n");
    return false;
  }
  
  LOG0("\n>>> Validating credentials and pinging device...\n");
  
  WiFiClientSecure *secureClient = new WiFiClientSecure;
  if(secureClient) {
    secureClient->setInsecure();  // Skip certificate verification
    
    int httpCode = 0;
    bool success = false;

    {
      HTTPClient http;
      http.begin(*secureClient, String("https://api.particle.io/v1/devices/") + deviceId + "/ping");
      http.addHeader("Authorization", String("Bearer ") + apiKey);
      http.setTimeout(API_HTTP_TIMEOUT);
    
      httpCode = http.PUT("");
    
      if(httpCode == 200) {
        // Read and parse the JSON response
        String response = http.getString();
        
        // Parse JSON to extract "online" value
        int onlinePos = response.indexOf("\"online\":");
        if(onlinePos > 0) {
          int truePos = response.indexOf("true", onlinePos);
          int falsePos = response.indexOf("false", onlinePos);
        
          if(truePos > onlinePos && (falsePos < 0 || truePos < falsePos)) {
            LOG0(">>> Device is ONLINE and credentials validated successfully!\n");
          } else if(falsePos > onlinePos && (truePos < 0 || falsePos < truePos)) {
            LOG0(">>> Device is OFFLINE but credentials validated successfully.\n");
          } else {
            LOG0(">>> Credentials validated successfully!\n");
          }
        } else {
          LOG0(">>> Credentials validated successfully!\n");
        }
        success = true;
      } else {
        if(httpCode > 0) {
          LOG0("*** ERROR: Invalid Particle.io API configuration details were provided (HTTP %d)\n", httpCode);
        } else {
          LOG0("*** ERROR: Connection failed (error: %s)\n", http.errorToString(httpCode).c_str());
        }
        success = false;
      }
      http.end();
    } // HTTPClient destroyed here

    delete secureClient;
    return success;
  }
  return false;
}

///////////////////////////////

void ParticleConfig::serialConfigure() {
  
  char tempApiKey[MAX_API_KEY + 1];
  char tempDeviceId[MAX_DEVICE_ID + 1];
  boolean validated = false;

  LOG0("\n*** Particle.io Setup ***\n\n");
  
  while(!validated) {
    tempApiKey[0] = '\0';
    tempDeviceId[0] = '\0';
    
    // Get access token
    while(!strlen(tempApiKey) || strlen(tempApiKey) != MAX_API_KEY) {
      LOG0(">>> Particle.io access token (see https://docs.particle.io/reference/cloud-apis/access-tokens/#create-a-token-browser-based-): ");
      readSerial(tempApiKey, MAX_API_KEY);
      LOG0("<entered>\n");
      if(strlen(tempApiKey) > 0 && strlen(tempApiKey) != MAX_API_KEY) {
        LOG0("*** ERROR: Access token must be exactly %d characters (received %d characters). Please re-enter.\n\n", MAX_API_KEY, strlen(tempApiKey));
        tempApiKey[0] = '\0';
      }
    }
    
    // Get Device ID
    while(!strlen(tempDeviceId) || strlen(tempDeviceId) != MAX_DEVICE_ID) {
      LOG0(">>> Particle.io Device ID (see https://console.particle.io/devices): ");
      readSerial(tempDeviceId, MAX_DEVICE_ID);
      LOG0("%s\n", tempDeviceId);
      if(strlen(tempDeviceId) > 0 && strlen(tempDeviceId) != MAX_DEVICE_ID) {
        LOG0("*** ERROR: Device ID must be exactly %d characters (received %d characters). Please re-enter.\n\n", MAX_DEVICE_ID, strlen(tempDeviceId));
        tempDeviceId[0] = '\0';
      }
    }
    
    // Validate credentials with Particle.io API
    validated = validateCredentials(tempApiKey, tempDeviceId);
    
    if(!validated) {
      LOG0("\n*** Particle.io API test call FAILED  Re-prompting for configuration details.\n\n");
    }
  }
  
  // Copy validated credentials to member data
  strcpy(particleData.apiKey, tempApiKey);
  strcpy(particleData.deviceId, tempDeviceId);

  // Save to NVS
  nvs_set_blob(particleNVS, "PDATA", &particleData, sizeof(particleData));
  nvs_commit(particleNVS);
  
  LOG0("\n*** Particle.io configuration details saved successfully.\n\n");
}

///////////////////////////////

boolean ParticleConfig::isConfigured() {
  return (strlen(particleData.apiKey) > 0 && strlen(particleData.deviceId) > 0);
}

///////////////////////////////

boolean ParticleConfig::checkThrottle(const char *endpointType, const char *endpointName) {
  if(!PARTICLE_THROTTLE_ENABLED) {
    return true;  // Throttling disabled, allow call
  }
  
  char endpoint[80];
  snprintf(endpoint, sizeof(endpoint), "%s:%s", endpointType, endpointName);
  unsigned long now = millis();
  
  // Search for existing entry
  for(int i = 0; i < throttleCacheSize; i++) {
    if(strcmp(throttleCache[i].endpoint, endpoint) == 0) {
      unsigned long elapsed = now - throttleCache[i].lastCall;
      if(elapsed < PARTICLE_THROTTLE_SECONDS * 1000) {
        LOG0("*** ERROR: API throttle active for %s '%s' (called %.1f seconds ago, minimum %d seconds)\n",
             endpointType, endpointName, elapsed / 1000.0, PARTICLE_THROTTLE_SECONDS);
        return false;  // Throttled
      }
      // Update existing entry
      throttleCache[i].lastCall = now;
      return true;  // Allow call
    }
  }
  
  // Add new entry if space available
  if(throttleCacheSize < PARTICLE_THROTTLE_CACHE_SIZE) {
    strncpy(throttleCache[throttleCacheSize].endpoint, endpoint, sizeof(throttleCache[throttleCacheSize].endpoint) - 1);
    throttleCache[throttleCacheSize].endpoint[sizeof(throttleCache[throttleCacheSize].endpoint) - 1] = '\0';
    throttleCache[throttleCacheSize].lastCall = now;
    throttleCacheSize++;
  } else {
    // Cache full - new endpoint cannot be throttled
    LOG0("*** WARNING: Throttle cache full (%d/%d endpoints). %s '%s' will NOT be throttled.\n",
         throttleCacheSize, PARTICLE_THROTTLE_CACHE_SIZE, endpointType, endpointName);
    LOG0("*** Consider increasing PARTICLE_THROTTLE_CACHE_SIZE in Particle.h\n");
  }
  
  return true;  // Allow call
}

///////////////////////////////

void ParticleConfig::displayConfig() {
  LOG0("\n*** Current Particle.io Configuration ***\n");
  LOG0("  Access Token: %s\n", strlen(particleData.apiKey) > 0 ? "<configured>" : "<not configured>");
  LOG0("  Device ID: %s\n", strlen(particleData.deviceId) > 0 ? particleData.deviceId : "<not configured>");
  LOG0("\n");
}

///////////////////////////////

void ParticleConfig::clearConfig() {
  particleData.apiKey[0] = '\0';
  particleData.deviceId[0] = '\0';
  nvs_erase_key(particleNVS, "PDATA");
  nvs_commit(particleNVS);
  LOG0("\n*** Particle.io configuration details cleared successfully.\n\n");
}

///////////////////////////////

// FreeRTOS task entry point - called by xTaskCreate() from callFunctionAsync()
// Executes HTTP POST in separate thread to avoid blocking HomeSpan event loop
void ParticleConfig::callFunctionTaskImpl(void *params) {
  CallFunctionTaskParams *task = (CallFunctionTaskParams*)params;
  
  // Save callback info
  void (*localCallback)(int, bool, void*) = task ? task->callback : nullptr;
  void *localUserData = task ? task->userData : nullptr;
  
  int returnValue = -1;
  bool success = false;
  
  for(int attempt = 0; attempt <= PARTICLE_FUNCTION_RETRY_COUNT && !success; attempt++) {
    WiFiClientSecure *secureClient = new WiFiClientSecure;
    if(!secureClient) {
      break;
    }

    secureClient->setInsecure();  // Skip certificate verification
    
    int httpCode = 0;
    
    {
      HTTPClient http;
      http.begin(*secureClient, String("https://api.particle.io/v1/devices/") + task->deviceId + "/" + task->functionName);
      http.addHeader("Authorization", String("Bearer ") + task->apiKey);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      http.setTimeout(API_HTTP_TIMEOUT);
      http.setConnectTimeout(3000);
    
      // Build POST body with argument
      String postData = String("arg=") + task->functionArgument;
    
      httpCode = http.POST(postData);
    
      if(httpCode == 200) {
        String response = http.getString();
        
        // Parse JSON to extract "return_value" field
        int returnPos = response.indexOf("\"return_value\":");
        if(returnPos > 0) {
          int startPos = returnPos + 15;
          
          // Skip whitespace
          while(startPos < response.length() && (response.charAt(startPos) == ' ' || response.charAt(startPos) == '\t')) {
            startPos++;
          }
          
          // Extract the number
          int endPos = startPos;
          while(endPos < response.length() && (isdigit(response.charAt(endPos)) || response.charAt(endPos) == '-')) {
            endPos++;
          }
          
          String returnValueStr = response.substring(startPos, endPos);
          returnValue = returnValueStr.toInt();
          success = true;
        }
      } else {
        LOG0("*** ERROR: Particle callFunction failed (HTTP %d): %s\n", httpCode, http.errorToString(httpCode).c_str());
      }

      http.end();
    } // HTTPClient destroyed here

    delete secureClient;

    // Retry only on read timeouts and only for function calls
    if(!success && httpCode == HTTPC_ERROR_READ_TIMEOUT && attempt < PARTICLE_FUNCTION_RETRY_COUNT) {
      LOG1("Retrying Particle function call after timeout (attempt %d of %d)\n", attempt + 1, PARTICLE_FUNCTION_RETRY_COUNT);
      vTaskDelay(pdMS_TO_TICKS(PARTICLE_FUNCTION_RETRY_DELAY_MS));
    }
  }
  
  // Stack usage logging
  UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
  size_t stackUsed = PARTICLE_TASK_STACK_SIZE - (stackRemaining * sizeof(StackType_t));
  LOG2("Particle callFunction task: %d/%d bytes stack used (%d bytes remaining)\n", 
       stackUsed, PARTICLE_TASK_STACK_SIZE, stackRemaining * sizeof(StackType_t));
  
  // Free task parameters (already copied callback info)
  delete task;
  task = nullptr;
  
  // Always invoke callback if it exists - callback uses homeSpanPAUSE/homeSpanRESUME for thread safety
  if(localCallback) {
    localCallback(returnValue, success, localUserData);
  }
  
  vTaskDelete(NULL);
}

///////////////////////////////

// FreeRTOS task entry point - called by xTaskCreate() from getVariableAsync()
// Executes HTTP GET in separate thread to avoid blocking HomeSpan event loop
void ParticleConfig::getVariableTaskImpl(void *params) {
  GetVariableTaskParams *task = (GetVariableTaskParams*)params;
  
  // Save callback info
  void (*localCallback)(const char*, bool, void*) = task ? task->callback : nullptr;
  void *localUserData = task ? task->userData : nullptr;
  
  char resultBuffer[1025];
  resultBuffer[0] = '\0';
  bool success = false;
  
  WiFiClientSecure *secureClient = new WiFiClientSecure;
  if(secureClient) {
    secureClient->setInsecure();  // Skip certificate verification
    
    int httpCode = 0;
    
    {
      HTTPClient http;
      http.begin(*secureClient, String("https://api.particle.io/v1/devices/") + task->deviceId + "/" + task->variableName);
      http.addHeader("Authorization", String("Bearer ") + task->apiKey);
      http.setTimeout(API_HTTP_TIMEOUT);
      http.setConnectTimeout(3000);
    
      httpCode = http.GET();
    
      if(httpCode == 200) {
        String response = http.getString();
        
        // Parse JSON to extract "result" field
        int resultPos = response.indexOf("\"result\":");
        if(resultPos > 0) {
          int startPos = resultPos + 9;
          
          // Skip whitespace
          while(startPos < response.length() && (response.charAt(startPos) == ' ' || response.charAt(startPos) == '\t')) {
            startPos++;
          }
          
          // Check if result is a string or a number/boolean
          if(response.charAt(startPos) == '\"') {
            // String value
            startPos++;
            int endPos = response.indexOf('\"', startPos);
            if(endPos > startPos) {
              String result = response.substring(startPos, endPos);
              if(result.length() >= 1024) {
                result = result.substring(0, 1024);
              }
              strcpy(resultBuffer, result.c_str());
              success = true;
            }
          } else {
            // Number or boolean value
            int endPos = startPos;
            while(endPos < response.length() && response.charAt(endPos) != ',' && response.charAt(endPos) != '}') {
              endPos++;
            }
            String result = response.substring(startPos, endPos);
            result.trim();
            if(result.length() >= 1024) {
              result = result.substring(0, 1024);
            }
            strcpy(resultBuffer, result.c_str());
            success = true;
          }
        }
      } else {
        LOG0("*** ERROR: Particle getVariable failed (HTTP %d): %s\n", httpCode, http.errorToString(httpCode).c_str());
      }
      http.end();
    } // HTTPClient destroyed here

    delete secureClient;
  }
  
  // Stack usage logging
  UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
  size_t stackUsed = PARTICLE_TASK_STACK_SIZE - (stackRemaining * sizeof(StackType_t));
  LOG2("Particle getVariable task: %d/%d bytes stack used (%d bytes remaining)\n", 
       stackUsed, PARTICLE_TASK_STACK_SIZE, stackRemaining * sizeof(StackType_t));
  
  // Free task parameters (already copied callback info)
  delete task;
  task = nullptr;
  
  // Always invoke callback if it exists - callback uses homeSpanPAUSE/homeSpanRESUME for thread safety
  if(localCallback) {
    localCallback(resultBuffer, success, localUserData);
  }
  
  vTaskDelete(NULL);
}

///////////////////////////////

void ParticleConfig::callFunctionAsync(const char *functionName, const char *functionArgument,
                                       void (*callback)(int result, bool success, void *userData),
                                       void *userData) {
  
  // Validate function name length
  if(strlen(functionName) > 64) {
    LOG0("*** ERROR: Function name exceeds 64 byte limit: %s\n", functionName);
    if(callback) {
      callback(-1, false, userData);
    }
    return;
  }
  
  // Validate function argument length
  if(strlen(functionArgument) > 1024) {
    LOG0("*** ERROR: Function argument exceeds 1024 byte limit\n");
    if(callback) {
      callback(-1, false, userData);
    }
    return;
  }
  
  // Check if configured, and prompt if not
  if(!isConfigured()) {
    // Check if WiFi is connected before attempting configuration
    if(WiFi.status() != WL_CONNECTED) {
      LOG0("\n*** ERROR: WiFi not connected - cannot configure Particle.io credentials ***\n");
      LOG0("*** Please type 'W' to configure WiFi first ***\n\n");
      if(callback) {
        callback(-1, false, userData);
      }
      return;
    }
    
    LOG0("\n*** Particle.io credentials not found ***\n");
    LOG0("*** Starting Particle.io setup... ***\n\n");
    serialConfigure();
    
    // Check again after configuration attempt
    if(!isConfigured()) {
      LOG0("*** ERROR: Particle.io credentials still not configured\n");
      if(callback) {
        callback(-1, false, userData);
      }
      return;
    }
  }
  
  // Check throttle
  if(!checkThrottle("function", functionName)) {
    if(callback) {
      callback(-1, false, userData);
    }
    return;
  }
  
  // Allocate task parameters on heap
  CallFunctionTaskParams *taskParams = new CallFunctionTaskParams();
  strcpy(taskParams->apiKey, particleData.apiKey);
  strcpy(taskParams->deviceId, particleData.deviceId);
  strcpy(taskParams->functionName, functionName);
  strcpy(taskParams->functionArgument, functionArgument);
  taskParams->callback = callback;
  taskParams->userData = userData;
  
  // Create FreeRTOS task
  BaseType_t result = xTaskCreate(
    callFunctionTaskImpl,
    "particleCall",
    PARTICLE_TASK_STACK_SIZE,
    taskParams,
    1,
    NULL
  );
  
  if(result != pdPASS) {
    LOG0("*** ERROR: Failed to create async task for callFunction\n");
    if(callback) {
      callback(-1, false, userData);
    }
    delete taskParams;
  }
}

///////////////////////////////

void ParticleConfig::getVariableAsync(const char *variableName,
                                      void (*callback)(const char *result, bool success, void *userData),
                                      void *userData) {
  
  // Validate variable name length
  if(strlen(variableName) > 64) {
    LOG0("*** ERROR: Variable name exceeds 64 byte limit: %s\n", variableName);
    if(callback) {
      callback("", false, userData);
    }
    return;
  }
  
  // Check if configured, and prompt if not
  if(!isConfigured()) {
    // Check if WiFi is connected before attempting configuration
    if(WiFi.status() != WL_CONNECTED) {
      LOG0("\n*** ERROR: WiFi not connected - cannot configure Particle.io credentials ***\n");
      LOG0("*** Please type 'W' to configure WiFi first ***\n\n");
      if(callback) {
        callback("", false, userData);
      }
      return;
    }
    
    LOG0("\n*** Particle.io credentials not found ***\n");
    LOG0("*** Starting Particle.io setup... ***\n\n");
    serialConfigure();
    
    // Check again after configuration attempt
    if(!isConfigured()) {
      LOG0("*** ERROR: Particle.io credentials still not configured\n");
      if(callback) {
        callback("", false, userData);
      }
      return;
    }
  }
  
  // Check throttle
  if(!checkThrottle("variable", variableName)) {
    if(callback) {
      callback("", false, userData);
    }
    return;
  }
  
  // Allocate task parameters on heap
  GetVariableTaskParams *taskParams = new GetVariableTaskParams();
  strcpy(taskParams->apiKey, particleData.apiKey);
  strcpy(taskParams->deviceId, particleData.deviceId);
  strcpy(taskParams->variableName, variableName);
  taskParams->callback = callback;
  taskParams->userData = userData;
  
  // Create FreeRTOS task
  BaseType_t result = xTaskCreate(
    getVariableTaskImpl,
    "particleGet",
    PARTICLE_TASK_STACK_SIZE,
    taskParams,
    1,
    NULL
  );
  
  if(result != pdPASS) {
    LOG0("*** ERROR: Failed to create async task for getVariable\n");
    if(callback) {
      callback("", false, userData);
    }
    delete taskParams;
  }
}

///////////////////////////////
