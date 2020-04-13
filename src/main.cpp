/*
  ESP8266 fog machine controller
  See github for more info https://github.com/larsenglund/fogger
*/

#include <Arduino.h>
#include <math.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <FS.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <Fonts/Picopixel.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <MAX31855.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

// which analog pin to connect
#define THERMISTORPIN A0  
// resistance at 25 degrees C
#define THERMISTORNOMINAL 10000      
// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 25   
// how many samples to take and average, more takes longer
// but is more 'smooth'
#define NUMSAMPLES 5
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3892
// the value of the 'other' resistor
#define SERIESRESISTOR 47090  

#define BUTTON_PIN D3
#define PUMP_PIN D0
#define HEATER_PIN D4

int samples[NUMSAMPLES];

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

volatile bool update_text = true;
volatile bool heater = false;
volatile bool pump = false;
volatile bool button = false;
volatile bool prev_button = false;
volatile bool wifi_button = false;
volatile bool connected = false;
volatile int num_connected = 0;
volatile float fog_temp = 0.0;
volatile float sys_temp = 0.0;
volatile float fogjuice = 0.0;
float fog_upper_temp = 240.0;
float fog_lower_temp = 230.0;
uint32_t test_timestamp;
int prev_x = 0;
int speed = 16; // pixels per second
int prev_timestamp = -1;
int dir = 1;
char buff[16];
int16_t _x1, _y1;
uint16_t w, h;
int32_t rawData = 0;
int delay_count = 0;

MAX31855 myMAX31855(D8);

const char *ssid = "FOGGER-AP";
const char *password =  "fogjuice";
const char *msg_toggle_led = "toggleLED";
const char *msg_get_led = "getLEDState";
const int dns_port = 53;
const int http_port = 80;
const int ws_port = 1337;

IPAddress apIP(192, 168, 4, 1);

DNSServer dnsServer;
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(1337);
char msg_buf[128];
int led_state = 0;

void updateText();
void drawAnimation();
void onWebSocketEvent(uint8_t client_num,
                      WStype_t type,
                      uint8_t * payload,
                      size_t length);
void onIndexRequest(AsyncWebServerRequest *request);
void onPageNotFound(AsyncWebServerRequest *request);

int averageAnalogRead();
float readNTCTemp();


void setup() {
  Serial.begin(115200);

  if( !SPIFFS.begin()){
    Serial.println("Error mounting SPIFFS");
    while(1);
  }

  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
      Serial.print(dir.fileName());
      if(dir.fileSize()) {
          File f = dir.openFile("r");
          Serial.println(f.size());
      }
  }

  // Start access point
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, password);
  
  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  dnsServer.start(dns_port, "*", apIP);

  // Print our IP address
  Serial.println();
  Serial.println("AP running");
  Serial.print("My IP address: ");
  Serial.println(WiFi.softAPIP());

  // On HTTP request for root, provide index.html file
  server.on("/", HTTP_GET, onIndexRequest);
  server.on("/fogger", HTTP_GET, onIndexRequest);
  server.on("/generate_204", HTTP_GET, onIndexRequest);
  server.on("/gen_204", HTTP_GET, onIndexRequest);
  server.on("/L0", HTTP_GET, onIndexRequest);
  server.on("/L2", HTTP_GET, onIndexRequest);
  server.on("/ALL", HTTP_GET, onIndexRequest);
  server.on("/bag", HTTP_GET, onIndexRequest);

  server.on("/btn", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/btn.png", "image/png");
  });
  server.on("/btn_pressed", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/btn_pressed.png", "image/png");
  });
 
  // Handle requests for pages that do not exist
  server.onNotFound(onPageNotFound);
 
  // Start web server
  server.begin();
 
  // Start WebSocket server and assign callback
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.setRotation(1);
  display.setFont(&Picopixel);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.clearDisplay();

  myMAX31855.begin();
  while (myMAX31855.getChipID() != MAX31855_ID)
  {
    Serial.println(F("MAX31855 error")); //(F()) saves string to flash & keeps dynamic memory free
    delay(5000);
  }
  Serial.println(F("MAX31855 OK"));

  switch (myMAX31855.detectThermocouple())
  {
    case MAX31855_THERMOCOUPLE_SHORT_TO_VCC:
      Serial.println(F("Thermocouple short to VCC"));
      break;

    case MAX31855_THERMOCOUPLE_SHORT_TO_GND:
      Serial.println(F("Thermocouple short to GND"));
      break;

    case MAX31855_THERMOCOUPLE_NOT_CONNECTED:
      Serial.println(F("Thermocouple not connected"));
      break;

    case MAX31855_THERMOCOUPLE_UNKNOWN:
      Serial.println(F("Thermocouple unknown error, check spi cable"));
      break;
  }

  if (myMAX31855.detectThermocouple() == MAX31855_THERMOCOUPLE_OK) {
    rawData = myMAX31855.readRawData();

    Serial.print(F("Chip ID: "));
    Serial.println(myMAX31855.getChipID(rawData));

    Serial.print(F("Cold Junction: "));
    Serial.println(myMAX31855.getColdJunctionTemperature(rawData));

    Serial.print(F("Thermocouple: "));
    Serial.println(myMAX31855.getTemperature(rawData));

    fog_temp = myMAX31855.getTemperature(rawData);
  }

  Serial.print("NTC RAW: ");
  Serial.println(analogRead(A0));
  Serial.print("NTC: ");
  sys_temp = readNTCTemp();
  Serial.println(sys_temp);

  pinMode(BUTTON_PIN,INPUT_PULLUP);
  pinMode(PUMP_PIN,OUTPUT);
  pinMode(HEATER_PIN,OUTPUT);

  test_timestamp = millis()+1000;
}

