/*
  ESP8266 (ESP-01) HTML-MQTT Geiger Counter 

  This sketch allows you to connect an ESP-01 to a Geiger Counter and
  get the Counts Per Minute (CPM) via a webpage or pushed via MQTT.

  CPM Counter:
  * Counts every time the input pin is pulled low (Interrupt).
  * Counts are logged every 60 seconds.
  * 15 minutes of counts are kept and available via the Web Interface
  * 1, 5, and 15 minute averages are calculated every minute
  * All of these values are only kept in RAM and are lost at reboot.

  Web Interface:
  * Index (Home) Page: Includes links to JSON API endpoints
  * API Endpoints: Present CPM counts in a JSON format
  * You can navigate these endpoints with a webbrowser by entering in
    the IP of your ESP-01 into the address bar.

  MQTT:
  * Every 60 seconds, when the CPM is logged, the count total is
    published to the defined MQTT server and topic below.
  * Note: No timestamps are included in the published payload.


    -------------------------[Pin Connections]--------------------------
  The Geiger Tube kit has 3 connections, 5V, GND, and INT.
    * 5V  - Connect to 3.3v regulator to power ESP-01
    * GND - Connect to 3.3v regulator ground and ESP-01 ground
    * INT - Connect to GPIO Pin2 on the ESP-01 (geigerInputPin variable)

  # NOTE: INT on my geiger kit included a pullup resistor.


  LED_BUILTIN: The ESP-01 includes a built in LED that I use to show
                WiFi connectivity status. LED on = Connected


    ------------------[KRACK Vulnerability Mitigation]------------------
  I HIGHLY recommend the ESP8266 Board Manager version of 2.4.0-rc2
  (or newer) to avoid KRACK WPA vulnerability. You can use this board
  manager url string to get it:
    https://github.com/esp8266/Arduino/releases/download/2.4.0-rc2/package_esp8266com_index.json


    -------------------[Sketch Info From Geiger Kit]--------------------
    I have included the comment block from the RadiationD-v1.1(CAJOE)
  Geiger Counter Kit below:

  * Serial Communication for Radiation Detector Arduino Compatible DIY Kit ver 2.01 or higher
  * http://radiohobbystore.com/radiation-detector-geiger-counter-diy-kit-second-edition.html
  * Allow to connect the kit to computer and use the kit with Radiation Logger PC software
  * http://radiohobbystore.com/radiation-logger/
  * [The Original] Arduino sketch written by Alex Boguslavsky RH Electronics; mail: support@radiohobbystore.com
  * CPM counting algorithm is very simple, it just collect GM Tube events during presettable log period.
  * For radiation monitoring station it's recommended to use 30-60 seconds logging period. Feel free to modify
  * or add functions to this sketch. This Arduino software is an example only for education purpose without any
  * warranty for precision radiation measurements. You are fully responsible for your safety in high
  * radiation area!!
  * ------------------------------------------------------------------
  * WHAT IS CPM?
  * CPM (or counts per minute) is events quantity from Geiger Tube you get during one minute. Usually it used to 
  * calculate a radiation level. Different GM Tubes has different quantity of CPM for background. Some tubes can
  * produce about 10-50 CPM for normal background, other GM Tube models produce 50-100 CPM or 0-5 CPM for same
  * radiation level. Please refer your GM Tube datasheet for more information. Just for reference here, SBM-20 can
  * generate about 10-50 CPM for normal background.


    ------------------------[Sketch Information]------------------------
  Created November 25 2018
  * By Adam Melton (Dokument)

  Modified January 28 2018
  * By Adam Melton

  Schematics, pictures, and more information can be found:
  * www.AdamMelton.com/esp8266-geiger-counter.html
  * www.github.com/Dokument/esp8266-geiger-counter
*/

// Expose Espressif SDK functionality - wrapped in ifdef so that it still
// compiles on other platforms
#ifdef ESP8266
  extern "C" {
    #include "user_interface.h"
  }
#endif

#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SPI.h>

// Here are your wifi credentials. There are better methods of saving them but this one is simple.
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// You can enable or disable MQTT publishing with the mqtt_enabled value
const bool mqtt_enabled = true;
const char* mqtt_server = "YOUR_MQTT_BROKER_IP";
const int mqtt_port = 1883;
const char* mqtt_topic = "radiation";

