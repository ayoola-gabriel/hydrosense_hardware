#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <ArduinoJson.h>
#include <Wifi.h> 
#include <Preferences.h>
#include <ESPSupabase.h>
#include <BluetoothSerial.h>

Supabase db;

#define USERNAME_STORAGE_ADDR 0
#define PASSWORD_STORAGE_ADDR 1
#define WIFI_SSID_ADDR 2
#define WIFI_PASSWORD_ADDR 3

#define WIFI_SSID "Ayoola"
#define WIFI_PASSWORD "1234567890"
#define DEFAULT_EMAIL "newnesselectronics@gmail.com"
#define SENSOR_TABLE "sensor_readings"
#define ALERT_TABLE "alerts"
#define DEFAULT_PASSWORD "Qwertyuiop"
#define WIFI_TIMEOUT_MS 10000

#define SUPABASE_URL "https://mdsvufmknyewogagdyvy.supabase.co"
#define SUPABASE_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im1kc3Z1Zm1rbnlld29nYWdkeXZ5Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzYyNTQ4MjYsImV4cCI6MjA5MTgzMDgyNn0.pwgIAqhiu2mMp1-owfxi4mN6TKVGb9eboAdyFF5JdU0"

String accessToken = "";
String userId = "";

//Map analog pins to their names
#define PH_PIN 33
#define TEMP_PIN 32
#define EC_PIN 36
// #define TDS_PIN 36
#define LEVEL_PIN 39
#define LED_PIN 4

float temp, ph_value, level_value, ec_float = 0.0;
uint32_t ec_value, tds_value = 0;

bool wifiConnected = false;
bool loggedIn = false;
bool uploadingData = false;

//custom lcd characters
byte wifi[] = { 0b00000, 0b00100, 0b01010, 0b10001, 0b00100, 0b01010, 0b10001, 0b00100 };

bool deviceConnected = false;
bool oldDeviceConnected = false;

uint32_t value = 0;

LiquidCrystal_PCF8574 lcd(0x27);  // set the LCD address to 0x27 for a 16 chars and 2 line display

//function templates
void sensor_values_to_mA();
void checkWiFiConnection();
void reconnectWiFi();
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max);
bool setupSupabase();
void restartESP();

void setup() {
  Serial.begin(9600);

  Serial.println("Probing for PCF8574 on address 0x27...");

  Wire.begin();
  Wire.beginTransmission(0x27);
  int error = Wire.endTransmission();
  Serial.print("Error: ");
  Serial.print(error);

  if (error == 0) {
    Serial.println(": LCD found.");
 
    lcd.begin(20, 4);  
    lcd.createChar(0, wifi);

  } else {
    Serial.println(": LCD not found.");
  }  // if

  lcd.setBacklight(255);

  pinMode(LED_PIN, OUTPUT);

  // SSL Memory Optimization - CRITICAL for ESP32
  Serial.println("Configuring SSL memory optimization...");
 

  // WiFi optimization
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  //connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long startAttemptTime = millis();

  lcd.setCursor(0, 0);
  lcd.print("Connecting to WiFi..");
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    lcd.setCursor(0, 1);
    lcd.print("Connected!");
    digitalWrite(LED_PIN, HIGH); // turn on LED to indicate WiFi connection
    wifiConnected = true;
    delay(2000); // wait a moment before trying to log in to Supabase
    lcd.setCursor(0, 2);
    lcd.print("Logging in...");
    
    loggedIn = setupSupabase();
    if(loggedIn) {
      lcd.setCursor(0, 3);
      lcd.print("Login Success!");
    } else {
      lcd.setCursor(0, 3);
      lcd.print("Login Failed!");
      restartESP();
    }
    // Initialize Supabase client
  } else {
    lcd.setCursor(0, 1);
    lcd.print("Failed to connect");
    Serial.println("Failed to connect to WiFi");
    wifiConnected = false;
    restartESP();
  }

}  // setup()

uint32_t lastSensorReadTime = 0;
String jsonStr;

