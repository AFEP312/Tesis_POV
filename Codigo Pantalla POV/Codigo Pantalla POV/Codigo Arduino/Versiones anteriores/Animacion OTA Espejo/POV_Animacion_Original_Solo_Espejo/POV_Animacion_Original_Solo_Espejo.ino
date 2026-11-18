#include <FastLED.h>
#include "imgs.h"
#include <pgmspace.h>
#include <stdlib.h>
#include <stdio.h>

// ============================================================
// WIFI + OTA
// ============================================================

#include <WiFi.h>
#include <ArduinoOTA.h>

const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";

bool otaStarted = false;


// ============================================================
// ANIMACION
// ============================================================

/*
   Estos valores vienen automaticamente
   desde el imgs.h generado por Python.

   Ejemplo:

   #define POV_ORIGINAL_FRAMES 80
   #define POV_STORED_FRAMES 69
*/

const int NUM_IMAGES = POV_STORED_FRAMES;

const int ORIGINAL_IMAGES = POV_ORIGINAL_FRAMES;


// Frame realmente almacenado que estamos mostrando
volatile int cont = 0;


// Paso temporal de la animacion.
// Va de 0 hasta ORIGINAL_IMAGES - 1.
volatile int animation_step = 0;


// ============================================================
// SENSOR HALL
// ============================================================

const int HALL_SENSOR = 12;

volatile unsigned long lastTime = 0;

unsigned long threshold = 50;


// ============================================================
// PANTALLA POV
// ============================================================

int resolution = 6;

// 360 / 6 = 60 posiciones por vuelta
byte leds_samples = 360 / resolution;

volatile int leds_pos = 0;

volatile bool leds_pos_changed = false;


// ============================================================
// LEDS
// ============================================================

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


// ============================================================
// TIMER ESP32 CORE 3.x
// ============================================================

hw_timer_t* timer = NULL;

portMUX_TYPE timerMux =
    portMUX_INITIALIZER_UNLOCKED;

volatile float lap_time = 1000;

volatile float pulses_number = 0;


// ============================================================
// PROTOTIPOS
// ============================================================

void draw_leds(int leds_pos);

void IRAM_ATTR onTimer();

uint32_t expandColor(uint16_t color);

void startOTA();


// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(115200);

    delay(2000);


    // ========================================================
    // INFORMACION DE LA ANIMACION
    // ========================================================

    Serial.print("Frames originales: ");
    Serial.println(ORIGINAL_IMAGES);

    Serial.print("Frames almacenados: ");
    Serial.println(NUM_IMAGES);


    // ========================================================
    // SENSOR HALL
    // ========================================================

    pinMode(
        HALL_SENSOR,
        INPUT
    );


    // ========================================================
    // INICIALIZAR LEDS
    // ========================================================

    FastLED.addLeds<
        LEDS_TYPE,
        DATA_PIN_1,
        COLOR_ORDER
    >(
        leds1,
        NUM_LEDS
    );


    FastLED.addLeds<
        LEDS_TYPE,
        DATA_PIN_2,
        COLOR_ORDER
    >(
        leds2,
        NUM_LEDS
    );


    FastLED.addLeds<
        LEDS_TYPE,
        DATA_PIN_3,
        COLOR_ORDER
    >(
        leds3,
        NUM_LEDS
    );


    FastLED.addLeds<
        LEDS_TYPE,
        DATA_PIN_4,
        COLOR_ORDER
    >(
        leds4,
        NUM_LEDS
    );


    FastLED.setMaxPowerInVoltsAndMilliamps(
        5,
        1000
    );


    FastLED.setBrightness(
        BRIGHTNESS
    );


    // ========================================================
    // PRUEBA INICIAL
    // ========================================================

    for (
        int i = 0;
        i < NUM_LEDS;
        i++
    ) {

        leds1[i].setRGB(
            255,
            0,
            0
        );

        leds2[i].setRGB(
            0,
            0,
            0
        );

        leds3[i].setRGB(
            0,
            0,
            0
        );

        leds4[i].setRGB(
            0,
            0,
            0
        );

    }


    FastLED.show();


    // ========================================================
    // PUNTO AZUL
    // ========================================================

    delay(1000);


    leds1[0].setRGB(
        0,
        0,
        255
    );

    leds2[0].setRGB(
        0,
        0,
        255
    );

    leds3[0].setRGB(
        0,
        0,
        255
    );

    leds4[0].setRGB(
        0,
        0,
        255
    );


    FastLED.show();


    delay(3000);


    // ========================================================
    // TIMER
    // ========================================================

    timer =
        timerBegin(
            1000000
        );


    timerAttachInterrupt(
        timer,
        &onTimer
    );


    pulses_number =
        (lap_time * 1000) /
        leds_samples;


    timerAlarm(
        timer,
        pulses_number,
        true,
        0
    );


    // ========================================================
    // VERDE = LISTO
    // ========================================================

    leds1[0].setRGB(
        0,
        255,
        0
    );

    leds2[0].setRGB(
        0,
        255,
        0
    );

    leds3[0].setRGB(
        0,
        255,
        0
    );

    leds4[0].setRGB(
        0,
        255,
        0
    );


    FastLED.show();


    delay(5000);


    // ========================================================
    // WIFI
    // ========================================================

    /*
       OTA mínimo.

       No bloqueamos esperando la conexión.
       WiFi conecta en segundo plano.
    */

    WiFi.mode(
        WIFI_STA
    );


    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

}


// ============================================================
// LOOP
// ============================================================

