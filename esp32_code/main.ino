#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESP32Servo.h>

// WIFI
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// MQTT
const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);

// SENSOR PINS
#define DHT_PIN 4
#define DHTTYPE DHT22
#define PIR_PIN 14
#define LDR_PIN 34
#define TRIG_PIN 5
#define ECHO_PIN 18

// ACTUATORS
#define LED_RED 25
#define LED_YELLOW 26
#define LED_GREEN 27
#define BUZZER 32
#define RELAY 13
#define SERVO_PIN 33

DHT dht(DHT_PIN, DHTTYPE);
Servo servo;

unsigned long lastMsg = 0;

// -------- Thresholds (F6) --------
float tempThreshold = 30.0;
int lightThreshold = 2500;
float distanceThreshold = 20.0;

// -------- Manual States --------
bool redManual = false;
bool yellowManual = false;
bool greenManual = false;
bool buzzerManual = false;
bool relayManual = false;
int servoManualAngle = 90;


// WIFI CONNECT
void connectWiFi() {

  Serial.print("Connecting WiFi");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println(" Connected!");
}


// MQTT CALLBACK
void callback(char* topic, byte* payload, unsigned int length) {

  String message = "";

  for (int i = 0; i < length; i++)
    message += (char)payload[i];

  String t = String(topic);

  Serial.println("MQTT -> " + t + " : " + message);


  // -------- MANUAL SWITCH STATES --------

  if (t == "actuators/led/red")
    redManual = (message == "ON");

  if (t == "actuators/led/yellow")
    yellowManual = (message == "ON");

  if (t == "actuators/led/green")
    greenManual = (message == "ON");

  if (t == "actuators/buzzer")
    buzzerManual = (message == "ON");

  if (t == "actuators/relay")
    relayManual = (message == "ON");

  if (t == "actuators/servo")
    servoManualAngle = message.toInt();


  // -------- THRESHOLD UPDATES --------

  if (t == "thresholds/temperature")
    tempThreshold = message.toFloat();

  if (t == "thresholds/light")
    lightThreshold = message.toInt();

  if (t == "thresholds/distance")
    distanceThreshold = message.toFloat();
}


// MQTT RECONNECT
void reconnect() {

  while (!client.connected()) {

    Serial.print("Connecting MQTT...");

    String clientId = "ESP32-" + String(random(1000));

    if (client.connect(clientId.c_str())) {

      Serial.println("connected");

      client.subscribe("actuators/led/red");
      client.subscribe("actuators/led/yellow");
      client.subscribe("actuators/led/green");
      client.subscribe("actuators/buzzer");
      client.subscribe("actuators/relay");
      client.subscribe("actuators/servo");

      client.subscribe("thresholds/temperature");
      client.subscribe("thresholds/light");
      client.subscribe("thresholds/distance");

    } else {

      Serial.println("retrying...");
      delay(2000);
    }
  }
}


// ULTRASONIC SENSOR
long readDistance() {

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);

  return duration * 0.034 / 2;
}


void setup() {

  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(RELAY, OUTPUT);

  servo.attach(SERVO_PIN);

  dht.begin();

  connectWiFi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}


void loop() {

  if (WiFi.status() != WL_CONNECTED)
    connectWiFi();

  if (!client.connected())
    reconnect();

  client.loop();


  // SENSOR LOOP EVERY 2s
  if (millis() - lastMsg > 2000) {

    lastMsg = millis();

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int motion = digitalRead(PIR_PIN);
    int light = analogRead(LDR_PIN);
    long distance = readDistance();


    // SERIAL DEBUG
    Serial.println("------ Sensor Data ------");

    Serial.print("Temperature: ");
    Serial.println(temperature);

    Serial.print("Humidity: ");
    Serial.println(humidity);

    Serial.print("Light: ");
    Serial.println(light);

    Serial.print("Motion: ");
    Serial.println(motion);

    Serial.print("Distance: ");
    Serial.println(distance);

    Serial.println("-------------------------");


    // JSON TELEMETRY
    String tempJson = "{\"value\":" + String(temperature) + ",\"unit\":\"C\"}";
    String humJson = "{\"value\":" + String(humidity) + ",\"unit\":\"%\"}";
    String lightJson = "{\"value\":" + String(light) + "}";
    String motionJson = "{\"detected\":" + String(motion) + "}";
    String distJson = "{\"value\":" + String(distance) + ",\"unit\":\"cm\"}";

    client.publish("sensors/temperature", tempJson.c_str());
    client.publish("sensors/humidity", humJson.c_str());
    client.publish("sensors/light", lightJson.c_str());
    client.publish("sensors/motion", motionJson.c_str());
    client.publish("sensors/distance", distJson.c_str());


    // -------- OR LOGIC (Threshold OR Switch) --------

    bool redAuto = temperature > tempThreshold;
    digitalWrite(LED_RED, redAuto || redManual);

    bool yellowAuto = light > lightThreshold;
    digitalWrite(LED_YELLOW, yellowAuto || yellowManual);

    digitalWrite(LED_GREEN, greenManual);

    bool buzzerAuto = motion;
    if (buzzerManual || buzzerAuto)
      tone(BUZZER, 1000);
    else
      noTone(BUZZER);

    digitalWrite(RELAY, relayManual);

    bool servoAuto = distance > 0 && distance < distanceThreshold;

    if (servoAuto)
      servo.write(0);
    else
      servo.write(servoManualAngle);
  }
}