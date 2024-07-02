#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SimpleTimer.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>

// Your existing variables and setup
float calibph7 = 2.64; //spannung bei ph7 bzw 6.86
float calibph4 = 3.30; // spannung bei ph4 
float calibph7_ph = 6.97 ;
float calibph4_ph = 4.01 ;
float m;
float b;
float phValue;
float Voltage;
float maxph = 7.3;
float minph = 6.8;
boolean wifi = false;
boolean mqtt = false;

///SimpleTimer timer(5000);
int phval = 0; 
unsigned long int avgval; 
int buffer_arr[100], temp;

float ph_act; 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

#define RELAY_PIN_MAX 16
#define RELAY_PIN_MIN 17

// WiFi credentials
const char* ssid = "<yourSSID>";
const char* password = "<yourpass>";

// MQTT Server
const char* mqtt_server = "yourmqttserver";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);
WebServer server(80);

// OLED display setup
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Sliding window buffer for 180 values
#define BUFFER_SIZE 180
float phBuffer[BUFFER_SIZE];
int bufferIndex = 0;
bool bufferFull = false;

void setup() {
  Wire.begin();
  Serial.begin(115200);

  pinMode(RELAY_PIN_MAX, OUTPUT);
  pinMode(RELAY_PIN_MIN, OUTPUT); 
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE); 
  display_pHValue();


  m = (calibph4_ph - calibph7_ph) / (calibph4 - calibph7);
  b = calibph7_ph - m * calibph7;

  connectWiFi();
  client.setServer(mqtt_server, mqtt_port);
  connectMQTT();

  // Web server routes
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.begin();
}

void loop() {
    for(int i = 0; i < 100; i++) { 
    buffer_arr[i] = analogRead(32);
    delay(30);
  }
  for(int i = 0; i < 99; i++) {
    for(int j = i + 1; j < 100; j++) {
      if(buffer_arr[i] > buffer_arr[j]) {
        temp = buffer_arr[i];
        buffer_arr[i] = buffer_arr[j];
        buffer_arr[j] = temp;
      }
    }
  }
  avgval = 0;
  for(int i = 40; i < 60; i++)
    avgval += buffer_arr[i];
 
  Voltage = (float)avgval * 3.3 / 4096.0 / 20; // Averagevalue is devided by supplied voltage 3.3V devided by ADC resolution 4096bit and devided by number of samples read 6 for avgvalue
  Serial.print("*******Voltage: ");
  Serial.println(Voltage);
  phValue = m * Voltage + b;
  Serial.print("*******pH Val: ");
  Serial.println(phValue);  

  updatePHBuffer(phValue);
  float avgPH = calculateAveragePH();

  checkPH(avgPH);
 
  display_pHValue();

  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  server.handleClient();
}

void updatePHBuffer(float newValue) {
  phBuffer[bufferIndex] = newValue;
  bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
  if (bufferIndex == 0) {
    bufferFull = true;
  }
  String phvalueStr = String(newValue);   // Use avgPH for the string
  String status;
  client.publish("/garten/pool/ph/currentph", phvalueStr.c_str());
}

float calculateAveragePH() {
  int count = bufferFull ? BUFFER_SIZE : bufferIndex;
  float sum = 0;
  for (int i = 0; i < count; i++) {
    sum += phBuffer[i];
  }
  return count > 0 ? sum / count : 0;
}

void display_pHValue() {
  float avgPH = calculateAveragePH();
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("pH:");
  display.setCursor(30, 0);
  display.print(Voltage);
  display.setCursor(50, 0);
  display.print(" V");

  display.setTextSize(3);
  display.setCursor(0, 10);
  display.print(phValue);
  
  display.setTextSize(1);
  display.setCursor(72, 0);
  display.print(maxph);

  display.setCursor(72, 25);
  display.print(minph);

  display.setTextSize(1);
  display.setCursor(100, 0);
  display.print("AVG");
  display.setCursor(100, 8);
  display.print(avgPH);
  

  if(wifi) {
    display.setCursor(100, 19);
    display.print("WIFI");
  } else {
    display.setCursor(100, 19);
    display.print("-IF-");
  }

  if(mqtt) {
    display.setCursor(100, 27);
    display.print("MQTT");
  } else {
    display.setCursor(100, 27);
    display.print("-QT-");
  }

  if (avgPH > maxph) {
    drawArrowDown();
  } else if (avgPH < minph) {
    drawArrowUp();
  } else {
    drawCircle();
  }
  display.display();
}

