#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESP8266httpUpdate.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define APIKEY "318f29c3-a556-4108-a1a0-b4d445309571"
#define FW_VER "v@1.0.9"

// ======= WiFi config =======
const char *ssid = "NGOC PHUC";
const char *password = "np555588";

// ======= MQTT config =======
const char *mqtt_server = "192.168.1.11"; // ví dụ 192.168.1.100
const int mqtt_port = 8883;
const char *mqtt_username = "imghostcode";
const char *mqtt_password = "Test@12345";
const char *deviceId = "esp8266_living_room";
const char *room = "living_room";
String controlTopic = String("home/") + room + "/" + deviceId + "/control";
String statusTopic = String("home/") + room + "/" + deviceId + "/status";
String alertTopic = String("home/") + room + "/" + deviceId + "/alert";

// ======= DHT config =======
#define DHTPIN D5 // GPIO14
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

WiFiClientSecure espClient;
PubSubClient client(espClient);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String getChipId()
{
  return String(ESP.getChipId(), HEX);
}

void update_FOTA()
{
  Serial.println("Check update");
  String url = String("http://otadrive.com/deviceapi/update?k=") +
               APIKEY + "&v=" + FW_VER + "&s=" + getChipId();
  Serial.println("URL: " + url);

  WiFiClient client;
  t_httpUpdate_return result = ESPhttpUpdate.update(client, url, FW_VER);

  switch (result)
  {
  case HTTP_UPDATE_FAILED:
    Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
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
  for (unsigned int i = 0; i < length; i++)
  {
    msg += (char)payload[i];
  }

  Serial.print("Command: ");
  Serial.print(topic);
  Serial.print(" --> ");
  Serial.println(msg);
}

void reconnect()
{
  // Loop đến khi kết nối lại được
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-"; // Create a random client ID
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password))
    {
      Serial.println("Connected");
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

void setup()
{

  Serial.begin(115200);
  Serial.print("Device:");
  Serial.println(deviceId);
  Serial.print("Firmware Verison:");
  Serial.println(FW_VER);
  dht.begin();
  // pinMode(LED_PIN, OUTPUT);
  // digitalWrite(LED_PIN, HIGH);  // tắt LED mặc định

  setup_wifi();

#ifdef ESP8266
  espClient.setInsecure();
#else
  espClient.setCACert(root_ca); // enable this line and the the "certificate" code for secure connection
#endif

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  timeClient.begin();
  timeClient.setTimeOffset(7 * 3600); // Việt Nam GMT+7
}

void loop()
{
  if (!client.connected())
    reconnect(); // check if client is connected
  client.loop();

  // Đọc sensor mỗi 5s
  static unsigned long lastTime = 0;
  unsigned long now = millis();
  if (now - lastTime > 10000)
  {
    lastTime = now;
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    // if (isnan(h) || isnan(t))
    // {
    //   Serial.println("Lỗi đọc cảm biến DHT!");
    //   return;
    // }

    timeClient.update();
    unsigned long timestamp = timeClient.getEpochTime();
    Serial.println(timeClient.getFormattedTime());
    String payload = "{";
    payload += "\"device_id\":\"esp8266_livingroom\",";
    payload += "\"location\":\"living_room\",";
    payload += "\"type\":\"dht_sensor\",";
    payload += "\"timestamp\":" + String(timestamp) + ",";
    payload += "\"values\":{\"temp\":" + String(t, 2) + ",\"hum\":" + String(h, 2) + "}}";

    client.publish(statusTopic.c_str(), payload.c_str());
    Serial.print("Published: ");
    Serial.println(payload);
  }
}
