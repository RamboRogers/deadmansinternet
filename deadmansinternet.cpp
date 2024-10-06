#include <Arduino.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESPping.h>
#include <M5AtomS3.h>
#include <ESPmDNS.h> // Add this line for mDNS support

//TODO

//Offline timer isn't trigger full failure or secondary repower
// Test with UDM
// 4/5 pings fail and it stays online..... lets give it another minute

// Pin Definitions
#define RELAY1 5 // G5 -> IN1
#define RELAY2 6 // G6 -> IN2

// WiFi Credentials
const char* ssid = "YOURSSID";
const char* password = "YOURSSIDKEY";

// NTP Settings
const long utcOffsetInSeconds = -18000; // UTC -5 hours for Eastern Time
const char* ntpServer = "pool.ntp.org";
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, utcOffsetInSeconds);

// HTTP Server Setup
WiFiServer server(80);

// Time Tracking Variables
unsigned long previousPingMillis = 0;
const unsigned long pingInterval = 60000; // Ping every 1 minute

unsigned long onlineStartMillis = 0;
unsigned long offlineStartMillis = 0;
bool isOffline = false;

// Scrolling IP variables
String scrollingIP = "";
int scrollPosition = 0;
unsigned long lastScrollTime = 0;
const int scrollSpeed = 100; // Milliseconds between each scroll step

// Function Prototypes
void connectToWiFi();
void initializeRelays();
void updateDisplay();
void performPingCheck();
void handleClientRequests();
String formatDurationDays(unsigned long milliseconds);
String formatDurationHMS(unsigned long milliseconds);
void logStatus(const String& message);
void updateScrollingIP();
void setupMDNS();
String getMetricsJSON();


//Display on Screen CYCLING
void displayCycling() {
    // Increase the height of the green box by 75% (midpoint between 50% and 100%)
    int boxHeight = 53; // Original 30 * 1.75
    int boxTop = AtomS3.Display.height() / 2 - boxHeight * 3 / 4; // Move up from midpoint

    AtomS3.Display.setTextColor(BLACK, ORANGE);
    AtomS3.Display.fillRect(0, boxTop, AtomS3.Display.width(), boxHeight, ORANGE);

    // Increase text size and adjust position
    AtomS3.Display.setTextSize(3);
    AtomS3.Display.setTextDatum(MC_DATUM);
    AtomS3.Display.drawString("CYCLING", AtomS3.Display.width() / 2, boxTop + boxHeight / 2);

}

void checkButtonPress() {
    AtomS3.update();
    if (AtomS3.BtnA.wasPressed()) {
        logStatus("Button pressed. Forcing device to OFFLINE mode.");
      updateScrollingIP();
      updateDisplay();
      handleClientRequests();

            isOffline = true;
            offlineStartMillis = millis();
            logStatus("Switching to OFFLINE mode and restarting network devices.");
            updateScrollingIP();
            updateDisplay();
            handleClientRequests();
            // Activate Relays to Restart Devices
            digitalWrite(RELAY1, LOW);
            digitalWrite(RELAY2, LOW);
            logStatus("Relays activated. Restarting devices...");

            for (int i = 0; i < 30; i++) {
                // Update the display every second
                displayCycling();  // Call your display update function

                // Perform a delay for 1 second
                delay(1000);  // Delay for 1 second (1000 milliseconds)
            }

            digitalWrite(RELAY1, HIGH);
            digitalWrite(RELAY2, HIGH);
            logStatus("Relays deactivated.");

    }
}

void buttonPress() {
      logStatus("GUI Button pressed. Forcing device to OFFLINE mode.");
      updateScrollingIP();
      updateDisplay();
      handleClientRequests();

      isOffline = true;
      offlineStartMillis = millis();
      logStatus("Switching to OFFLINE mode and restarting network devices.");
      updateScrollingIP();
      updateDisplay();
      handleClientRequests();
      // Activate Relays to Restart Devices
      digitalWrite(RELAY1, LOW);
      digitalWrite(RELAY2, LOW);
      logStatus("Relays activated. Restarting devices...");

      for (int i = 0; i < 30; i++) {
          // Update the display every second
          displayCycling();  // Call your display update function

          // Perform a delay for 1 second
          delay(1000);  // Delay for 1 second (1000 milliseconds)
      }

      digitalWrite(RELAY1, HIGH);
      digitalWrite(RELAY2, HIGH);
      logStatus("Relays deactivated.");

    }