void connectWiFi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    wifi = false;
  }
  Serial.println("");
  Serial.println("WiFi connected");
  wifi = true;
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  
  String messageTemp = "";
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();
}

void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      mqtt = true;
    } else {
      mqtt = false;
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      mqtt = true;
    } else {
      mqtt = false;
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void checkPH(float avgPH) {
  String phvalueStr = String(avgPH);   // Use avgPH for the string
  String status;
  client.publish("/garten/pool/ph/phvalue", phvalueStr.c_str());
  if (avgPH > maxph) {  // Use avgPH instead of phValue
    digitalWrite(RELAY_PIN_MAX, HIGH);
    digitalWrite(RELAY_PIN_MIN, LOW);
    status = "PH High";
  } else if (avgPH < minph) {  // Use avgPH instead of phValue
    digitalWrite(RELAY_PIN_MIN, HIGH);
    digitalWrite(RELAY_PIN_MAX, LOW);
    status = "PH Low";
  } else {
    digitalWrite(RELAY_PIN_MAX, LOW);
    digitalWrite(RELAY_PIN_MIN, LOW);
    status = "PH OK";
  }
  client.publish("/garten/pool/ph/status", status.c_str());
}

void drawCircle() {
  display.fillCircle(80, 16, 6, 1);
}

void drawArrowUp() {
  display.fillTriangle(72, 9, 88, 9, 80, 22, 1);
}

void drawArrowDown() {
  display.fillTriangle(72, 22, 88, 22, 80, 9, 1);
}

void handleRoot() {
  float avgPH = calculateAveragePH();
  String html = "<html>\
  <head>\
  <title>pH Monitor</title>\
  </head>\
  <body>\
  <h1>pH Monitor</h1>\
  <p>Current pH Value: " + String(phValue) + "</p>\
  <p>Current Voltage: " + String(Voltage) + " V</p>\
  <p>Average pH Value: " + String(avgPH) + "</p>\
  <p>Max pH: " + String(maxph) + "</p>\
  <p>Min pH: " + String(minph) + "</p>\
  <p>Calibration pH 7 Voltage: " + String(calibph7) + "</p>\
  <p>Calibration pH 4 Voltage: " + String(calibph4) + "</p>\
  <p>Calibration pH 7 Value: " + String(calibph7_ph) + "</p>\
  <p>Calibration pH 4 Value: " + String(calibph4_ph) + "</p>\
  <form action=\"/set\" method=\"GET\">\
  <label for=\"calibph7\">New Calibration pH 7 Voltage:</label><br>\
  <input type=\"text\" id=\"calibph7\" name=\"calibph7\" value=\"" + String(calibph7) + "\"><br>\
  <label for=\"calibph4\">New Calibration pH 4 Voltage:</label><br>\
  <input type=\"text\" id=\"calibph4\" name=\"calibph4\" value=\"" + String(calibph4) + "\"><br>\
  <label for=\"calibph7_ph\">New Calibration pH 7 Value:</label><br>\
  <input type=\"text\" id=\"calibph7_ph\" name=\"calibph7_ph\" value=\"" + String(calibph7_ph) + "\"><br>\
  <label for=\"calibph4_ph\">New Calibration pH 4 Value:</label><br>\
  <input type=\"text\" id=\"calibph4_ph\" name=\"calibph4_ph\" value=\"" + String(calibph4_ph) + "\"><br><br>\
  <input type=\"submit\" value=\"Submit\">\
  </form>\
  </body>\
  </html>";

  server.send(200, "text/html", html);
}

void handleSet() {
   if (server.hasArg("calibph7") && server.hasArg("calibph4") && server.hasArg("calibph7_ph") && server.hasArg("calibph4_ph")) {
    calibph7 = server.arg("calibph7").toFloat();
    calibph4 = server.arg("calibph4").toFloat();
    calibph7_ph = server.arg("calibph7_ph").toFloat();
    calibph4_ph = server.arg("calibph4_ph").toFloat();
    m = (calibph4_ph - calibph7_ph) / (calibph4 - calibph7);
    b = calibph7_ph - m * calibph7;
  }
  handleRoot();
}
