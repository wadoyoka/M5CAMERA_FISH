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

// Select camera model
#define CAMERA_MODEL_M5STACK_PSRAM // M5Stack with PSRAM
#include "camera_pins.h"

const char *ssid = "*******";     // Replace with your Wi-Fi SSID
const char *password = "*******"; // Replace with your Wi-Fi password

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

bool getLatestData()
{
    if (Firebase.ready())
    {
        String documentPath = "slackMessage";

        Serial.print("Querying Firestore for latest data...");

        if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str()))
        {
            Serial.println("Query successful!");
            // Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
            // Firebase JSON からドキュメントIDを取得
            DynamicJsonDocument doc(2048); // 必要に応じてサイズを調整
            deserializeJson(doc, fbdo.payload().c_str());

            // document.name フィールドの確認
            if (!doc["documents"].isNull() && doc["documents"].size() > 0)
            {
                String documentStatus = doc["documents"][0]["fields"]["status"]["booleanValue"].as<String>();
                Serial.println("JsonParse successful!");
                return documentStatus.equals("true");
            }
            else
            {
                Serial.println("No documents found or documents array is empty!");
                return false;
            }
        }
        else
        {
            Serial.println("Query failed: ");
            Serial.println(fbdo.errorReason());
            return false;
        }
    }
    else
    {
        Serial.println("Firebase is not ready");
        return false;
    }
}

void updateStatus()
{
    if (Firebase.ready())
    {
        String collection = "slackMessage";
        String documentPath = "/slackStatus";
        documentPath = collection + documentPath;
        FirebaseJson content;

        content.set("fields/status/booleanValue", false);

        // まずは更新を試みる
        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), ""))
        {
            Serial.println("Data updated in Firestore successfully!");
        }
        else
        {
            Serial.println("Data updated in Firestore Failed");
        }
    }
    else
    {
        Serial.println("Firebase is not ready");
    }
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
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    // Initialize Firebase
    firebaseConfig.api_key = API_KEY;
    firebaseConfig.token_status_callback = tokenStatusCallback;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;

    Firebase.begin(&firebaseConfig, &auth);
    Firebase.reconnectWiFi(true);
    syncTime();
    Serial.println("Firebase initialized");
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

void loop()
{
    delay(5000);
    camera_fb_t *fb = NULL;
    fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("Camera capture failed");
        esp_camera_fb_return(fb);
        return;
    }
    // Wi-Fiの接続状態をチェックし、再接続を試みる
    if (reconnectWiFi())
    {
        if (!fb)
        {
            Serial.println("Camera capture failed");
            return;
        }

        if (getLatestData())
        {
            Serial.println("============Update Mode=========");

            if (uploadImageToFirebase(fb))
            {
                delay(2000);
                updateStatus();
            }
            delay(2000);
        }
    }
    esp_camera_fb_return(fb);
}