void setup() {
  Serial.begin(115200);
  delay(100);

  AtomS3.begin();
  AtomS3.Display.setBrightness(50);
  AtomS3.Display.setTextSize(1);
  AtomS3.Display.setTextColor(WHITE, BLACK);
  AtomS3.Display.fillScreen(BLACK);

  logStatus("System initializing...");

  initializeRelays();
  connectToWiFi();

  logStatus("Initializing NTP client...");
  timeClient.begin();
  if (timeClient.update()) {
    logStatus("NTP time synchronized.");
  } else {
    logStatus("Failed to synchronize NTP time.");
  }

  setupMDNS();
  
  server.begin();
  logStatus("HTTP server started.");

  onlineStartMillis = millis();
  scrollingIP = WiFi.localIP().toString() + " "; // Add some spaces for smooth scrolling
  updateDisplay();
}

unsigned long previousDisplayUpdateMillis = 0;
const unsigned long displayUpdateInterval = 1000; // Faster updates for smoother scrolling

unsigned long lastResetTime = 0;
const unsigned long REBOOT_THRESHOLD = 60 * 60 * 1000; // 60 minutes in milliseconds

//////Primary Loop
void loop() {
    checkButtonPress();

    unsigned long currentMillis = millis();

    if (currentMillis - previousPingMillis >= pingInterval) {
        previousPingMillis = currentMillis;
        performPingCheck();
    }

    if (currentMillis - previousDisplayUpdateMillis >= displayUpdateInterval) {
        previousDisplayUpdateMillis = currentMillis;
        updateScrollingIP();
        updateDisplay();
    }

    handleClientRequests();

    // Check if we need to reboot after 60 minutes of continuous resets
    if (millis() - lastResetTime > REBOOT_THRESHOLD) {
        logStatus("60 minutes of continuous resets. Rebooting device.");
        ESP.restart();
    }
}

void connectToWiFi() {
  logStatus("Connecting to WiFi...");
  AtomS3.Display.fillScreen(BLUE);
  AtomS3.Display.setCursor(10, 20);
  AtomS3.Display.setTextSize(2); // Keep text size at 2
  AtomS3.Display.print("Connecting to WiFi...");

  WiFi.begin(ssid, password);

  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
    delay(1000);
    AtomS3.Display.print(".");
    retryCount++;
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    logStatus("Connected to WiFi.");
    logStatus("IP Address: " + WiFi.localIP().toString());
  } else {
    logStatus("Failed to connect to WiFi.");
    AtomS3.Display.fillScreen(RED);
    AtomS3.Display.setCursor(10, 20);
    AtomS3.Display.print("WiFi Connection Failed!");
  }
}

void initializeRelays() {
  logStatus("Initializing relays...");
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  logStatus("Relays initialized and set to OFF.");
}

void updateScrollingIP() {
  if (millis() - lastScrollTime > scrollSpeed) {
    lastScrollTime = millis();
    scrollPosition++;
    if (scrollPosition >= scrollingIP.length()) {
      scrollPosition = 0;
    }
  }
}

