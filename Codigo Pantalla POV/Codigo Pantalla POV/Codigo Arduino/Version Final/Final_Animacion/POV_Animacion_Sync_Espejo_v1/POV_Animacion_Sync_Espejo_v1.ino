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

const int NUM_IMAGES = POV_STORED_FRAMES;

const int ORIGINAL_IMAGES = POV_ORIGINAL_FRAMES;

volatile int cont = 0;

volatile int animation_step = 0;


// ============================================================
// SENSOR HALL
// ============================================================

const int HALL_SENSOR = 12;

// El Hall pasa a ser la referencia física de 0° en cada revolución.
const uint32_t HALL_MIN_INTERVAL_US = 50000;
const uint32_t HALL_MAX_VALID_LAP_US = 180000;
const uint32_t INITIAL_LAP_US = 100000; // ~600 RPM

volatile uint32_t hallLastUs = 0;
volatile uint32_t hallEventUs = 0;
volatile uint32_t hallMeasuredLapUs = 0;
volatile bool hallEvent = false;

uint32_t filteredLapUs = INITIAL_LAP_US;
bool lapFilterInitialized = false;


// ============================================================
// PANTALLA POV
// ============================================================

int resolution = 6;

byte leds_samples = 360 / resolution;

volatile int leds_pos = 0;

volatile bool leds_pos_changed = false;

// Corrige el espejo invirtiendo únicamente el recorrido angular.
const bool REVERSE_ANGULAR_DIRECTION = true;

// Corrección fija opcional en pasos de 6°.
const int ANGULAR_OFFSET_STEPS = 0;


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

volatile uint32_t pulses_number =
    (INITIAL_LAP_US + 30) / 60;


// ============================================================
// PROTOTIPOS
// ============================================================

void draw_leds(int leds_pos);

void IRAM_ATTR onTimer();
void IRAM_ATTR onHall();

void processHallEvent();
void advanceAnimation();

int getImageRow(int position);

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
        (INITIAL_LAP_US + (leds_samples / 2)) /
        leds_samples;


    timerAlarm(
        timer,
        pulses_number,
        true,
        0
    );


    // ========================================================
    // INTERRUPCION HALL
    // ========================================================

    attachInterrupt(
        digitalPinToInterrupt(HALL_SENSOR),
        onHall,
        FALLING
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
    // SINCRONIZACION HALL
    // ========================================================

    processHallEvent();


    // ========================================================
    // POV
    // ========================================================

    int positionToDraw = -1;

    portENTER_CRITICAL(&timerMux);

    if(leds_pos_changed){

        leds_pos_changed = false;
        positionToDraw = leds_pos;

    }

    portEXIT_CRITICAL(&timerMux);


    if(positionToDraw >= 0){

        draw_leds(positionToDraw);
        FastLED.show();

    }


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

}


// ============================================================
// INTERRUPCION HALL
// ============================================================

void IRAM_ATTR onHall(){

    uint32_t nowUs = micros();


    if(hallLastUs != 0){

        uint32_t deltaUs =
            nowUs - hallLastUs;

        if(deltaUs < HALL_MIN_INTERVAL_US){
            return;
        }

        hallMeasuredLapUs = deltaUs;

    }
    else{

        hallMeasuredLapUs = 0;

    }


    hallLastUs = nowUs;
    hallEventUs = nowUs;
    hallEvent = true;


    // El Hall fija inmediatamente el 0° físico.
    portENTER_CRITICAL_ISR(&timerMux);

    leds_pos = 0;
    leds_pos_changed = true;

    portEXIT_CRITICAL_ISR(&timerMux);

}


// ============================================================
// PROCESAR SINCRONIZACION HALL
// ============================================================