// This saves our hostname from the ESP. uses ESP_<lasthalfmacaddress> as hostname
const char* my_hostname = wifi_station_get_hostname();

ESP8266WebServer server(80);
WiFiClient wifiClient;
PubSubClient client(wifiClient);

/*
  -----------------[Declare Geiger Counter Variables]-----------------
  geigerInputPin: Which pin for the low interrupt signal
  counts: The current GM Tube event count. Cleared every minute.
  cpmHistory: Array of 15 unsigned logs. This is the last 15 CPM.
  cpmPointer: Points to the last logged index value of cpmHistory. CPM
              are only logged at the end of the minute.
  previousMillis: Keeps track of the last time we saved the CPM total.
 
  Note: "previousMillis" is reset to 0 when the millis() function
        resets to 0 (Approximately once per 50 days).
 */
const int geigerInputPin = 2;  // This is the Geiger INT pin
unsigned long counts;
unsigned long cpmHistory[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
unsigned int cpmPointer;
unsigned long previousMillis;
/*  ------------------------------------------------------------------*/

/*
  --------------------------[Block Strings]---------------------------
  Below is the long HTML code for the home page in block strings.
*/
const char* index_html_str = "<!DOCTYPE html>"
"<html>"
"<head>"
"<meta charset=\"UTF-8\">"
"<style>"
"html {font-family: Geneva;}"
"body {background-color: black;color: yellow;}"
"ul   {margin-top: 2px;}"
"a:link {"
"      color: white;"
"      background-color: transparent;"
"      text-decoration: underline;"
"     }"
"a:visited {"
"      color: white;"
"      background-color: transparent;"
"      text-decoration: underline;"
"     }"
"div  {margin: auto;"
"   max-width: 750px;"
"   min-width: 500px;"
"  }"
"</style>"
"<title>ESP8266 HTML-MQTT Geiger Counter</title>"
"</head>"
"<body>"
"<div>"
"<h1 style=\"text-align: center\">ESP8266 HTML-MQTT Geiger Counter</h1>"
"<hr>"
"<p>"
"This web interface provides access to the count of Geiger Tube Events. "
"Every minute, the Counts Per Minute, CPM, are saved internally and published to MQTT. "
"CPM totals are kept for the past 15 minutes and used to calculate 1 minute, 5 minute, and 15 minute averages. "
"This history is lost at power reset. Use the links below to access the CPM. "
"</p>"
"<table style=\"width: 100%; border-spacing: 0;\">"
"<tr>"
"<th style=\"text-align: left;\">"
"<b>Links:</b>"
"<ul>"
"<li><a href=\"/mqtt\">MQTT Status</a></li>"
"<li><a href=\"/current\">Current Minute CPM Count</a></li>"
"<li><a href=\"/last\">Last Minute CPM Count</a></li>"
"<li><a href=\"/history\">15 Minute CPM History</a></li>"
"<li><a href=\"/averages\">CPM 1/5/15 Minute Averages</a></li>"
"</ul>"
"</th>"
"<th style=\"text-align: right; font-size: 12em; line-height: 0.75;\">"
"&#9762"
"</th>"
"</tr>"
"</table>"
"<hr>"
"<p style=\"text-align: center; font-size: 0.9em;\">"
"<a href=\"https://github.com/Dokument/esp8266-geiger-counter\">github.com</a> | "
"<a href=\"http://www.adammelton.com/esp8266-geiger-counter.html\">adammelton.com</a>"
"</p>"
"</div>"
"</body>"
"</html>";
/*  ------------------------------------------------------------------*/


void setup(void){
  // Setup onboard LED
  pinMode(LED_BUILTIN, OUTPUT);

  // Turn off onboard LED
  digitalWrite(LED_BUILTIN, HIGH);  // OFF

  // Start WIFI with defined credentials
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for WIFI connection before proceeding
  while (WiFi.status() != WL_CONNECTED) {
    // Flash led 2 times to let user know WIFI issue.
    flashLED(2);
  }
  digitalWrite(LED_BUILTIN, LOW);  // Turn on LED

  // Start every thing else.
  startCounter();
  startWebserver();
  startMqtt();
}

void loop(void){
  // Handle webserver requests
  server.handleClient();

  if (cpmUpdate()) {
    // Should only return true once per minute.
    // MQTT - Check connectivity, reconnect if needed, and publish
    if (mqtt_enabled == true && mqttConnect() == true) { 
      client.publish(mqtt_topic, String(cpmHistory[cpmPointer]).c_str());
    }
  }
  // Loop the MQTT client, but not too frequently (causes issues).
  client.loop();
  delay(100);
}

void startCounter(){
  // Starts CPM counter operations.
  counts = 0;
  cpmPointer = 0;

  // Pin is connected to Geiger Counter. We count each time it transitions to ground (falling).
  attachInterrupt(geigerInputPin, tubeImpulse, FALLING);
}

void startWebserver(){
  server.on("/", serveRoot);             // Home page
  server.on("/mqtt", serveMqtt);         // MQTT status page
  server.on("/current", serveCurrent);   // Current CPM
  server.on("/last", serveLast);         // Last CPM
  server.on("/history", serveHistory);   // 15 minute CPM history
  server.on("/averages", serveAverages); // 1,5,15 minute CPM avgs
  server.onNotFound(serveNotFound);      // 404 Error

  server.begin();                        // Start webserver
}

void startMqtt() {
  // MQTT - connect to the defined mqtt broker server
  if (mqtt_enabled == true) {
    client.setServer(mqtt_server, mqtt_port);
    mqttConnect();
    client.loop();
  }
}

void tubeImpulse() {       
  // Geiger Tube Event occurred. Increment counter up by one.
  counts++;
}

bool mqttConnect() {
  // Used to connect to the MQTT server.
  // Returns connected status as boolean
  int max_retry_attempts = 2;

  if (client.connected()) {
    return true;  // Already connected
  }

  int retry_counter = 0;
  while (!client.connected()) {
    digitalWrite(LED_BUILTIN, HIGH);  // Turn off LED to indicate error

    retry_counter += 1;
    if (retry_counter > max_retry_attempts) {
      // Stop trying
      break;
    }

      // Attempt to connect using the hostname as identifier
      if (client.connect(my_hostname)) {
        // Connected, now subscribe to topic
        client.subscribe(mqtt_topic);
        digitalWrite(LED_BUILTIN, LOW);  // Turn on LED
        return true;
      } else {
        // FLash the LED 3 times to let user know MQTT error:
        flashLED(3);
    }
  }
  return false;  // Not connected
}

bool cpmUpdate() {
  // Compare the current time to the last time we saved the CPM count.
  // Returns bool value outcome of check (True = updated)

  unsigned long currentMillis = millis();
  if(previousMillis > currentMillis){
    // millis() value resets to 0 every ~50 days. We have to handle that here. 
    previousMillis = 0;
  }

  // Check if 60 seconds has passed
  if(currentMillis > previousMillis + 60000){
    // Instead of setting previous to equal current, we increment by 60 sec to keep the time the same. Otherwise it will drift.
    previousMillis = previousMillis + 60000;

    // Increment pointer to oldesst value
    // We increment here so that pointer is newest value between loops
    if(cpmPointer == 15){
      // Loop back to first index if we reached the end
      cpmPointer = 0;
    }
    else {
      // Otherwise add one
      cpmPointer += 1;
    }

    // Save count to pointer location and clear counter
    cpmHistory[cpmPointer] = counts;
    counts = 0;

    return true;
  }
  return false;
}

void flashLED(int flash_count) {
  // Flashes the LED flash_count number of times as status indicator to user
  // Finishes with LED in the off state. Since we assume an error (on = okay).
  digitalWrite(LED_BUILTIN, HIGH);  // OFF

  // Outer loop always loops 2 times to help the user see it.
  for (int loop_count=0; loop_count < 2; loop_count++) {
    for (int i=0; i < flash_count; i++) {
      delay(125);
      digitalWrite(LED_BUILTIN, LOW);  // ON
      delay(125);
      digitalWrite(LED_BUILTIN, HIGH);  // OFF
    }
      delay(1000);
    }
  digitalWrite(LED_BUILTIN, HIGH);  // OFF
}

/*
  ----------------------------[Web Pages]-----------------------------
  Below are the functions for each of the urls from startWebserver.
*/
void serveRoot() {
  // Return the following HTML when someone navigates to homepage
  server.send(200, "text/html", index_html_str);
}

void serveMqtt() {
  // Returns json with status of MQTT connectivity
  String jsonString = "{ \"mqtt_enabled\": \"" + String(mqtt_enabled) + "\"";
  jsonString +=  ", \"state\": \"" + String(client.state()) + "\"";
  jsonString +=  ", \"mqtt_server\": \"" + String(mqtt_server) + "\"";
  jsonString +=  ", \"mqtt_port\": \"" + String(mqtt_port) + "\"";
  jsonString +=  ", \"mqtt_topic\": \"" + String(mqtt_topic) + "\"";
  jsonString += " }";

  // Note:We send as "application/json" instead of "text/plain"
  server.send(200, "application/json", jsonString);
}

void serveCurrent() {
  // Returns json with current CPM count before totaled (<60s).
  String jsonString = "{ \"current\": ";
  jsonString += String(counts);
  jsonString += " }";

  // Note:We send as "application/json" instead of "text/plain"
  server.send(200, "application/json", jsonString);
}

void serveLast() {
  // Returns json with most recent CPM count total.
  String jsonString = "{ \"last\": ";
  jsonString += String(cpmHistory[cpmPointer]);
  jsonString += " }";

  // Note:We send as "application/json" instead of "text/plain"
  server.send(200, "application/json", jsonString);
}

void serveHistory() {
  // Return the full history (last 15 minutes) of CPM events in a list, JSON formatted.

  String jsonString = "{ \"history\": [";

  // Process pointer buffer in order (newest to oldest) and save to JSON output, comma separated
  unsigned int currentPointer = cpmPointer;
  for (int i = 0; i < 15; i++) {
    jsonString += String(cpmHistory[currentPointer]);
    
    if(i != 14){
      // Only add commas if not the last element in the output list.
      jsonString += ", ";
    }

    // Decriment pointer to loop through array in order of newest to oldest
    if(currentPointer == 0){
      // We reached the first element so loop back to the end
      currentPointer = 14;
    }
    else
    {
      currentPointer -= 1;
    }
  }

  jsonString += "] }";

  // Note:We send as "application/json" instead of "text/plain"
  server.send(200, "application/json", jsonString);
}

void serveAverages() {  
  // Return a list of the 1, 5, and 15 minute averages. JSON formatted
  // NOTE: Only 5 and 15 minutes are actual averages, 1 minute is just the newest cpmHistory value.

  unsigned long countTotal = 0;
  float oneMinuteAverage = cpmHistory[cpmPointer];
  float fiveMinuteAverage;
  float fifteenMinuteAverage;

  unsigned int currentPointer = cpmPointer;
  /*
      The history array is 15 values but we overwrite the oldest value
      with the newest value, so index 0 isn't necessarily the newest
      data. We use the currentPointer value to point to the newest data.
  */
  for (int i = 0; i < 15; i++) {
    countTotal += cpmHistory[currentPointer];

    if (i == 4) {
        // Calculate and save 5 minute avg
        fiveMinuteAverage = countTotal / 5;
    }
    else if (i == 14) {
        // Calculate and save 15 minute avg
        fifteenMinuteAverage = countTotal / 15;
    }

    // Decriment to loop through array in order of newest to oldest
    if (currentPointer == 0) {
        // We reached the first element so loop back to the end
        currentPointer = 14;
    }
    else {
        currentPointer -= 1;
    }
  }

  // Format into JSON output
  String jsonString = "{ \"averages\": [";
  jsonString += String(oneMinuteAverage) + ", ";
  jsonString += String(fiveMinuteAverage) + ", ";
  jsonString += String(fifteenMinuteAverage);
  jsonString += "] }";

  // Note:We send as "application/json" instead of "text/plain"
  server.send(200, "application/json", jsonString);
}

void serveNotFound(){
  // 404 Error
  // Web server received a request for a page that doesn't exist.
  
  String html = "File Not Found\n\n";
  html += "URI: ";
  html += server.uri();
  html += "\nMethod: ";
  html += (server.method() == HTTP_GET)?"GET":"POST";
  html += "\nArguments: ";
  html += server.args();
  html += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    html += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", html);
}
/*  ------------------------------------------------------------------*/
