#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ESPSupabase.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

Supabase db;
Preferences userprefs;
AsyncWebServer server(80);

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
String email, password, ssid, wifiPassword;

// Map analog pins to their names
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

// custom lcd characters
byte wifi[] = {0b00000, 0b00100, 0b01010, 0b10001, 0b00100, 0b01010, 0b10001, 0b00100};

bool deviceConnected = false;
bool oldDeviceConnected = false;

uint32_t value = 0;

LiquidCrystal_PCF8574 lcd(0x27); // set the LCD address to 0x27 for a 16 chars and 2 line display

// server routes
void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}

// save user credentials to preferences
void saveCredentials(const String &email, const String &password, const String &ssid, const String &wifiPassword)
{
  userprefs.begin("credentials", false);
  userprefs.putString("email", email);
  userprefs.putString("password", password);
  userprefs.putString("ssid", ssid);
  userprefs.putString("wifiPassword", wifiPassword);
  userprefs.end();
}

// html template for login page with CSS styling
const char *settingsPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Settings</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background-color: #f4f4f4;
            margin: 0;
            padding: 20px;
        }
        .container {
            max-width: 400px;
            margin: 0 auto;
            background: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        h1 {
            text-align: center;
            color: #333;
        }
        h2 {
            color: #555;
            border-bottom: 1px solid #ddd;
            padding-bottom: 5px;
        }
        label {
            display: block;
            margin-top: 10px;
            color: #333;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 8px;
            margin-top: 5px;
            border: 1px solid #ccc;
            border-radius: 4px;
        }
        button {
            width: 100%;
            padding: 10px;
            background-color: #4CAF50;
            color: white;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            margin-top: 20px;
        }
        button:hover {
            background-color: #45a049;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Settings</h1>
        <h2>Account</h2>
        <label for="email">Email:</label>
        <input type="text" id="email" placeholder="Enter email">
        <label for="password">Password:</label>
        <input type="password" id="password" placeholder="Enter password">
        <h2>WiFi Settings</h2>
        <label for="ssid">WiFi Name:</label>
        <input type="text" id="ssid" placeholder="Enter WiFi SSID">
        <label for="wifiPassword">WiFi Password:</label>
        <input type="password" id="wifiPassword" placeholder="Enter WiFi password">
        <button onclick="syncSettings()">Sync Setting</button>
    </div>
    <script>
        function syncSettings() {
            const email = document.getElementById('email').value;
            const password = document.getElementById('password').value;
            const ssid = document.getElementById('ssid').value;
            const wifiPassword = document.getElementById('wifiPassword').value;
            const params = new URLSearchParams({
                email: email,
                password: password,
                ssid: ssid,
                wifiPassword: wifiPassword
            });
            const baseUrl = window.location.origin; // Get the base URL of the current page
            const url = baseUrl + '/save?' + params.toString();
            console.log('Sending settings to server:', url);
            fetch(url, {
                method: 'GET'
            }).then(response => {
                if (response.ok) {
                    alert('Settings saved successfully!');
                } else {
                    alert('Failed to save settings.');
                }
            }).catch(error => {
                console.error('Error:', error);
                alert('Error saving settings.');
            });
        }
    </script>
</body>
</html>
)rawliteral";

//settings page route
void handleSettings(AsyncWebServerRequest *request)
{
  request->send(200, "text/html", settingsPage);
}

// function templates
bool sensor_values_to_mA();
void checkWiFiConnection();
void reconnectWiFi();
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max);
bool setupSupabase();
void restartESP();

void setup()
{
  Serial.begin(9600);

  Serial.println("Probing for PCF8574 on address 0x27...");

  Wire.begin();
  Wire.beginTransmission(0x27);
  int error = Wire.endTransmission();
  Serial.print("Error: ");
  Serial.print(error);

  if (error == 0)
  {
    Serial.println(": LCD found.");

    lcd.begin(20, 4);
    lcd.createChar(0, wifi);
  }
  else
  {
    Serial.println(": LCD not found.");
  } // if

  lcd.setBacklight(255);

  pinMode(LED_PIN, OUTPUT);

  //load credentials from preferences
  userprefs.begin("credentials", false);
  email = userprefs.getString("email", DEFAULT_EMAIL);
  password = userprefs.getString("password", DEFAULT_PASSWORD);
  ssid = userprefs.getString("ssid", WIFI_SSID);
  wifiPassword = userprefs.getString("wifiPassword", WIFI_PASSWORD);

  Serial.printf("Loaded credentials - Email: %s, SSID: %s\n", email.c_str(), ssid.c_str());

  // WiFi optimization
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("HydroSense_Setup", "password123"); // Start AP for setup
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  //begin server and define routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", settingsPage);
  });
  // server.on("/", HTTP_GET, handleSettings); 
  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    String email, password, ssid, wifiPassword;
    if (request->hasParam("email") && request->hasParam("password") && request->hasParam("ssid") && request->hasParam("wifiPassword"))
    {
      email = request->getParam("email")->value();
      password = request->getParam("password")->value();
      ssid = request->getParam("ssid")->value();
      wifiPassword = request->getParam("wifiPassword")->value();
      saveCredentials(email, password, ssid, wifiPassword);
      request->send(200, "text/plain", "Settings saved successfully!");
      restartESP();
    }
    else
    {
      request->send(400, "text/plain", "Missing parameters");
    }
  });
  server.onNotFound(notFound);
  server.begin();

  // connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, wifiPassword);
  unsigned long startAttemptTime = millis();

  lcd.setCursor(0, 0);
  lcd.print("Connecting to WiFi..");
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS)
  {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
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
    if (loggedIn)
    {
      lcd.setCursor(0, 3);
      lcd.print("Login Success!");
    }
    else
    {
      lcd.setCursor(0, 3);
      lcd.print("Login Failed!");
      restartESP();
    }
    // Initialize Supabase client
  }
  else
  {
    lcd.setCursor(0, 1);
    lcd.print("Failed to connect");
    Serial.println("Failed to connect to WiFi");
    wifiConnected = false;
    return;
    // restartESP();
  }

} // setup()

