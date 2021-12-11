#include <Arduino.h>
#include <DHT.h> // Digital relative humidity & temperature sensor AM2302/DHT22

#ifdef ARDUINO_ESP8266_WEMOS_D1MINI
//#include <WiFiClientSecure.h>
//#include <DNSServer.h>
//#include <ESP8266HTTPClient.h>
//#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include "DefinePin8266.h"
#endif

#ifdef ARDUINO_ESP32_DEV
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include "DefinePin32.h"
#endif

#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager
#include <EspMQTTClient.h>
#include <Wire.h> // Library for I2C communication
#include <SH1106Wire.h>
#include <ArduinoJson.h>
#include <CircularBuffer.h>
#include "SlopeTracker.h"
#include "iconset_16x12.xbm"
#include <ezTime.h>
#include "arduino_secrets.h"
#include <Ticker.h>

SH1106Wire display(0x3c, SDA, SCL); // ADDRESS, SDA, SCL

WiFiManager wifiManager;

char auth[] = BLYNK_AUTH_TOKEN;

#define DHTTYPE DHT22 // DHT 22 (AM2302)

DHT dht(DHTPIN, DHTTYPE);
BlynkTimer timer;

EspMQTTClient client(
    NULL,
    NULL,
    MQTT_BROKER,    // MQTT Broker server ip
    MQTT_USER,      // Can be omitted if not needed
    MQTT_PASS,      // Can be omitted if not needed
    THIS_DEVICE_ID, // Client name that uniquely identify your device
    1883            // The MQTT port, default to 1883. this line can be omitted
);

float temp_realtime, rh_realtime, rh_1s_mva, t_1s_mva;

const unsigned long mqtt_timer_int = 60000L;
const uint8_t avg_sample_time = 250, disp_timer_int = 1000;

void read_sensor();
void display_value();
void send_dht_mqtt();

Ticker timer_measure(read_sensor, avg_sample_time);
Ticker timer_display(display_value, disp_timer_int);
Ticker timer_mqtt(send_dht_mqtt, mqtt_timer_int);

SlopeTracker t_short_buffer(4, avg_sample_time / 60000.0);
SlopeTracker rh_short_buffer(4, avg_sample_time / 60000.0);
uint8_t x_1col = 28, x_2col = 96, y_0row = 1, y_1row = 20, y_2row = 44;

Timezone myTZ;
// Provide official timezone names
// https://en.wikipedia.org/wiki/List_of_tz_database_time_zones

// This function sends Arduino's up time every second to Virtual Pin (5).
// In the app, Widget's reading frequency should be set to PUSH. This means
// that you define how often to send data to Blynk App.
void sendSensor()
{
  Blynk.virtualWrite(V5, rh_1s_mva);
  Blynk.virtualWrite(V6, t_1s_mva);
}

void onConnectionEstablished()
{
  // Subscribe to "mytopic/test" and display received message to Serial
}

void setup()
{
  // Debug console
  Serial.begin(9600);
  // connect_to_wifi();
  wifiManager.setTimeout(180);
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
#ifdef ARDUINO_ESP32_DEV
  wifiManager.setConfigPortalBlocking(false);
#endif
  wifiManager.autoConnect("AutoConnectAP");
  delay(2000);

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("connected...yeey :)");
    Blynk.config(auth, IPAddress(64, 225, 16, 22), 8080);
    // You can also specify server:
    // Blynk.begin(auth, ssid, pass, "blynk-cloud.com", 80);
  }
  display.init();
  display.flipScreenVertically();
  display.clear();
  dht.begin();

  // Setup a function to be called every second
  timer.setInterval(mqtt_timer_int, sendSensor);

  // Optionnal functionnalities of EspMQTTClient :
  client.enableDebuggingMessages(); // Enable debugging messages sent to serial output
  client.enableHTTPWebUpdater();    // Enable the web updater. User and password default to values of MQTTUsername and MQTTPassword. These can be overrited with enableHTTPWebUpdater("user", "password").
  client.enableLastWillMessage("TestClient/lastwill", "I am going offline");

  waitForSync();
  myTZ.setLocation(F("America/Vancouver"));
  timer_measure.start();
  timer_display.start();
  timer_mqtt.start();
}

void loop()
{
  client.loop();
  Blynk.run();
  timer.run();
  timer_measure.update();
  timer_display.update();
  timer_mqtt.update();
}

void draw_background()
{
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(x_1col, y_1row, "T");
  display.drawString(x_1col, y_2row, "RH");
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(x_2col, y_1row, "'C");
  display.drawString(x_2col, y_2row, "%");
}

void displayTime()
{
  display.setFont(ArialMT_Plain_10);
  display.drawString(1, 0, myTZ.dateTime("l"));
  display.drawString(1, 9, myTZ.dateTime("m/d H:i"));
}

void read_sensor()
{
  rh_realtime = dht.readHumidity();
  temp_realtime = dht.readTemperature();
  rh_short_buffer.addPoint(rh_realtime);
  t_short_buffer.addPoint(temp_realtime);
  if (rh_short_buffer.ready())
  {
    rh_1s_mva = rh_short_buffer.getAvg();
  }
  else
  {
    rh_1s_mva = rh_realtime;
  }
  if (t_short_buffer.ready())
  {
    t_1s_mva = t_short_buffer.getAvg();
  }
  else
  {
    t_1s_mva = temp_realtime;
  }
}

void send_dht_mqtt()
{
  String telemetry_json = "{\"t\":";
  telemetry_json += String(t_1s_mva, 1);
  telemetry_json += ",\"h\":";
  telemetry_json += String(rh_1s_mva, 1);
  telemetry_json += "}";
  String tele_topic = MQTT_TOPIC_PREFIX;
  tele_topic += THIS_DEVICE_ID;
  tele_topic += MQTT_TOPIC_SUFFIX;
  client.publish(tele_topic, telemetry_json);
}

void display_value()
{
  uint16_t xc = x_1col + 14;
  display.clear();
  draw_background();
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(xc, y_1row - 2, String(t_1s_mva, 1));
  display.drawString(xc, y_2row - 2, String(rh_1s_mva, 1));
  displayTime();
  display.drawLine(74, y_0row + 13, 128, y_0row + 13);

  if ((WiFi.status() == WL_CONNECTED))
  {
    display.setFont(ArialMT_Plain_10);
    // display.drawString(x_2col, y_3row, "WiFi");
    display.drawXbm(112, y_0row, Iot_Icon_width, Iot_Icon_height, wifi1_icon16x12);
  }
  if (Blynk.CONNECTED)
  {
    display.drawXbm(94, y_0row, Iot_Icon_width, Iot_Icon_height, blynk_icon16x12);
  }
  if (client.isConnected())
  {
    display.drawXbm(76, y_0row, Iot_Icon_width, Iot_Icon_height, mqtt_icon16x12);
  }
  display.display();
}