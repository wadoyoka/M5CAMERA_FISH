#include <iostream>
#include <string>
#include "esp_camera.h"
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <ArduinoJson.h> // FirebaseJson を扱いやすくするため
#include <HTTPClient.h>
#include <base64.h>
#include <time.h>
#include <PubSubClient.h>
#include <WireGuard-ESP32.h>

// Select camera model
#define CAMERA_MODEL_M5STACK_PSRAM // M5Stack with PSRAM
#include "camera_pins.h"

const char *ssid = "WIFI";
const char *password = "WIFI";
const char *mqtt_server = "YOURSERVER";
const int mqtt_port = 99999;
const char *mqtt_topic = "YOURTOPIC";

WiFiClient espClient;
PubSubClient client(espClient);

// SORACOM ArcのWireGuard情報
const char *private_key = "YOUR_SORACOM_ARC_PRIVATE_KEY";
IPAddress local_ip(10, 0, 0, 1); // SORACOM ArcのIPアドレス (例)
const char *peer_public_key = "YOUR_SORACOM_ARC_PUBLIC_KEY";
const char *endpoint_address = "YOUR_SORACOM_ARC_ENDPOINT.arc.soracom.io";
const int endpoint_port = 11010; // WireGuardのデフォルトポート

WireGuard wg;

// Firebase project credentials
#define FIREBASE_PROJECT_ID "secret"              // プロジェクトID
#define STORAGE_BUCKET_ID "********" // Replace with your Storage Bucket ID
#define API_KEY "*******"            // Replace with your Firebase API Key
#define USER_EMAIL "********"        // Optional user email for authentication
#define USER_PASSWORD "********"     // Optional user password for authentication

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig firebaseConfig;

void syncTime()
{
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print("Waiting for time synchronization...");
    while (time(nullptr) < 8 * 3600 * 2)
    { // UNIX時間の初期値が1970年のため
        Serial.print(".");
        delay(1000);
    }
    Serial.println("Time synchronized.");
}

void setup_wifi()
{
    delay(10);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    // Google Public DNSを設定
    IPAddress dns(8, 8, 8, 8);
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns);
    // DNSが利用可能になるまで待機
    delay(1000);
}

void connectToWireGuard()
{
    Serial.println("Connecting to SORACOM Arc...");

    Serial.print("Adjusting system time: ");
    configTime(9 * 60 * 60, 0, "ntp.jst.mfeed.ad.jp", "ntp.nict.jp", "time.google.com");
    delay(3000);
    Serial.println("done.");

    Serial.print("Connect to SORACOM Arc (WireGuard):");
    wg.begin(local_ip, private_key, endpoint_address, peer_public_key, endpoint_port);
    Serial.println("done.");
}

bool uploadImageToFirebase(camera_fb_t *fb)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi not connected");
        return false;
    }

    char path[32];
    snprintf(path, sizeof(path), "/images/CPS.jpg");

    Serial.println("Uploading image...");
    if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, fb->buf, fb->len, path, "image/jpeg"))
    {
        Serial.println("Image uploaded successfully");
        return true;
    }
    else
    {
        Serial.println("Image upload failed");
        return false;
    }
}

bool reconnectWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return true; // Wi-Fiが接続済みの場合、何もしない
    }

    Serial.println("WiFi disconnected. Attempting to reconnect...");

    WiFi.disconnect();
    WiFi.begin(ssid, password);

    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 10000; // 再接続タイムアウト (10秒)

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout)
    {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nWiFi reconnected successfully");
        return true;
    }
    else
    {
        Serial.println("\nWiFi reconnection failed. Retrying in 5 seconds...");
        return false;
    }
}

void take_photo()
{
    camera_fb_t *fb = NULL;
    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = NULL;
    fb =esp_camera_fb_get();

    if (!fb)
    {
        Serial.println("Camera capture failed");
        ESP.restart();
        return;
    }
    // Wi-Fiの接続状態をチェックし、再接続を試みる
    if (reconnectWiFi())
    {
        Serial.println("============Update Mode=========");
        if (uploadImageToFirebase(fb))
        {
            delay(2000);
        }
        delay(2000);
    }
    esp_camera_fb_return(fb);
}

void callback(char *topic, byte *payload, unsigned int length)
{
    StaticJsonDocument<200> slackMessage; // 必要に応じてサイズを調整
    deserializeJson(slackMessage, payload);
    const char *message = slackMessage["message"];
    Serial.println(message == "photo");

    if (strcmp(message, "photo") == 0)
    {
        take_photo();
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println();

    // Initialize PSRAM
    if (!psramInit())
    {
        Serial.println("PSRAM initialization failed");
        return;
    }

    // Camera configuration
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    // Optimize camera settings for stability and quality
    if (psramFound())
    {
        config.frame_size = FRAMESIZE_SVGA; // 解像度800x600
        config.jpeg_quality = 10;           // 高品質
        config.fb_count = 1;                // 複数バッファを使用
    }
    else
    {
        config.frame_size = FRAMESIZE_VGA; // 解像度640x480
        config.jpeg_quality = 12;          // 低品質
        config.fb_count = 1;               // バッファ数を1に制限
    }

    if (esp_camera_init(&config) != ESP_OK)
    {
        Serial.println("Camera init failed");
        return;
    }

    // Adjust camera sensor settings
    sensor_t *s = esp_camera_sensor_get();
    if (s)
    {
        s->set_brightness(s, 0);
        s->set_contrast(s, 1);
        s->set_saturation(s, 0);
        s->set_sharpness(s, 1);
    }

    // Connect to Wi-Fi
    setup_wifi();

    // Initialize Firebase
    firebaseConfig.api_key = API_KEY;
    firebaseConfig.token_status_callback = tokenStatusCallback;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;

    Firebase.begin(&firebaseConfig, &auth);
    Firebase.reconnectWiFi(true);
    syncTime();
    Serial.println("Firebase initialized");

    connectToWireGuard();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
}

void reconnect()
{
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        if (client.connect("M5StackClient"))
        {
            Serial.println("connected");
            client.subscribe(mqtt_topic);
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

void loop()
{

    if (!client.connected())
    {
        reconnect();
    }
    client.loop();
}
