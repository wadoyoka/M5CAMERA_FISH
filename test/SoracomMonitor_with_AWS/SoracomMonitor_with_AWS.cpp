#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <WireGuard-ESP32.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD.h>
#include <Update.h>

// Select camera model
#define CAMERA_MODEL_M5STACK_PSRAM // M5Stack with PSRAM
#include "camera_pins.h"

//WIFI 情報
const char *ssid = "**********";
const char *password = "**********";

// SORACOMのhttpエントリポイント情報
const char *serverUrl = "**********";

// SORACOMのmqqtエントリポイント情報
const char *mqtt_server = "***********";
const int mqtt_port = 9999;
const char *mqtt_topic = "********";

// SORACOM ArcのWireGuard情報
const char *private_key = "***********";
IPAddress local_ip(0, 0, 0, 0); // SORACOM ArcのIPアドレス (例)
const char *peer_public_key = "***********************";
const char *endpoint_address = "**********************";
const int endpoint_port = 9999; // WireGuardのデフォルトポート

WiFiClient espClient;
PubSubClient client(espClient);

WireGuard wg;

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
    configTime(9 * 60 * 60, 0, "ntp.jst.mfeed.ad.jp", "ntp.nict.jp", "time.google.com");
    delay(3000);
    wg.begin(local_ip, private_key, endpoint_address, peer_public_key, endpoint_port);
    Serial.println("Connected to SORACOM Arc");
}

void sendImageToSoracomFunk()
{
    camera_fb_t *fb = NULL;
    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = NULL;
    fb = esp_camera_fb_get();

    if (!fb)
    {
        Serial.println("Camera capture failed");
        ESP.restart();
        return;
    }

    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/octet-stream");

    int httpResponseCode = http.POST(fb->buf, fb->len);

    if (httpResponseCode > 0)
    {
        Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    }
    else
    {
        Serial.printf("Error code: %d\n", httpResponseCode);
    }

    http.end();
    esp_camera_fb_return(fb);

}

void callback(char *topic, byte *payload, unsigned int length)
{
    StaticJsonDocument<200> slackMessage;
    DeserializationError error = deserializeJson(slackMessage, payload, length);

    if (error)
    {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }

    const char *message = slackMessage["message"];
    if (strcmp(message, "photo") == 0)
    {
        sendImageToSoracomFunk();
    }
}

void setup()
{
    Serial.begin(115200);

    if (!psramInit())
    {
        Serial.println("PSRAM initialization failed");
        return;
    }

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

    if (psramFound())
    {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 1;
    }
    else
    {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }

    setup_wifi();
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