void loop() {

  if (update_text) {
    update_text = false;
    display.clearDisplay();
    updateText();
    sprintf(msg_buf, "%d %d %d %d %d %d", (int)fog_temp, (int)sys_temp, heater, pump, (int)fogjuice, button);
    Serial.printf("Broadcasting: %s\n", msg_buf);
    webSocket.broadcastTXT(msg_buf);
    //display.drawRect(0, 0, display.width(), display.height(), SSD1306_WHITE); // draw fullscreen rect to help align the deplay in its enclosure
  }

  dnsServer.processNextRequest();
  webSocket.loop();
  drawAnimation();

  if (++delay_count > 10) {
    delay_count = 0;
    if (myMAX31855.detectThermocouple() == MAX31855_THERMOCOUPLE_OK) {
      rawData = myMAX31855.readRawData();
      float new_temp = myMAX31855.getTemperature(rawData);
      if (new_temp > 800) {
        new_temp = fog_temp;
      }
      if ((int)new_temp != (int)fog_temp) {
        fog_temp = new_temp;
        if (fog_temp > fog_upper_temp) {
          heater = false;
        }
        if (fog_temp < fog_lower_temp) {
          heater = true;
        }
        update_text = true;
      }
    } else {
      heater = false;
      update_text = true;
    }
    if (sys_temp > 90.0) {
      heater = false;
      update_text = true;
    }

    if ((int)readNTCTemp() != (int)sys_temp) {
      sys_temp = readNTCTemp();
      update_text = true;
    }
  }

  /*if (test_timestamp < millis()) {
    test_timestamp = millis() + 1000;
    fog_temp = (fog_temp + 1.0);
    if (fog_temp > 300) fog_temp = 0;
    fogjuice = (fogjuice + 0.3);
    if (fogjuice > 5000) fogjuice = 0;
    update_text = true;
  }*/

  button = !digitalRead(BUTTON_PIN);
  if (button != prev_button) {
    Serial.print("Button: ");
    Serial.println(button);
    Serial.print("WiFi button: ");
    Serial.println(wifi_button);
    prev_button = button;
    update_text = true;
  }

  pump = (button || wifi_button);

  digitalWrite(PUMP_PIN, !pump);
  digitalWrite(HEATER_PIN, !heater);

  display.display();
  delay(50);
}

int averageAnalogRead() {
  int val = 0;
  for(int i = 0; i < 20; i++) {
    val += analogRead(A0);
    delay(1);
  }
  return val / 20;
}

float readNTCTemp() {
  // Thermistor code from https://learn.adafruit.com/thermistor/using-a-thermistor
  uint8_t i;
  float average;
 
  // take N samples in a row, with a slight delay
  for (i=0; i< NUMSAMPLES; i++) {
   samples[i] = analogRead(THERMISTORPIN);
   delay(10);
  }
  
  // average all the samples out
  average = 0;
  for (i=0; i< NUMSAMPLES; i++) {
     average += samples[i];
  }
  average /= NUMSAMPLES;
 
  //Serial.print("Average analog reading "); 
  //Serial.println(average);
  
  // convert the value to resistance
  average = 1023 / average - 1;
  average = SERIESRESISTOR / average;
  //Serial.print("Thermistor resistance "); 
  //Serial.println(average);
  
  float steinhart;
  steinhart = average / THERMISTORNOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert to C
  
  //Serial.print("Temperature "); 
  //Serial.print(steinhart);
  //Serial.println(" *C");

  return steinhart;
}