void updateDisplay() {
  AtomS3.Display.fillScreen(BLACK);

  // Calculate the position for the IP address
  int boxHeight = 53; // Height of the green box
  int boxTop = AtomS3.Display.height() / 2 - boxHeight * 3 / 4; // Top of the green box
  int ipY = boxTop * 0.6; // Move IP down by 60% of the space above the green box

  // Display Scrolling IP Address
  AtomS3.Display.setTextSize(2); // Keep text size at 2
  AtomS3.Display.setTextColor(WHITE, BLACK);
  AtomS3.Display.setCursor(0, ipY);
  String visibleIP = scrollingIP.substring(scrollPosition) + scrollingIP.substring(0, scrollPosition);
  visibleIP = visibleIP.substring(0, 10); // Keep substring size at 10
  AtomS3.Display.print(visibleIP);

  // Display Connection Status and Duration
  if (isOffline) {
    unsigned long offlineDuration = millis() - offlineStartMillis;

    // Increase the height of the red box by 75% (midpoint between 50% and 100%)
    int boxHeight = 53; // Original 30 * 1.75
    int boxTop = AtomS3.Display.height() / 2 - boxHeight * 3 / 4; // Move up from midpoint

    AtomS3.Display.setTextColor(BLACK, RED);
    AtomS3.Display.fillRect(0, boxTop, AtomS3.Display.width(), boxHeight, RED);

    // Increase text size and adjust position
    AtomS3.Display.setTextSize(3);
    AtomS3.Display.setTextDatum(MC_DATUM);
    AtomS3.Display.drawString("OFFLINE", AtomS3.Display.width() / 2, boxTop + boxHeight / 2);

    // Center and increase size of duration information
    AtomS3.Display.setTextColor(WHITE, BLACK);

    // Display days with larger text size
    AtomS3.Display.setTextSize(3);
    String daysText = formatDurationDays(offlineDuration);
    AtomS3.Display.setTextDatum(CC_DATUM);
    AtomS3.Display.drawString(daysText, AtomS3.Display.width() / 2, AtomS3.Display.height() - 30);

    // Display time with slightly smaller text size
    AtomS3.Display.setTextSize(2);
    String timeText = formatDurationHMS(offlineDuration);
    AtomS3.Display.drawString(timeText, AtomS3.Display.width() / 2, AtomS3.Display.height() - 10);
  } else {
    unsigned long onlineDuration = millis() - onlineStartMillis;

    // Increase the height of the green box by 75% (midpoint between 50% and 100%)
    int boxHeight = 53; // Original 30 * 1.75
    int boxTop = AtomS3.Display.height() / 2 - boxHeight * 3 / 4; // Move up from midpoint

    AtomS3.Display.setTextColor(BLACK, GREEN);
    AtomS3.Display.fillRect(0, boxTop, AtomS3.Display.width(), boxHeight, GREEN);

    // Increase text size and adjust position
    AtomS3.Display.setTextSize(3);
    AtomS3.Display.setTextDatum(MC_DATUM);
    AtomS3.Display.drawString("ONLINE", AtomS3.Display.width() / 2, boxTop + boxHeight / 2);

    // Center and increase size of duration information
    AtomS3.Display.setTextColor(WHITE, BLACK);

    // Display days with larger text size
    AtomS3.Display.setTextSize(3);
    String daysText = formatDurationDays(onlineDuration);
    AtomS3.Display.setTextDatum(CC_DATUM);
    AtomS3.Display.drawString(daysText, AtomS3.Display.width() / 2, AtomS3.Display.height() - 30);

    // Display time with slightly smaller text size
    AtomS3.Display.setTextSize(2);
    String timeText = formatDurationHMS(onlineDuration);
    AtomS3.Display.drawString(timeText, AtomS3.Display.width() / 2, AtomS3.Display.height() - 10);
  }
}



//Display on Screen PINGING
void displayPinging() {
    // Increase the height of the green box by 75% (midpoint between 50% and 100%)
    int boxHeight = 53; // Original 30 * 1.75
    int boxTop = AtomS3.Display.height() / 2 - boxHeight * 3 / 4; // Move up from midpoint

    AtomS3.Display.setTextColor(WHITE, BLUE);
    AtomS3.Display.fillRect(0, boxTop, AtomS3.Display.width(), boxHeight, BLUE);

    // Increase text size and adjust position
    AtomS3.Display.setTextSize(3);
    AtomS3.Display.setTextDatum(MC_DATUM);
    AtomS3.Display.drawString("PINGING", AtomS3.Display.width() / 2, boxTop + boxHeight / 2);

}

//TODO: This function is locking up the screen.  We need to fix
unsigned long lastRelayActionTime = 0; // Global variable to track the last relay action time