void processHallEvent(){

    bool eventPending = false;
    uint32_t eventUs = 0;
    uint32_t measuredLapUs = 0;


    portENTER_CRITICAL(&timerMux);

    if(hallEvent){

        eventPending = true;
        eventUs = hallEventUs;
        measuredLapUs = hallMeasuredLapUs;
        hallEvent = false;

    }

    portEXIT_CRITICAL(&timerMux);


    if(!eventPending){
        return;
    }


    bool completedRealLap = false;


    if(
        measuredLapUs >= HALL_MIN_INTERVAL_US &&
        measuredLapUs <= HALL_MAX_VALID_LAP_US
    ){

        completedRealLap = true;


        if(!lapFilterInitialized){

            filteredLapUs = measuredLapUs;
            lapFilterInitialized = true;

        }
        else{

            filteredLapUs =
                (filteredLapUs * 3 + measuredLapUs) /
                4;

        }

    }


    uint32_t newPeriodUs =
        (filteredLapUs + (leds_samples / 2)) /
        leds_samples;

    if(newPeriodUs < 1){
        newPeriodUs = 1;
    }

    pulses_number = newPeriodUs;


    // Re-alinear periodo Y fase del temporizador con el Hall.
    timerStop(timer);

    timerAlarm(
        timer,
        newPeriodUs,
        true,
        0
    );


    uint32_t elapsedUs =
        micros() - eventUs;

    uint32_t step =
        elapsedUs / newPeriodUs;

    if(step >= leds_samples){
        step = leds_samples - 1;
    }

    uint32_t phaseUs =
        elapsedUs % newPeriodUs;


    portENTER_CRITICAL(&timerMux);

    leds_pos =
        step * resolution;

    leds_pos_changed = true;

    portEXIT_CRITICAL(&timerMux);


    timerWrite(
        timer,
        phaseUs
    );

    timerStart(timer);


    // La animación avanza una sola vez por REVOLUCION FISICA.
    // Ya no depende de que el timer alcance 360° por sí solo.
    if(completedRealLap){

        advanceAnimation();

    }

}


// ============================================================
// AVANCE TEMPORAL DE ANIMACION
// ============================================================

void advanceAnimation(){

    animation_step++;


    if(
        animation_step >=
        ORIGINAL_IMAGES
    ){

        animation_step = 0;

    }


    cont =
        (
            animation_step *
            NUM_IMAGES
        ) /
        ORIGINAL_IMAGES;


    if(
        cont >=
        NUM_IMAGES
    ){

        cont =
            NUM_IMAGES - 1;

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
// MAPEO ANGULAR
// ============================================================

int getImageRow(int position){

    int row =
        (position / resolution) %
        leds_samples;


    if(REVERSE_ANGULAR_DIRECTION){

        row =
            (leds_samples - row) %
            leds_samples;

    }


    row += ANGULAR_OFFSET_STEPS;

    while(row < 0){
        row += leds_samples;
    }

    row %= leds_samples;


    return row;

}


// ============================================================
// DIBUJAR LEDS
// ============================================================

void draw_leds(
    int leds_pos
) {


    int row =
        getImageRow(leds_pos);


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
    // SEGUNDA MITAD: 180° OPUESTA AL ROW YA CORREGIDO
    // ========================================================

    int oppositeRow =
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


        color16 =
            pgm_read_word(
                &(
                    frames
                    [cont]
                    [oppositeRow]
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


        color16 =
            pgm_read_word(
                &(
                    frames
                    [cont]
                    [oppositeRow]
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

    portENTER_CRITICAL_ISR(&timerMux);

    // El timer ya no define el comienzo de una revolución.
    // Solo avanza posiciones entre dos referencias Hall.
    if(
        leds_pos <
        (360 - resolution)
    ) {

        leds_pos +=
            resolution;

    }
    else {

        // Se mantiene en la última columna esperando el siguiente Hall.
        // Así desaparece la deriva provocada por una vuelta "inventada"
        // por el timer antes de la referencia física.
        leds_pos =
            360 - resolution;

    }


    leds_pos_changed =
        true;

    portEXIT_CRITICAL_ISR(&timerMux);

}
