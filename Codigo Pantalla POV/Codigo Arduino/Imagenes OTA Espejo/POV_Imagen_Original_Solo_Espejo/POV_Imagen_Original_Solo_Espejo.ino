#include <FastLED.h>
#include "imgs.h"
#include <pgmspace.h>

// ===== OTA =====
#include <WiFi.h>
#include <ArduinoOTA.h>

// ================================
// CONFIGURACION WIFI
// ================================

const char* WIFI_SSID = "ANDRES";
const char* WIFI_PASSWORD = "Angelica2012";

bool otaStarted = false;


// ================================
// Imagen única
// ================================

#define IMAGE_NUMBER 5


// ================================
// Sensor Hall
// ================================

const int HALL_SENSOR = 12;

volatile unsigned long lastTime = 0;

unsigned long threshold = 50;


// ================================
// POV
// ================================

int resolution = 6;

byte leds_samples = 360 / resolution;

volatile int leds_pos = 0;

volatile bool leds_pos_changed = false;


// ================================
// LEDS
// ================================

#define NUM_LEDS 50

#define LEDS_TYPE WS2812B

#define BRIGHTNESS 10

#define COLOR_ORDER GRB

#define DATA_PIN_1 19
#define DATA_PIN_2 21
#define DATA_PIN_3 22
#define DATA_PIN_4 23


CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];
CRGB leds3[NUM_LEDS];
CRGB leds4[NUM_LEDS];


// ================================
// Timer ESP32 Core 3.x
// ================================

hw_timer_t * timer = NULL;

volatile float lap_time = 1000;

volatile float pulses_number = 0;


// ================================
// Funciones
// ================================

void draw_leds(int leds_pos);

void IRAM_ATTR onTimer();

uint32_t expandColor(uint16_t color);

// ===== OTA =====
void startOTA();


// ============================================================
// SETUP
// ============================================================

void setup(){

    Serial.begin(115200);

    delay(2000);

    pinMode(HALL_SENSOR, INPUT);


    // ================================
    // FASTLED
    // ================================

    FastLED.addLeds<LEDS_TYPE, DATA_PIN_1, COLOR_ORDER>(
        leds1,
        NUM_LEDS
    );

    FastLED.addLeds<LEDS_TYPE, DATA_PIN_2, COLOR_ORDER>(
        leds2,
        NUM_LEDS
    );

    FastLED.addLeds<LEDS_TYPE, DATA_PIN_3, COLOR_ORDER>(
        leds3,
        NUM_LEDS
    );

    FastLED.addLeds<LEDS_TYPE, DATA_PIN_4, COLOR_ORDER>(
        leds4,
        NUM_LEDS
    );


    FastLED.setBrightness(BRIGHTNESS);


    // ================================
    // Prueba inicial roja
    // ================================

    for(int i = 0; i < NUM_LEDS; i++){

        leds1[i] = CRGB(255, 0, 0);

        leds2[i] = CRGB(0, 0, 0);

        leds3[i] = CRGB(0, 0, 0);

        leds4[i] = CRGB(0, 0, 0);
    }

    FastLED.show();


    delay(1000);


    // ================================
    // Punto azul referencia
    // ================================

    leds1[0] = CRGB(0, 0, 255);

    leds2[0] = CRGB(0, 0, 255);

    leds3[0] = CRGB(0, 0, 255);

    leds4[0] = CRGB(0, 0, 255);


    FastLED.show();


    delay(3000);


    // ================================
    // TIMER
    // ================================

    timer = timerBegin(1000000);

    timerAttachInterrupt(timer, &onTimer);


    pulses_number =
        (lap_time * 1000) / leds_samples;


    timerAlarm(
        timer,
        pulses_number,
        true,
        0
    );


    // ================================
    // Verde inicio
    // ================================

    leds1[0] = CRGB(0, 255, 0);

    leds2[0] = CRGB(0, 255, 0);

    leds3[0] = CRGB(0, 255, 0);

    leds4[0] = CRGB(0, 255, 0);


    FastLED.show();


    delay(5000);


    // ========================================================
    // ===== OTA - SOLO INICIAMOS WIFI ========================
    // ========================================================

    Serial.println();
    Serial.println("==============================");
    Serial.println("Iniciando WiFi para OTA...");
    Serial.println("==============================");

    WiFi.mode(WIFI_STA);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    /*
       IMPORTANTE:

       NO hacemos esto:

       while(WiFi.status() != WL_CONNECTED)

       porque eso bloquearía el POV si
       el WiFi no existe.

       La conexión ocurre en segundo plano.
    */

    Serial.println("WiFi intentando conectar en segundo plano.");
    Serial.println("El POV continua funcionando normalmente.");

}


// ============================================================
// LOOP
// ============================================================