void loop() {
  // Check WiFi connection and reconnect if necessary
  checkWiFiConnection();

  //read sensors and update LCD
  if(millis() - lastSensorReadTime > 10000) { // read sensors every 10 seconds
    sensor_values_to_mA();
    if(wifiConnected) {
      lcd.setCursor(19, 0);
      lcd.write((uint8_t)0); // custom wifi character

      JsonDocument doc;
      doc["ph"] = ph_value; //ph_value;
      doc["temp"] = temp; //temp;
      doc["ec"] = ec_value; //ec_value;
      doc["tds"] = tds_value; //tds_value;
      doc["level"] = level_value; //level_value;
      doc["user_id"] = userId; // Include user ID in the data being uploaded

      serializeJson(doc, jsonStr);
      int code = db.insert(SENSOR_TABLE, jsonStr, false);
      if (code == 201) {
        digitalWrite(LED_PIN, HIGH); // turn on LED to indicate successful upload
        delay(300);
        digitalWrite(LED_PIN, LOW); // turn off LED after a short delay
      }
      db.urlQuery_reset();
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Level: ");
    lcd.print(level_value, 1);
    lcd.print("m");
    
    lcd.setCursor(0, 1);
    lcd.print("Temp:");
    lcd.print(temp, 1);
    lcd.print((char)223); // degree symbol
    lcd.print("C ");
    lcd.print("pH:");
    lcd.print(ph_value, 1);
   
    lcd.setCursor(0, 2);
    lcd.print("EC:");
    lcd.print(ec_value);
    lcd.print("uS/cm");
    lcd.setCursor(0, 3);
    lcd.print("TDS:");
    lcd.print(tds_value);
    lcd.print("ppm");
    lastSensorReadTime = millis();
  }

  // notify changed value
  // if (deviceConnected)
  // {
  //   txCharacteristics->setValue(jsonStr.c_str());
  //   txCharacteristics->notify();
  //   // value++;
  //   Serial.print("New value notified: ");
  //   Serial.println(jsonStr);
  //   delay(10000); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
  // }
  // // disconnecting
  // if (!deviceConnected && oldDeviceConnected)
  // {
  //   Serial.println("Device disconnected.");
  //   delay(500);                  // give the bluetooth stack the chance to get things ready
  //   pServer->startAdvertising(); // restart advertising
  //   Serial.println("Start advertising");
  //   oldDeviceConnected = deviceConnected;
  // }
  // // connecting
  // if (deviceConnected && !oldDeviceConnected)
  // {
  //   // do stuff here on connecting
  //   oldDeviceConnected = deviceConnected;
  //   Serial.println("Device Connected");
    
  // }
  
}  // loop()

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
} 

bool setupSupabase() {
  Serial.println("Initializing Supabase client...");
  
  // Check WiFi status before attempting login
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected! Cannot login to Supabase.");
    return false;
  }
  
  Serial.printf("Free heap before Supabase init: %d bytes\n", ESP.getFreeHeap());
  
  // Initialize with optimized client
  db.begin(SUPABASE_URL, SUPABASE_KEY);
  
  Serial.printf("Free heap after Supabase init: %d bytes\n", ESP.getFreeHeap());
  
  // Add retry logic for login
  int retryCount = 0;
  const int maxRetries = 3;
  
  while (retryCount < maxRetries) {
    Serial.printf("Login attempt %d/%d...\n", retryCount + 1, maxRetries);
    Serial.printf("Free heap before login: %d bytes\n", ESP.getFreeHeap());
    
    int loginResult = db.login_email(DEFAULT_EMAIL, DEFAULT_PASSWORD, userId);
    
    Serial.printf("Free heap after login: %d bytes\n", ESP.getFreeHeap());
    
    if (loginResult == 200) {
      Serial.println("Logged in to Supabase successfully");
      Serial.println("User ID: " + userId);
      return true;
    } else {
      Serial.printf("Login attempt %d failed with code: %d\n", retryCount + 1, loginResult);
      retryCount++;
      
      if (retryCount < maxRetries) {
        Serial.println("Waiting 2 seconds before retry...");
        delay(2000);  // Wait before retry
      }
    }
  }
  
  Serial.println("Failed to log in to Supabase after all retries");
  return false;
}

void reconnectWiFi() {
  Serial.println("Reconnecting to WiFi...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Reconnected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_PIN, HIGH); // turn on LED to indicate WiFi connection
    wifiConnected = true;
    loggedIn = setupSupabase(); // Try to log in to Supabase again after reconnecting
  } else {
    Serial.println("Failed to reconnect to WiFi");
    digitalWrite(LED_PIN, LOW); // turn off LED to indicate WiFi disconnection
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    reconnectWiFi();
  }
}

//function to read sensors and convert to mA
void sensor_values_to_mA() {
  for(int i = 0; i < 64; i++) { // take multiple readings and average them to reduce noise
    delay(10);
    temp += analogReadMilliVolts(TEMP_PIN);
    ph_value += analogReadMilliVolts(PH_PIN);
    ec_float += analogReadMilliVolts(EC_PIN);
    // tds_value += analogReadMilliVolts(TDS_PIN);
    level_value += analogReadMilliVolts(LEVEL_PIN);
  }
  temp /= 64.0;
  ph_value /= 64.0;
  ec_float /= 64;
  // tds_value /= 64; 
  level_value /= 64.0;

  // convert to mA using the sensor's transfer function (example for a hypothetical sensor)
  temp = temp / 150; 
  ph_value = ph_value / 146;
  ec_float = ec_float / 146;
  tds_value = tds_value / 146;
  level_value = level_value / 146;
   Serial.printf("Temp: %.1f °C, pH: %.1f, EC: %.1f uS/cm, Level: %.1f%\n", temp, ph_value, ec_value, level_value);


  // convert to actual values
  temp = ((temp - 4.0) * 6.25) - 20.0;
  ph_value = (ph_value - 4.0) * 0.875;
  ec_float = 44000 * ((ec_float - 4.0) / 16.0);
  ec_value = (uint32_t)ec_float;
  tds_value = 0.6 * ec_value;
  level_value = mapFloat(level_value, 4, 20, 0, 50); // assuming level sensor gives 0-20mA for 0-100% level
 }


void restartESP(){
  delay(2000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Restarting...");
  delay(2000);
  ESP.restart();
}