uint32_t lastSensorReadTime = 0;
String jsonStr;

void loop()
{
  // Check WiFi connection and reconnect if necessary
  checkWiFiConnection();

  // read sensors and update LCD
  if (millis() - lastSensorReadTime > 10000)
  { // read sensors every 10 seconds
    lastSensorReadTime = millis();
    if (!sensor_values_to_mA())
    {
      // Serial.println("Failed to read sensor values.");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Sensor error!");
      lcd.setCursor(0, 1);
      lcd.print("Check connections");
      return;
    }
    if (wifiConnected)
    {
      lcd.setCursor(19, 0);
      lcd.write((uint8_t)0); // custom wifi character

      JsonDocument doc;
      doc["ph"] = ph_value;       // ph_value;
      doc["temp"] = temp;         // temp;
      doc["ec"] = ec_value;       // ec_value;
      doc["tds"] = tds_value;     // tds_value;
      doc["level"] = level_value; // level_value;
      doc["user_id"] = userId;    // Include user ID in the data being uploaded

      serializeJson(doc, jsonStr);
      // SerialBT.println(jsonStr); // Send JSON string over Bluetooth for debugging
      Serial.println("Uploading data to Supabase...");
      int code = db.insert(SENSOR_TABLE, jsonStr, false);
      if (code == 201)
      {
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
  }

} // loop()

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

bool setupSupabase()
{
  Serial.println("Initializing Supabase client...");

  // Check WiFi status before attempting login
  if (WiFi.status() != WL_CONNECTED)
  {
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

  while (retryCount < maxRetries)
  {
    Serial.printf("Login attempt %d/%d...\n", retryCount + 1, maxRetries);
    Serial.printf("Free heap before login: %d bytes\n", ESP.getFreeHeap());

    int loginResult = db.login_email(email, password);

    Serial.printf("Free heap after login: %d bytes\n", ESP.getFreeHeap());

    if (loginResult == 200)
    {
      Serial.println("Logged in to Supabase successfully");
      userId = db.getUserId();
      Serial.println("User ID: " + userId);
      return true;
    }
    else
    {
      Serial.printf("Login attempt %d failed with code: %d\n", retryCount + 1, loginResult);
      retryCount++;

      if (retryCount < maxRetries)
      {
        Serial.println("Waiting 2 seconds before retry...");
        delay(2000); // Wait before retry
      }
    }
  }

  Serial.println("Failed to log in to Supabase after all retries");
  return false;
}

void reconnectWiFi()
{
  Serial.println("Reconnecting to WiFi...");
  WiFi.disconnect();
  WiFi.begin(ssid, wifiPassword);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS)
  {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Reconnected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_PIN, HIGH); // turn on LED to indicate WiFi connection
    wifiConnected = true;
    loggedIn = setupSupabase(); // Try to log in to Supabase again after reconnecting
  }
  else
  {
    Serial.println("Failed to reconnect to WiFi");
    digitalWrite(LED_PIN, LOW); // turn off LED to indicate WiFi disconnection
  }
}

void checkWiFiConnection()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    reconnectWiFi();
  }
}

// function to read sensors and convert to mA
bool sensor_values_to_mA()
{
  for (int i = 0; i < 64; i++)
  { // take multiple readings and average them to reduce noise
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
  temp = temp / 146.1;
  ph_value = ph_value / 145.6;
  ec_float = ec_float / 141.2;
  tds_value = tds_value / 146;
  level_value = level_value / 146.2;

  Serial.printf("Temp: %.1f °C, pH: %.1f, EC: %.1f uS/cm, Level: %.1f%\n", temp, ph_value, ec_value, level_value);

  
  // convert to actual values
  temp = ((temp - 4.0) * 6.25) - 20.0;
  ph_value = (ph_value - 4.0) * 0.875;
  ec_float = 44000 * ((ec_float - 4.0) / 16.0);
  ec_float = (ec_float < 0) ? 0 : ec_float; // Clamp to 0 if negative
  ec_value = (uint32_t)ec_float;
  tds_value = 0.6 * ec_value;
  level_value = mapFloat(level_value, 4, 20, 0, 50); // assuming level sensor gives 0-20mA for 0-100% level
  
  if (temp < 0 || temp > 80 || ph_value < 0 || ph_value > 14 || ec_value < 0 || ec_value > 50000 || level_value < 0 || level_value > 100)
  {
    // Serial.println("Sensor reading out of range, discarding...");
    return false;
  }
  return true;
}

void restartESP()
{
  delay(2000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Restarting...");
  delay(2000);
  ESP.restart();
}