void loop(){


    // ========================================================
    // ===== OTA ==============================================
    // ========================================================

    /*
       Si el WiFi consiguió conectarse
       iniciamos OTA UNA SOLA VEZ.
    */

    if(
        !otaStarted &&
        WiFi.status() == WL_CONNECTED
    ){

        startOTA();

    }


    /*
       Si OTA ya fue iniciado,
       atendemos las solicitudes.
    */

    if(otaStarted){

        ArduinoOTA.handle();

    }


    // ========================================================
    // CODIGO POV ORIGINAL
    // ========================================================

    if(
        !digitalRead(HALL_SENSOR) &&
        (millis() - lastTime) > threshold
    ){

        lap_time =
            millis() - lastTime;


        pulses_number =
            (lap_time * 1000) /
            leds_samples;


        timerAlarm(
            timer,
            pulses_number,
            true,
            0
        );


        lastTime = millis();

    }



    if(leds_pos_changed){

        leds_pos_changed = false;


        if(leds_pos != 0){

            draw_leds(leds_pos);

        }


        FastLED.show();

    }

}


// ============================================================
// OTA
// ============================================================

void startOTA(){

    Serial.println();
    Serial.println("==============================");
    Serial.println("WIFI CONECTADO");
    Serial.println("==============================");

    Serial.print("IP: ");

    Serial.println(
        WiFi.localIP()
    );


    // Nombre que veremos en Arduino IDE

    ArduinoOTA.setHostname(
        "POV-ESP32"
    );


    ArduinoOTA.onStart([](){

        Serial.println();
        Serial.println("==============================");
        Serial.println("INICIANDO ACTUALIZACION OTA");
        Serial.println("==============================");

    });


    ArduinoOTA.onEnd([](){

        Serial.println();
        Serial.println("==============================");
        Serial.println("OTA COMPLETADO");
        Serial.println("==============================");

    });


    ArduinoOTA.onProgress(
        [](unsigned int progress,
           unsigned int total){

            Serial.printf(
                "OTA: %u%%\r",
                (progress * 100) / total
            );

        }
    );


    ArduinoOTA.onError(
        [](ota_error_t error){

            Serial.printf(
                "Error OTA [%u]: ",
                error
            );


            if(error == OTA_AUTH_ERROR){

                Serial.println(
                    "Error autenticacion"
                );

            }
            else if(error == OTA_BEGIN_ERROR){

                Serial.println(
                    "Error inicio"
                );

            }
            else if(error == OTA_CONNECT_ERROR){

                Serial.println(
                    "Error conexion"
                );

            }
            else if(error == OTA_RECEIVE_ERROR){

                Serial.println(
                    "Error recepcion"
                );

            }
            else if(error == OTA_END_ERROR){

                Serial.println(
                    "Error finalizacion"
                );

            }

        }
    );


    ArduinoOTA.begin();


    otaStarted = true;


    Serial.println();
    Serial.println("OTA LISTO");
    Serial.println();

    Serial.print("Nombre: ");
    Serial.println("POV-ESP32");

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    Serial.println();
    Serial.println(
        "Ya puedes cargar codigo por WiFi."
    );

}


// ============================================================
// DIBUJAR LEDS
// ============================================================

void draw_leds(int leds_pos){

    int row =
        leds_pos / resolution;


    // ========================================================
    // UNICO CAMBIO: ESPEJO ANGULAR
    // ========================================================
    //
    // El codigo original recorria:
    // 0, 1, 2, ... 59
    //
    // Ahora recorre:
    // 0, 59, 58, ... 1
    //
    // No se modifica Hall, timer, lap_time ni sincronizacion.
    //
    row =
        (leds_samples - row) %
        leds_samples;


    uint16_t color16;

    uint32_t color24;


    for(int i = 0; i < NUM_LEDS; i++){

        color16 =
            pgm_read_word(
                &(frame5[row][i])
            );


        color24 =
            expandColor(color16);


        leds1[i] =
            color24;



        color16 =
            pgm_read_word(
                &(frame5[row][NUM_LEDS + i])
            );


        color24 =
            expandColor(color16);


        leds2[i] =
            color24;

    }


    // La fila opuesta sigue estando exactamente a 180 grados.
    row =
        (row + (leds_samples / 2)) %
        leds_samples;


    for(int i = 0; i < NUM_LEDS; i++){


        color16 =
            pgm_read_word(
                &(frame5[row][2 * NUM_LEDS - i - 1])
            );


        color24 =
            expandColor(color16);


        leds3[i] =
            color24;



        color16 =
            pgm_read_word(
                &(frame5[row][NUM_LEDS - i - 1])
            );


        color24 =
            expandColor(color16);


        leds4[i] =
            color24;

    }

}


// ============================================================
// RGB565 -> RGB888
// ============================================================

uint32_t expandColor(uint16_t color){

    uint16_t blue5 =
        color & 0x001F;


    uint16_t green6 =
        (color & 0x07E0) >> 5;


    uint16_t red5 =
        (color & 0xF800) >> 11;



    uint32_t blue8 =
        (blue5 * 527 + 23) >> 6;


    uint32_t green8 =
        (green6 * 259 + 33) >> 6;


    uint32_t red8 =
        (red5 * 527 + 23) >> 6;



    return
        (red8 << 16) +
        (green8 << 8) +
        blue8;

}


// ============================================================
// TIMER
// ============================================================

void IRAM_ATTR onTimer(){

    leds_pos_changed = true;


    if(leds_pos < 350){

        leds_pos +=
            resolution;

    }
    else{

        leds_pos = 0;

    }

}
