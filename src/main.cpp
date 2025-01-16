#include "esp_camera.h"
#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// Select camera model
#define CAMERA_MODEL_M5STACK_PSRAM // M5Stack with PSRAM
#include "camera_pins.h"

// Wi-Fi credentials
const char *ssid = "*******";        // Replace with your Wi-Fi SSID
const char *password = "**********"; // Replace with your Wi-Fi password

// Firebase project credentials
#define STORAGE_BUCKET_ID "*********" // Replace with your Storage Bucket ID
#define API_KEY "***************"           // Replace with your Firebase API Key
#define USER_EMAIL "**************"                        // Optional user email for authentication
#define USER_PASSWORD "*************"                                   // Optional user password for authentication

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig firebaseConfig;

void startCameraServer();

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();

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

    if (psramFound())
    {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    }
    else
    {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return;
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

    // firebaseConfig.storage_bucket_id = STORAGE_BUCKET_ID;

    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;

    Firebase.begin(&firebaseConfig, &auth);
    Firebase.reconnectWiFi(true);

    Serial.println("Firebase initialized");
    
}

void fcsUploadCallback(FCS_UploadStatusInfo info)
{
    if (info.status == firebase_fcs_upload_status_init)
    {
        Serial.printf("Uploading file %s (%d) to %s\n", info.localFileName.c_str(), info.fileSize, info.remoteFileName.c_str());
    }
    else if (info.status == firebase_fcs_upload_status_upload)
    {
        Serial.printf("Uploaded %d%s, Elapsed time %d ms\n", (int)info.progress, "%", info.elapsedTime);
    }
    else if (info.status == firebase_fcs_upload_status_complete)
    {
        Serial.println("Upload completed\n");
        FileMetaInfo meta = fbdo.metaData();
        Serial.printf("Name: %s\n", meta.name.c_str());
        Serial.printf("Bucket: %s\n", meta.bucket.c_str());
        Serial.printf("contentType: %s\n", meta.contentType.c_str());
        Serial.printf("Size: %d\n", meta.size);
        Serial.printf("Generation: %lu\n", meta.generation);
        Serial.printf("Metageneration: %lu\n", meta.metageneration);
        Serial.printf("ETag: %s\n", meta.etag.c_str());
        Serial.printf("CRC32: %s\n", meta.crc32.c_str());
        Serial.printf("Tokens: %s\n", meta.downloadTokens.c_str());
        Serial.printf("Download URL: %s\n\n", fbdo.downloadURL().c_str());
    }
    else if (info.status == firebase_fcs_upload_status_error)
    {
        Serial.printf("Upload failed, %s\n", info.errorMsg.c_str());
    }
}

void uploadImageToFirebase()
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("Camera capture failed");
        return;
    }

    String path = "/images/" + String(millis()) + ".jpg"; // Unique path for each image

    Serial.println("Uploading to Firebase...");
    if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, fb->buf, fb->len, path.c_str(), "image/jpeg", fcsUploadCallback))
    {
        Serial.println("Upload successful: " + fbdo.downloadURL());
    }
    else
    {
        Serial.println("Upload failed: " + fbdo.errorReason());
    }

    esp_camera_fb_return(fb);
}

void loop()
{
    uploadImageToFirebase();
    delay(60000); // Wait for 1 minute
}