void performPingCheck() {
  logStatus("Performing ping check...");
  int successCount = 0;
  for (int i = 0; i < 5; i++) {
    updateScrollingIP();
    displayPinging();
    handleClientRequests();
    checkButtonPress();
    if (Ping.ping("8.8.8.8")) {
      successCount++;
      logStatus("Ping " + String(i+1) + ": Success");
    } else {
      logStatus("Ping " + String(i+1) + ": Failure");
    }
    updateScrollingIP();
    displayPinging();
    handleClientRequests();
    checkButtonPress();
  }

  unsigned long currentTime = millis();
  bool fiveMinutesPassed = (currentTime - lastRelayActionTime) >= 300000; // 300000 ms = 5 minutes

  if (successCount > 0) {
    if (isOffline) {
      logStatus("Internet connection restored. Switching to ONLINE mode.");
      isOffline = false;
      onlineStartMillis = currentTime;
      lastRelayActionTime = currentTime; // Reset the timer when coming back online
    } else {
      logStatus("Internet connection is stable.");
    }
    updateScrollingIP();
    updateDisplay();
    handleClientRequests();
  } else {
    if (!isOffline) {
      logStatus("All pings failed. Switching to OFFLINE mode.");
      isOffline = true;
      offlineStartMillis = currentTime;
      lastRelayActionTime = currentTime;
    } else if (fiveMinutesPassed) {
      logStatus("5 minutes passed while offline. Restarting network devices.");
      lastRelayActionTime = currentTime;
      lastResetTime = currentTime; // Update the last reset time
      
      // Activate Relays to Restart Devices
      digitalWrite(RELAY1, LOW);
      digitalWrite(RELAY2, LOW);

      //30 second power down
      logStatus("Relays activated. Restarting devices...");
      for (int i = 0; i < 30; i++) {
        displayCycling();
        delay(1000);
      }
      digitalWrite(RELAY1, HIGH);
      digitalWrite(RELAY2, HIGH);
      logStatus("Relays deactivated.");
    } else {
      logStatus("Still offline. Monitoring continues.");
    }
  }
  
  updateScrollingIP();
  updateDisplay();
  handleClientRequests();
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: Arial, sans-serif;
      background-color: #1e1e1e;
      color: #ffffff;
      margin: 0;
      padding: 20px;
    }
    .container {
      max-width: 600px;
      margin: 0 auto;
      background-color: #2d2d2d;
      border-radius: 10px;
      padding: 20px;
      box-shadow: 0 0 10px rgba(0,0,0,0.5);
    }
    h1 {
      color: #4CAF50;
      text-align: center;
      margin-bottom: 30px;
    }
    .data-row {
      display: flex;
      justify-content: space-between;
      margin-bottom: 15px;
      padding: 10px;
      background-color: #3d3d3d;
      border-radius: 5px;
    }
    .label {
      font-weight: bold;
    }
    .value {
      color: #4CAF50;
    }
    .button-container {
      display: flex;
      justify-content: space-between;
      margin-top: 20px;
    }
    .button {
      flex: 1;
      padding: 10px;
      color: white;
      border: none;
      border-radius: 5px;
      font-size: 16px;
      cursor: pointer;
      transition: background-color 0.3s;
    }
    .reboot-button {
      background-color: #4CAF50;
      margin-right: 10px;
    }
    .reboot-button:hover {
      background-color: #45a049;
    }
    .force-offline-button {
      background-color: #f44336;
      margin-left: 10px;
    }
    .force-offline-button:hover {
      background-color: #d32f2f;
    }
  </style>
  <script>
    function updateData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('status').textContent = data.status;
          document.getElementById('uptime').textContent = formatDuration(data.uptime);
          document.getElementById('currentDuration').textContent = formatDuration(data.current_duration);
          document.getElementById('totalOnlineTime').textContent = formatDuration(data.total_online_time);
          document.getElementById('totalOfflineTime').textContent = formatDuration(data.total_offline_time);
          document.getElementById('ip').textContent = data.ip;
          document.getElementById('hostname').textContent = data.hostname;
        });
    }
    function formatDuration(seconds) {
      let minutes = Math.floor(seconds / 60);
      let hours = Math.floor(minutes / 60);
      let days = Math.floor(hours / 24);
      
      return `${days}d ${hours%24}h ${minutes%60}m ${seconds%60}s`;
    }
    function rebootDevice() {
      if (confirm('Are you sure you want to reboot the device?')) {
        fetch('/reboot')
          .then(response => response.json())
          .then(data => {
            alert('Device is rebooting. Please wait a moment before refreshing the page.');
          })
          .catch(error => {
            console.error('Error:', error);
            alert('An error occurred while trying to reboot the device.');
          });
      }
    }
    function forceOffline() {
      if (confirm('Are you sure you want to force the device offline?')) {
        fetch('/forceoffline')
          .then(response => response.json())
          .then(data => {
            alert('Device is being forced offline. The status will update shortly.');
          })
          .catch(error => {
            console.error('Error:', error);
            alert('An error occurred while trying to force the device offline.');
          });
      }
    }
    setInterval(updateData, 5000);  // Update every 5 seconds
  </script>
