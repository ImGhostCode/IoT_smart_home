#include <Arduino.h>

#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <LittleFS.h> // or SPIFFS

#define APIKEY "cc930f9d-394b-42e3-a42a-a21dec3b655d"
#define FW_VER "v@1.0.9"
#define LIGHT_PIN 4
#define NTC_PIN 36          // A0 (GPIO36)
#define TEMP_THRESHOLD 40.0 // °C

// NTC config
const float R0 = 10000.0;      // 10kΩ
const float B = 3950.0;        // hệ số B
const float Ti0 = 298.15;      // 25°C = 298.15K
const float R_fixed = 10000.0; // Điện trở 10kΩ cố định

// ======= WiFi config =======
const char *ssid = "NGOC PHUC";
const char *password = "np555588";

// ======= MQTT config =======
const char *mqtt_server = "192.168.1.11"; // ví dụ 192.168.1.100
const int mqtt_port = 8883;
const char *mqtt_username = "imghostcode";
const char *mqtt_password = "Test@12345";
const char *room = "living_room";
const char *deviceId = "esp32_livingroom";
String controlTopic = String("home/") + room + "/" + deviceId + "/control";
String statusTopic = String("home/") + room + "/" + deviceId + "/status";
String alertTopic = String("home/") + room + "/" + deviceId + "/alert";

WiFiClientSecure espClient;
PubSubClient client(espClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String getChipId()
{
  String ChipIdHex = String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  ChipIdHex += String((uint32_t)ESP.getEfuseMac(), HEX);
  return ChipIdHex;
}

void update_FOTA()
{
  Serial.println("Check update");
  String url = String("http://otadrive.com/deviceapi/update?k=") +
               APIKEY + "&v=" + FW_VER + "&s=" + getChipId();
  Serial.println("URL: " + url);

  WiFiClient client;
  t_httpUpdate_return result = httpUpdate.update(client, url);

  switch (result)
  {
  case HTTP_UPDATE_FAILED:
    Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n",
                  httpUpdate.getLastError(),
                  httpUpdate.getLastErrorString().c_str());
    break;
  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("HTTP_UPDATE_NO_UPDATES");
    break;

  case HTTP_UPDATE_OK:
    Serial.println("HTTP_UPDATE_OK");
    break;
  }
}

void setup_wifi()
{
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  update_FOTA();
}

void callback(char *topic, byte *payload, unsigned int length)
{
  String msg;
  for (int i = 0; i < length; i++)
    msg += (char)payload[i];

  Serial.print("Received command - Topic: ");
  Serial.print(topic);
  Serial.print(" | Message: ");
  Serial.println(msg);

  if (String(topic).endsWith("/control"))
  {
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, msg);
    if (err)
    {
      Serial.println("JSON Parse Error");
      return;
    }
    if (doc["light"].is<const char *>())
    {
      digitalWrite(LIGHT_PIN, doc["light"] == "on" ? HIGH : LOW);
    }
  }
}

void reconnect()
{
  // Loop đến khi kết nối lại được
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-"; // Create a random client ID
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password))
    {
      Serial.println("Connected");
      client.subscribe(controlTopic.c_str());
      Serial.print("Subscribed topic: ");
      Serial.println(controlTopic.c_str());
    }
    else
    {
      Serial.print("Connection error, rc=");
      Serial.print(client.state());
      Serial.println(" retry after 5 seconds");
      delay(5000);
    }
  }
}

float readNTC()
{
  int adc = analogRead(NTC_PIN);
  float Vout = adc * 3.3 / 4095.0;
  float R_ntc = R_fixed * (3.3 - Vout) / Vout;
  float tempK = 1.0 / (1.0 / Ti0 + (1.0 / B) * log(R_ntc / R0));
  return tempK - 273.15;
}

void setup()
{
  Serial.begin(115200);
  delay(10);
  Serial.print("Device:");
  Serial.println(deviceId);
  Serial.print("Room:");
  Serial.println(room);
  Serial.print("Firmware Verison:");
  Serial.println(FW_VER);
  pinMode(LIGHT_PIN, OUTPUT);
  // digitalWrite(LED_PIN, HIGH);  // tắt LED mặc định

  setup_wifi();
  if (!LittleFS.begin())
  {
    Serial.println("An Error has occurred while mounting LittleFS");
  }
  else
  {
    File cert = LittleFS.open("/ca.crt", "r");
    if (cert)
    {
      String root_ca = cert.readString().c_str();
      Serial.println(root_ca);
      cert.close();
    }
    else
    {
      Serial.println("Failed to load CA certificate!");
    }
  }

  // #ifdef ESP8266
  espClient.setInsecure();
  // #else
  // espClient.setCACert(root_ca); // enable this line and the the "certificate" code for secure connection
  // #endif

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  timeClient.begin();
  timeClient.setTimeOffset(7 * 3600); // Việt Nam GMT+7
}

unsigned long lastSendTime = 0;
const unsigned long interval = 5000; // 5 giây
bool tempHigh = false;               // lưu trạng thái trước đó

void loop()
{
  if (!client.connected())
    reconnect();
  client.loop();
  timeClient.update();
  unsigned long now = millis();

  // Gửi dữ liệu mỗi 5 giây
  if (now - lastSendTime >= interval)
  {
    lastSendTime = now;

    unsigned long timestamp = timeClient.getEpochTime();
    Serial.println(timeClient.getFormattedTime());

    float tempNTC = readNTC();
    String payload = "{";
    payload += "\"device_id\":\"esp32_living_room\",";
    payload += "\"location\":\"living_room\",";
    payload += "\"type\":\"ntc_sensor\",";
    payload += "\"timestamp\":" + String(timestamp) + ",";
    payload += "\"values\":{\"temp\":" + String(tempNTC, 2) + "}}";

    client.publish(statusTopic.c_str(), payload.c_str());
    Serial.println(payload);
    // Gửi cảnh báo nếu quá ngưỡng
    if (tempNTC >= 40 && !tempHigh)
    {
      tempHigh = true;
      String alert = "{\"device_id\":\"esp32_livingroom\",\"type\":\"ntc_sensor\",";
      alert += "\"level\":\"warning\",\"message\":\"Temperature is too high!\",";
      alert += "\"timestamp\":" + String(timestamp) + "}";
      client.publish(alertTopic.c_str(), alert.c_str());
      Serial.println(alert);
    }

    if (tempNTC < 40 && tempHigh)
    {
      tempHigh = false;
      // gửi "OK" alert (tùy bạn)
      // String alert = "{\"device_id\":\"esp32_garden\",\"type\":\"soil_sensor\",";
      // alert += "\"level\":\"info\",\"message\":\"Soil moisture back to normal\",";
      // alert += "\"timestamp\":" + String(now) + "}";
      // client.publish("home/garden/esp32_garden/alert", alert.c_str());
    }
  }
}