void loop() {


    // ========================================================
    // OTA
    // ========================================================

    if (
        !otaStarted &&
        WiFi.status() == WL_CONNECTED
    ) {

        startOTA();

    }


    if (
        otaStarted
    ) {

        ArduinoOTA.handle();

    }


    // ========================================================
    // SENSOR HALL
    // ========================================================

    if (
        !digitalRead(HALL_SENSOR) &&
        (millis() - lastTime) > threshold
    ) {


        lap_time =
            millis() -
            lastTime;


        pulses_number =
            (lap_time * 1000) /
            leds_samples;


        timerAlarm(
            timer,
            pulses_number,
            true,
            0
        );


        lastTime =
            millis();

    }


    // ========================================================
    // POV
    // ========================================================

    if (
        leds_pos_changed
    ) {


        leds_pos_changed =
            false;


        if (
            leds_pos != 0
        ) {

            draw_leds(
                leds_pos
            );

        }


        FastLED.show();

    }

}


// ============================================================
// OTA MINIMO
// ============================================================

void startOTA() {


    ArduinoOTA.setHostname(
        "POV-ESP32"
    );


    ArduinoOTA.begin();


    otaStarted =
        true;

}


// ============================================================
// DIBUJAR LEDS
// ============================================================

void draw_leds(
    int leds_pos
) {


    int row =
        leds_pos /
        resolution;


    // ========================================================
    // UNICO CAMBIO: ESPEJO ANGULAR
    // ========================================================
    //
    // Se invierte solamente el orden de las 60 filas:
    // 0, 1, 2, ... 59  ->  0, 59, 58, ... 1
    //
    // No se modifica Hall, timer, lap_time, cont ni animation_step.
    //
    row =
        (leds_samples - row) %
        leds_samples;


    uint16_t color16 =
        0;


    uint32_t color24 =
        0;


    // ========================================================
    // TIRAS 1 Y 2
    // ========================================================

    for (
        int i = 0;
        i < NUM_LEDS;
        i++
    ) {


        // ----------------------------------------------------
        // TIRA 1
        // ----------------------------------------------------

        color16 =
            pgm_read_word(
                &(
                    frames
                    [cont]
                    [row]
                    [i]
                )
            );


        color24 =
            expandColor(
                color16
            );


        leds1[i] =
            color24;


        // ----------------------------------------------------
        // TIRA 2
        // ----------------------------------------------------

        color16 =
            pgm_read_word(
                &(
                    frames
                    [cont]
                    [row]
                    [NUM_LEDS + i]
                )
            );


        color24 =
            expandColor(
                color16
            );


        leds2[i] =
            color24;

    }


    // ========================================================
    // SEGUNDA MITAD
    // ========================================================

    // La fila opuesta continúa exactamente a 180 grados.
    row =
        (row + (leds_samples / 2)) %
        leds_samples;


    // ========================================================
    // TIRAS 3 Y 4
    // ========================================================

    for (
        int i = 0;
        i < NUM_LEDS;
        i++
    ) {


        // ----------------------------------------------------
        // TIRA 3
        // ----------------------------------------------------

        color16 =
            pgm_read_word(
                &(
                    frames
                    [cont]
                    [row]
                    [
                        2 *
                        NUM_LEDS -
                        i -
                        1
                    ]
                )
            );


        color24 =
            expandColor(
                color16
            );


        leds3[i] =
            color24;


        // ----------------------------------------------------
        // TIRA 4
        // ----------------------------------------------------

        color16 =
            pgm_read_word(
                &(
                    frames
                    [cont]
                    [row]
                    [
                        NUM_LEDS -
                        i -
                        1
                    ]
                )
            );


        color24 =
            expandColor(
                color16
            );


        leds4[i] =
            color24;

    }

}


// ============================================================
// RGB565 -> RGB888
// ============================================================

uint32_t expandColor(
    uint16_t color
) {


    uint16_t blue5 =
        0x001F &
        color;


    uint16_t green6 =
        (
            0x07E0 &
            color
        ) >>
        5;


    uint16_t red5 =
        (
            0xF800 &
            color
        ) >>
        11;


    uint32_t blue8 =
        (
            blue5 *
            527 +
            23
        ) >>
        6;


    uint32_t green8 =
        (
            green6 *
            259 +
            33
        ) >>
        6;


    uint32_t red8 =
        (
            red5 *
            527 +
            23
        ) >>
        6;


    uint32_t converted_color =
        (
            red8 <<
            16
        )
        +
        (
            green8 <<
            8
        )
        +
        blue8;


    return
        converted_color;

}


// ============================================================
// INTERRUPCION DEL TIMER
// ============================================================

void IRAM_ATTR onTimer() {


    leds_pos_changed =
        true;


    if (
        leds_pos < 350
    ) {


        leds_pos +=
            resolution;

    }
    else {


        // ====================================================
        // TERMINO UNA VUELTA
        // ====================================================

        leds_pos =
            0;


        // ====================================================
        // SIGUIENTE PASO TEMPORAL
        // ====================================================

        animation_step++;


        if (
            animation_step >=
            ORIGINAL_IMAGES
        ) {


            animation_step =
                0;

        }


        // ====================================================
        // MAPEAR PASOS TEMPORALES A FRAMES ALMACENADOS
        // ====================================================

        cont =
            (
                animation_step *
                NUM_IMAGES
            )
            /
            ORIGINAL_IMAGES;


        if (
            cont >=
            NUM_IMAGES
        ) {


            cont =
                NUM_IMAGES -
                1;

        }

    }

}
