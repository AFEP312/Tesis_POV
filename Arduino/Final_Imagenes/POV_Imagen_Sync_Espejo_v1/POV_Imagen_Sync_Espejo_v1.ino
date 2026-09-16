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

// El Hall se usa ahora como referencia física de 0° en CADA vuelta.
// Se mide en microsegundos para evitar el error de cuantización de millis().
const uint32_t HALL_MIN_INTERVAL_US = 50000;   // Rechaza rebotes < 50 ms
const uint32_t HALL_MAX_VALID_LAP_US = 180000; // Evita aceptar una vuelta perdida como nueva velocidad
const uint32_t INITIAL_LAP_US = 100000;        // Aproximadamente 600 RPM

volatile uint32_t hallLastUs = 0;
volatile uint32_t hallEventUs = 0;
volatile uint32_t hallMeasuredLapUs = 0;
volatile bool hallEvent = false;

uint32_t filteredLapUs = INITIAL_LAP_US;
bool lapFilterInitialized = false;


// ================================
// POV
// ================================

int resolution = 6;

byte leds_samples = 360 / resolution;

volatile int leds_pos = 0;
volatile bool leds_pos_changed = false;

// La imagen actual aparece en espejo con el sentido físico de giro.
// true invierte SOLO el eje angular sin modificar imgs.h.
const bool REVERSE_ANGULAR_DIRECTION = true;

// Permite una corrección fija de orientación en pasos de 6° si luego se necesita.
// 0 = sin desplazamiento adicional.
const int ANGULAR_OFFSET_STEPS = 0;


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

portMUX_TYPE timerMux =
    portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t pulses_number =
    (INITIAL_LAP_US + 30) / 60;


// ================================
// Funciones
// ================================

void draw_leds(int leds_pos);

void IRAM_ATTR onTimer();
void IRAM_ATTR onHall();

void processHallEvent();

int getImageRow(int position);

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
        (INITIAL_LAP_US + (leds_samples / 2)) /
        leds_samples;


    timerAlarm(
        timer,
        pulses_number,
        true,
        0
    );


    // ================================
    // INTERRUPCION HALL
    // ================================

    // El sensor se utilizaba como nivel LOW dentro de loop().
    // FALLING conserva esa misma referencia, pero captura el instante
    // físico con mucha más precisión y sin depender de la duración del loop.
    attachInterrupt(
        digitalPinToInterrupt(HALL_SENSOR),
        onHall,
        FALLING
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

    Serial.println("WiFi intentando conectar en segundo plano.");
    Serial.println("El POV continua funcionando normalmente.");

}


// ============================================================
// LOOP
// ============================================================

void loop(){

    // ========================================================
    // SINCRONIZACION FISICA CON HALL
    // ========================================================

    // Tiene prioridad sobre OTA para minimizar la latencia del POV.
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
    // ===== OTA ==============================================
    // ========================================================

    if(
        !otaStarted &&
        WiFi.status() == WL_CONNECTED
    ){

        startOTA();

    }


    if(otaStarted){

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

        // Evita que un mismo paso del imán produzca varias referencias.
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


    // El Hall ES el cero angular físico.
    // Esto impide que leds_pos pueda derivar durante varias vueltas.
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


    // Solo una medición plausible modifica la velocidad estimada.
    // Si se pierde un pulso Hall, no se permite que una vuelta de 200 ms,
    // por ejemplo, duplique de golpe el periodo angular.
    if(
        measuredLapUs >= HALL_MIN_INTERVAL_US &&
        measuredLapUs <= HALL_MAX_VALID_LAP_US
    ){

        if(!lapFilterInitialized){

            filteredLapUs = measuredLapUs;
            lapFilterInitialized = true;

        }
        else{

            // Filtro IIR suave: 75 % valor previo + 25 % nueva vuelta.
            // Reduce pequeñas variaciones sin perder seguimiento de velocidad.
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


    // Se vuelve a alinear la FASE del temporizador con el Hall.
    // No solo se cambia su periodo: también se corrige dónde comienza
    // la nueva vuelta, evitando la deriva visual.
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
// MAPEO ANGULAR
// ============================================================

int getImageRow(int position){

    int row =
        (position / resolution) %
        leds_samples;


    if(REVERSE_ANGULAR_DIRECTION){

        // 0 permanece como referencia Hall.
        // 1..59 se recorren en orden inverso para eliminar el espejo.
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

void draw_leds(int leds_pos){

    int row =
        getImageRow(leds_pos);


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


    // Segunda mitad: exactamente 180° opuesta al row ya corregido.
    int oppositeRow =
        (row + (leds_samples / 2)) %
        leds_samples;


    for(int i = 0; i < NUM_LEDS; i++){


        color16 =
            pgm_read_word(
                &(frame5[oppositeRow][2 * NUM_LEDS - i - 1])
            );


        color24 =
            expandColor(color16);


        leds3[i] =
            color24;



        color16 =
            pgm_read_word(
                &(frame5[oppositeRow][NUM_LEDS - i - 1])
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

    portENTER_CRITICAL_ISR(&timerMux);

    // El timer solo avanza dentro de la vuelta física actual.
    // Ya NO crea una nueva vuelta por sí mismo: el Hall manda.
    if(leds_pos < (360 - resolution)){

        leds_pos += resolution;

    }
    else{

        leds_pos = 360 - resolution;

    }

    leds_pos_changed = true;

    portEXIT_CRITICAL_ISR(&timerMux);

}