</head>
<body onload="updateData()">
  <div class="container">
    <h1>Internet Monitor: %HOSTNAME%</h1>
    <div class="data-row">
      <span class="label">Status:</span>
      <span class="value" id="status">--</span>
    </div>
    <div class="data-row">
      <span class="label">Uptime:</span>
      <span class="value" id="uptime">--</span>
    </div>
    <div class="data-row">
      <span class="label">Current Duration:</span>
      <span class="value" id="currentDuration">--</span>
    </div>
    <div class="data-row">
      <span class="label">Total Online Time:</span>
      <span class="value" id="totalOnlineTime">--</span>
    </div>
    <div class="data-row">
      <span class="label">Total Offline Time:</span>
      <span class="value" id="totalOfflineTime">--</span>
    </div>
    <div class="data-row">
      <span class="label">IP Address:</span>
      <span class="value" id="ip">--</span>
    </div>
    <div class="data-row">
      <span class="label">Hostname:</span>
      <span class="value" id="hostname">--</span>
    </div>
    <div class="button-container">
      <button class="button reboot-button" onclick="rebootDevice()">Reboot Device</button>
      <button class="button force-offline-button" onclick="forceOffline()">Force Offline</button>
    </div>
  </div>
</body>
</html>
)rawliteral";

void handleClientRequests() {
  WiFiClient client = server.available();
  if (client) {
    logStatus("Client connected.");
    String request = client.readStringUntil('\r');
    client.flush();

    String response;
    if (request.indexOf("/data") != -1) {
      // JSON endpoint for metrics
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println();
      response = getMetricsJSON();
    } else if (request.indexOf("/reboot") != -1) {
      // Reboot endpoint
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println();
      response = "{\"status\":\"rebooting\"}";
      client.print(response);
      client.stop();
      logStatus("Reboot requested via web endpoint.");
      delay(1000);  // Give some time for the response to be sent
      ESP.restart();  // Reboot the device
    } else if (request.indexOf("/forceoffline") != -1) {
      // Force offline endpoint
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println();
      response = "{\"status\":\"forcing offline\"}";
      client.print(response);
      client.stop();
      logStatus("Force offline requested via web endpoint.");
      buttonPress(); // Call the buttonPress routine
    } else {
      // HTML Response
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println();
      response = String(INDEX_HTML);
      response.replace("%HOSTNAME%", "deadman.local");
    }
    client.print(response);
    client.stop();
    logStatus("Response sent to client.");
  }
}

// Update formatDurationDays to return a more concise string
String formatDurationDays(unsigned long milliseconds) {
  unsigned long days = milliseconds / (1000 * 60 * 60 * 24);
  char buffer[10];
  sprintf(buffer, "%lud", days);
  return String(buffer);
}

String formatDurationHMS(unsigned long milliseconds) {
  unsigned long seconds = (milliseconds / 1000) % 60;
  unsigned long minutes = (milliseconds / (1000 * 60)) % 60;
  unsigned long hours = (milliseconds / (1000 * 60 * 60)) % 24;

  char buffer[20];
  sprintf(buffer, "%02lu:%02lu:%02lu", hours, minutes, seconds);
  return String(buffer);
}

void logStatus(const String& message) {
  String timestamp = timeClient.getFormattedTime();
  Serial.println("[" + timestamp + "] " + message);
}

void setupMDNS() {
  if (MDNS.begin("deadman")) {
    logStatus("mDNS responder started. Hostname: deadman.local");
    MDNS.addService("http", "tcp", 80);
  } else {
    logStatus("Error setting up mDNS responder!");
  }
}

String getMetricsJSON() {
  String metrics = "{";
  metrics += "\"status\":\"" + String(isOffline ? "OFFLINE" : "ONLINE") + "\",";
  metrics += "\"uptime\":" + String(millis() / 1000) + ",";
  metrics += "\"current_duration\":" + String((isOffline ? millis() - offlineStartMillis : millis() - onlineStartMillis) / 1000) + ",";
  metrics += "\"total_online_time\":" + String((isOffline ? onlineStartMillis : millis() - onlineStartMillis) / 1000) + ",";
  metrics += "\"total_offline_time\":" + String((isOffline ? millis() - offlineStartMillis : offlineStartMillis) / 1000) + ",";
  metrics += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  metrics += "\"hostname\":\"deadman.local\"";
  metrics += "}";
  return metrics;
}