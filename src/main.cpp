/*
  ESP8266 fog machine controller
  See github for more info https://github.com/larsenglund/fogger
*/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool heater = false;
bool pump = true;
bool connected = true;
float fog_temp = 160.0;
float sys_temp = 24.0;

void setup() {
  Serial.begin(115200);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
}

void loop() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0,0);
  display.print(F("HEATER:   "));
  display.println((heater?"ON":"OFF"));

  //display.setCursor(display.width()/2,0);
  display.print(F("PUMP:     "));
  display.println((pump?"ON":"OFF"));

  //display.setCursor(0,8);
  display.print(F("FOG TEMP: "));
  display.print(fog_temp, 0);
  display.print((char)247);
  display.println("C");

  display.print(F("SYS TEMP: "));
  display.print(sys_temp, 0);
  display.print((char)247);
  display.println("C");

  display.drawRect(104, 0, 15, 15, SSD1306_WHITE);
  if (connected) {
    display.fillRect(106, 2, 11, 11, SSD1306_WHITE);
  }
  display.setCursor(101,16);
  display.println("WiFi");

  display.display();
  delay(250);
}