void drawAnimation() {
  int now = millis();
  if (prev_timestamp < 0) {
    prev_timestamp = millis();
  }
  int step = (now - prev_timestamp) * speed / 1000;
  int x = prev_x + step*dir;
  if (x >= display.width()) { // right bounce
    dir = -1;
    x = prev_x + step*dir;
  }
  else if (x < 0) { // left bounce
    dir = 1;
    x = prev_x + step*dir;
  }
  display.drawPixel(prev_x, display.height()-1, SSD1306_BLACK);
  display.drawPixel(x, display.height()-1, SSD1306_WHITE);
  prev_x = x;
  prev_timestamp = now;
}

void updateText() {
  display.setCursor(4,5);
  display.println(F("FOGGER"));
  //display.println();
  display.drawLine(0,7,32,7,SSD1306_WHITE);
  display.setCursor(0,14);

  display.println(F("FOG TEMP"));
  display.println();
  display.setFont(&FreeSansBold9pt7b);
  display.print(fog_temp, 0);
  //display.print((char)247);
  //display.println("C");
  display.setFont(&Picopixel);
  display.println();
  display.println(F("SYS TEMP"));
  display.println();
  display.setFont(&FreeSansBold9pt7b);
  //sprintf(buff, "%d", (int)round(sys_temp));
  //display.getTextBounds(buff, 0, 0, &_x1, &_y1, &w, &h); //calc width of new string
  display.print(sys_temp, 0);
  //display.setCursor(display.getCursorX() + (display.width() - w) / 2, display.getCursorY());
  //display.print(buff);
  display.setFont(&Picopixel);
  display.println();
  display.println();

  display.print(F("HEAT "));
  display.println((heater?"ON":"OFF"));

  display.print(F("BTN "));
  display.println((button?"ON":"OFF"));

  display.print(F("PUMP "));
  display.println((pump?"ON":"OFF"));

  display.print(F("FOGJUICE"));
  //display.println();
  display.setFont();
  display.println();
  display.print(fogjuice, 0);
  display.setFont(&Picopixel);
  display.println();

  //display.print((char)247);
  //isplay.println("C");

  /*display.drawRect(104, 0, 15, 15, SSD1306_WHITE);
  if (connected) {
    display.fillRect(106, 2, 11, 11, SSD1306_WHITE);
  }
  display.setCursor(101,16);
  display.println("WiFi");*/
  display.println();
  display.println(F("WiFi"));
  display.println((num_connected>0?"ACTIVE":"WAITING"));
  display.print(F("BTN "));
  display.println((wifi_button?"ON":"OFF"));
}

// Callback: receiving any WebSocket message
void onWebSocketEvent(uint8_t client_num,
                      WStype_t type,
                      uint8_t * payload,
                      size_t length) {
 
  // Figure out the type of WebSocket event
  switch(type) {
 
    // Client has disconnected
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", client_num);
      num_connected --;
      break;
 
    // New client has connected
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(client_num);
        Serial.printf("[%u] Connection from ", client_num);
        Serial.println(ip.toString());
        num_connected ++;
      }
      break;
 
    // Handle text messages from client
    case WStype_TEXT:
 
      // Print out raw message
      Serial.printf("[%u] Received text: %s\n", client_num, payload);
 
      // Toggle LED
      if ( strcmp((char *)payload, "toggleLED") == 0 ) {
        led_state = led_state ? 0 : 1;
        Serial.printf("Toggling LED to %u\n", led_state);
        digitalWrite(LED_BUILTIN, led_state);
 
      // Report the state of the LED
      } else if ( strcmp((char *)payload, "getState") == 0 ) {
        sprintf(msg_buf, "%d %d %d %d %d %d", (int)fog_temp, (int)sys_temp, heater, pump, (int)fogjuice, button);
        Serial.printf("Sending to [%u]: %s\n", client_num, msg_buf);
        webSocket.sendTXT(client_num, msg_buf);
 
      // Message not recognized
      } else if ( strcmp((char *)payload, "fogDown") == 0 ) {
        wifi_button = true;

      } else if ( strcmp((char *)payload, "fogUp") == 0 ) {
        wifi_button = false;

      } else {
        Serial.println("[%u] Message not recognized");
      }
      break;
 
    // For everything else: do nothing
    case WStype_BIN:
    case WStype_ERROR:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    default:
      break;
  }
  update_text = true;
}

// Callback: send homepage
void onIndexRequest(AsyncWebServerRequest *request) {
  IPAddress remote_ip = request->client()->remoteIP();
  Serial.println("[" + remote_ip.toString() +
                  "] HTTP GET request of " + request->url());
  request->send(SPIFFS, "/index.html", "text/html");
}
 
// Callback: send 404 if requested file does not exist
void onPageNotFound(AsyncWebServerRequest *request) {
  IPAddress remote_ip = request->client()->remoteIP();
  Serial.println("[" + remote_ip.toString() +
                  "] HTTP GET request of " + request->url());
  request->send(404, "text/plain", "Not found");
}