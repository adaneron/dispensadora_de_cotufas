/*
================================================================================
 CÓDIGO PARA MÁQUINA DISPENSADORA DE COTUFAS BASADA EN ESP32 (VERSIÓN 4.4)
================================================================================
 Autores: Adan Gonzalez, Gemini (Asistente de IA de Google)
 Fecha: 01 de Agosto de 2025
 Versión: 4.4 - Corregida la lentitud y el parpadeo del LCD eliminando delay() de FastLED.
================================================================================
*/

// =============================================================================
// 1. INCLUSIÓN DE LIBRERÍAS
// =============================================================================
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LiquidCrystal_I2C.h>
#include <AccelStepper.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <FastLED.h>

// =============================================================================
// 2. SECCIÓN DE CONFIGURACIÓN MODIFICABLE
// =============================================================================

// --- Configuración de Red WiFi ---
const char* WIFI_SSID = "NOMBRE_DE_LA_RED_WIFI";
const char* WIFI_PASSWORD = "CONTRASEÑA_DE_LA_RED_WIFI";

// --- Configuración del Hotspot (Punto de Acceso) ---
const char* AP_SSID = "Máquina de cotufas";
const char* AP_PASSWORD = "cotufas";

// --- Configuración de la Tira LED ---
#define NUM_LEDS      110
#define DATA_PIN      4
#define BRIGHTNESS    128
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB
#define FRAMES_PER_SECOND 60 // Velocidad de actualización de los LEDs
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

// --- Configuración de Pines de la Máquina ---
#define LCD_SDA_PIN 21
#define LCD_SCL_PIN 22
#define STEPPER_STEP_PIN 14
#define STEPPER_DIR_PIN  12
#define STEPPER_M0_PIN   27
#define STEPPER_M1_PIN   26
#define STEPPER_M2_PIN   25
#define SERVO_POPCORN_PIN 2
#define SERVO_COOKIE_PIN  17
#define RELAY_VIBRATOR_PIN 33
#define RELAY_CHOCOLATE_PIN 32
#define RELAY_DRINK_PIN     15
#define US_1_TRIG_PIN 19
#define US_1_ECHO_PIN 18
#define US_2_TRIG_PIN 5
#define US_2_ECHO_PIN 13

// --- Configuración del Motor (PARÁMETROS DE LA VERSIÓN 1.2) ---
#define MOTOR_STEPS_PER_REV 200
#define MICROSTEPS          16
#define MAX_SPEED_RPM       30
const float STEPS_PER_DEGREE = (MOTOR_STEPS_PER_REV * MICROSTEPS) / 360.0;

// --- Posiciones de las Estaciones (grados) ---
#define POS_HOME          0
#define POS_CHOCOLATE    140
#define POS_POPCORN     320
#define POS_COOKIE      480

// --- Tiempos de Dispensado (milisegundos) ---
#define TIME_DISPENSE_POPCORN   3000
#define TIME_DISPENSE_CHOCOLATE 3500
#define TIME_DISPENSE_COOKIE    200
#define TIME_DISPENSE_DRINK     6000

// --- Configuración de Servos (ángulos) ---
#define SERVO_POPCORN_CLOSED_ANGLE 90
#define SERVO_POPCORN_OPEN_ANGLE   180
#define SERVO_COOKIE_CLOSED_ANGLE  180 // Invertido
#define SERVO_COOKIE_OPEN_ANGLE    140  // Invertido

// --- Configuración de Sensores ---
#define CUP_DETECTION_DISTANCE_CM 10
#define DETECTION_CONFIRM_TIME_MS 3000
#define PICKUP_CONFIRM_TIME_MS 3000

// =============================================================================
// 3. OBJETOS Y VARIABLES GLOBALES
// =============================================================================

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
LiquidCrystal_I2C lcd(0x27, 20, 4);
AccelStepper stepper(AccelStepper::DRIVER, STEPPER_STEP_PIN, STEPPER_DIR_PIN);
Servo servoPopcorn;
Servo servoCookie;

CRGB leds[NUM_LEDS];
typedef void (*SimplePatternList[])();
void rainbow();
void confetti();
void sinelon();
void juggle();
void bpm();
SimplePatternList gPatterns = {rainbow, confetti, sinelon, juggle, bpm};
uint8_t gCurrentPatternNumber = 0;
uint8_t gHue = 0;

enum MachineState { IDLE, VALIDATING_ORDER, PROCESSING_ORDER, WAITING_FOR_PICKUP };
MachineState machineState = IDLE;

enum OrderStep { NONE, CHECK_CUP_POPCORN, CHECK_CUP_DRINK, MOVE_TO_POPCORN, DISPENSE_POPCORN, MOVE_TO_CHOCOLATE, DISPENSE_CHOCOLATE, MOVE_TO_COOKIE, DISPENSE_COOKIE, DISPENSE_DRINK, MOVE_TO_HOME, ORDER_COMPLETE };
OrderStep currentStep = NONE;

String currentPIN = "";
String clientName = "";
bool order_popcorn = false;
bool order_chocolate = false;
bool order_cookie = false;
bool order_drink = false;

unsigned long actionTimer = 0;
unsigned long sensorTimer = 0;

// =============================================================================
// 4. PÁGINA WEB (HTML, CSS, JS)
// =============================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Máquina de Cotufas</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; background-color: #f0f2f5; color: #333; text-align: center; }
        .container { background-color: white; padding: 30px 40px; border-radius: 12px; box-shadow: 0 4px 20px rgba(0,0,0,0.1); max-width: 400px; width: 90%; transition: all 0.3s ease; }
        h1, h2 { color: #e74c3c; }
        h1 { font-size: 2em; margin-bottom: 10px; }
        h2 { font-size: 1.5em; margin-bottom: 20px; }
        p { margin-bottom: 20px; line-height: 1.6; }
        input[type="text"], input[type="number"] { width: calc(100% - 22px); padding: 12px; margin-bottom: 20px; border: 1px solid #ddd; border-radius: 8px; font-size: 1em; }
        .btn { background-color: #e74c3c; color: white; border: none; padding: 15px 20px; font-size: 1.1em; border-radius: 8px; cursor: pointer; width: 100%; transition: background-color 0.3s ease; font-weight: bold; }
        .btn:hover { background-color: #c0392b; }
        .screen { display: none; }
        .screen.active { display: block; }
        .checkbox-group { text-align: left; margin: 20px 0; }
        .checkbox-item { margin-bottom: 15px; }
        .checkbox-item label { font-size: 1.1em; cursor: pointer; display: flex; align-items: center; }
        .checkbox-item input { margin-right: 10px; width: 20px; height: 20px; }
        #toppings-section { border-left: 3px solid #e74c3c; padding-left: 15px; margin-left: 10px; margin-top: 15px; display: none; }
        #status-message { font-size: 1.2em; font-weight: 500; color: #555; }
        .emoji { font-size: 2.5em; margin-bottom: 15px; }
        #progress-bar-container { width: 100%; background-color: #eee; border-radius: 8px; overflow: hidden; margin-top: 20px; }
        #progress-bar { width: 0%; height: 30px; background-color: #2ecc71; transition: width 0.5s ease; }
    </style>
</head>
<body>

<div class="container">
    <div id="welcome-page" class="screen active">
        <div class="emoji">🍿</div>
        <h1>Máquina de Cotufas</h1>
        <p>Para continuar, por favor indique su nombre:</p>
        <input type="text" id="client-name" placeholder="Escriba su nombre aquí">
        <button class="btn" onclick="submitName()">Siguiente</button>
    </div>

    <div id="pin-page" class="screen">
        <div class="emoji">🔑</div>
        <h2>Verificación de Seguridad</h2>
        <p>Por favor, ingrese la clave de 6 dígitos que se muestra en la pantalla de la máquina.</p>
        <input type="number" id="pin-input" placeholder="Clave de un solo uso">
        <button class="btn" onclick="submitPin()">Verificar</button>
    </div>

    <div id="order-page" class="screen">
        <div class="emoji">📝</div>
        <h2>¡Hola, <span id="displayName"></span>!</h2>
        <p>Escoge lo que quieras.</p>
        <div class="checkbox-group">
            <div class="checkbox-item">
                <label><input type="checkbox" id="check-popcorn" onchange="toggleToppings()" checked>Incluir cotufas</label>
            </div>
            <div id="toppings-section" style="display:block;">
                <h3>Toppings</h3>
                <div class="checkbox-item">
                    <label><input type="checkbox" id="check-chocolate">Chocolate oscuro</label>
                </div>
                <div class="checkbox-item">
                    <label><input type="checkbox" id="check-cookie">Galleta triturada</label>
                </div>
            </div>
        </div>
        <div class="checkbox-group">
            <h3>Bebidas</h3>
            <div class="checkbox-item">
                <label><input type="checkbox" id="check-drink">Incluir bebida</label>
            </div>
        </div>
        <button class="btn" onclick="placeOrder()">Pedir 🍿</button>
    </div>

    <div id="status-page" class="screen">
        <div class="emoji" id="progress-emoji">⏳</div>
        <h2>Preparando tu pedido...</h2>
        <p id="status-message">Iniciando preparación...</p>
        <div id="progress-bar-container">
            <div id="progress-bar"></div>
        </div>
    </div>
</div>

<script>
    let websocket;
    let clientName = "";

    function initWebSocket() {
        websocket = new WebSocket(`ws://${window.location.hostname}/ws`);
        websocket.onopen = () => console.log('WebSocket conectado');
        websocket.onclose = () => { setTimeout(initWebSocket, 2000); };
        websocket.onmessage = (event) => {
            const data = JSON.parse(event.data);
            console.log('Mensaje del servidor:', data);

            if (data.action === 'pin_accepted') {
                document.getElementById('displayName').innerText = clientName;
                showScreen('order-page');
            } else if (data.action === 'pin_rejected') {
                alert('PIN incorrecto. Por favor, inténtalo de nuevo.');
            } else if (data.action === 'status_update') {
                showScreen('status-page');
                updateProgress(data.progress, data.message, data.emoji);
            } else if (data.action === 'order_complete') {
                showScreen('status-page');
                updateProgress(100, `${clientName}, ${data.message}`, '🎉');
            } else if (data.action === 'error') {
                alert(`Error: ${data.message}`);
                showScreen('order-page');
            } else if (data.action === 'reset_ui') {
                resetUI();
            }
        };
    }
    function showScreen(screenId) {
        document.querySelectorAll('.screen').forEach(s => s.classList.remove('active'));
        document.getElementById(screenId).classList.add('active');
    }
    function submitName() {
        clientName = document.getElementById('client-name').value;
        if (clientName.trim() === '') {
            alert('Por favor, ingresa tu nombre.');
            return;
        }
        websocket.send(JSON.stringify({ action: 'set_name', name: clientName }));
        showScreen('pin-page');
    }
    function submitPin() {
        const pin = document.getElementById('pin-input').value;
        websocket.send(JSON.stringify({ action: 'validate_pin', pin: pin }));
    }
    function toggleToppings() {
        const popcornChecked = document.getElementById('check-popcorn').checked;
        document.getElementById('toppings-section').style.display = popcornChecked ? 'block' : 'none';
        if (!popcornChecked) {
            document.getElementById('check-chocolate').checked = false;
            document.getElementById('check-cookie').checked = false;
        }
    }
    function placeOrder() {
        const order = {
            popcorn: document.getElementById('check-popcorn').checked,
            chocolate: document.getElementById('check-chocolate').checked,
            cookie: document.getElementById('check-cookie').checked,
            drink: document.getElementById('check-drink').checked
        };
        if (!order.popcorn && !order.drink) {
            alert('Debes seleccionar al menos un producto.');
            return;
        }
        websocket.send(JSON.stringify({ action: 'place_order', order: order }));
        showScreen('status-page');
    }
    function updateProgress(progress, message, emoji) {
        document.getElementById('progress-bar').style.width = progress + '%';
        document.getElementById('status-message').innerText = message;
        if(emoji) {
            document.getElementById('progress-emoji').innerText = emoji;
        }
    }
    function resetUI() {
        showScreen('welcome-page');
        document.getElementById('client-name').value = '';
        document.getElementById('pin-input').value = '';
        document.getElementById('check-popcorn').checked = true;
        toggleToppings();
        document.getElementById('check-chocolate').checked = false;
        document.getElementById('check-cookie').checked = false;
        document.getElementById('check-drink').checked = false;
    }
    window.onload = initWebSocket;
</script>
</body>
</html>
)rawliteral";

// =============================================================================
// 5. DECLARACIONES ADELANTADAS DE FUNCIONES
// =============================================================================
void handleOrderSequence();
void checkPickup();
void handleLeds();
void setupWiFi();
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void setupWebServer();
void updateLcdDisplay(String line1, String line2, String networkName);
void generateNewPIN();
long readUltrasonic(int trigPin, int echoPin);
void notifyClients(String message);
long degreesToSteps(float degrees);
void nextPattern();


// =============================================================================
// 6. FUNCIÓN DE CONFIGURACIÓN (SETUP)
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nIniciando Máquina de Cotufas v4.4...");

    FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear();
    FastLED.show();

    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Iniciando...");

    pinMode(RELAY_VIBRATOR_PIN, OUTPUT);
    pinMode(RELAY_CHOCOLATE_PIN, OUTPUT);
    pinMode(RELAY_DRINK_PIN, OUTPUT);
    digitalWrite(RELAY_VIBRATOR_PIN, HIGH);
    digitalWrite(RELAY_CHOCOLATE_PIN, HIGH);
    digitalWrite(RELAY_DRINK_PIN, HIGH);
    
    pinMode(US_1_TRIG_PIN, OUTPUT);
    pinMode(US_1_ECHO_PIN, INPUT);
    pinMode(US_2_TRIG_PIN, OUTPUT);
    pinMode(US_2_ECHO_PIN, INPUT);

    servoPopcorn.attach(SERVO_POPCORN_PIN);
    servoCookie.attach(SERVO_COOKIE_PIN);
    servoPopcorn.write(SERVO_POPCORN_CLOSED_ANGLE);
    servoCookie.write(SERVO_COOKIE_CLOSED_ANGLE);

    pinMode(STEPPER_M0_PIN, OUTPUT);
    pinMode(STEPPER_M1_PIN, OUTPUT);
    pinMode(STEPPER_M2_PIN, OUTPUT);
    digitalWrite(STEPPER_M0_PIN, LOW); 
    digitalWrite(STEPPER_M1_PIN, HIGH);
    digitalWrite(STEPPER_M2_PIN, HIGH);
    
    stepper.setMaxSpeed((MAX_SPEED_RPM / 60.0) * MOTOR_STEPS_PER_REV * MICROSTEPS);
    stepper.setAcceleration(stepper.maxSpeed() / 2);
    stepper.setCurrentPosition(0);

    setupWiFi();
    setupWebServer();

    generateNewPIN();
    updateLcdDisplay("Esperando pedido...", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
    Serial.println("Sistema listo.");
}

// =============================================================================
// 7. BUCLE PRINCIPAL (LOOP)
// =============================================================================
void loop() {
    ws.cleanupClients();
    stepper.run();
    handleLeds();

    switch (machineState) {
        case IDLE: break;
        case VALIDATING_ORDER: break;
        case PROCESSING_ORDER: handleOrderSequence(); break;
        case WAITING_FOR_PICKUP: checkPickup(); break;
    }
}

// =============================================================================
// 8. DEFINICIÓN DE FUNCIONES
// =============================================================================

void setupWiFi() {
    lcd.setCursor(0, 1);
    lcd.print("Conectando a WiFi..");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFallo al conectar. Creando Hotspot.");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Creando Hotspot...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        Serial.print("Hotspot '");
        Serial.print(AP_SSID);
        Serial.print("' creado. IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("\nConectado a WiFi.");
        Serial.print("Dirección IP: ");
        Serial.println(WiFi.localIP());
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("Cliente WebSocket conectado: #%u\n", client->id());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("Cliente WebSocket desconectado: #%u\n", client->id());
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            String message = (char*)data;
            Serial.printf("Mensaje recibido: %s\n", message.c_str());
            StaticJsonDocument<256> doc;
            deserializeJson(doc, message);
            String action = doc["action"];
            if (action == "set_name") {
                clientName = doc["name"].as<String>();
            } else if (action == "validate_pin") {
                String pin = doc["pin"];
                if (pin == currentPIN) {
                    client->text("{\"action\":\"pin_accepted\"}");
                    machineState = VALIDATING_ORDER;
                } else {
                    client->text("{\"action\":\"pin_rejected\"}");
                }
            } else if (action == "place_order") {
                if (machineState == VALIDATING_ORDER) {
                    JsonObject order = doc["order"];
                    order_popcorn = order["popcorn"];
                    order_chocolate = order["chocolate"];
                    order_cookie = order["cookie"];
                    order_drink = order["drink"];
                    machineState = PROCESSING_ORDER;
                    currentStep = CHECK_CUP_POPCORN;
                    sensorTimer = millis();
                }
            }
        }
    }
}

void setupWebServer() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });
    server.begin();
    Serial.println("Servidor Web iniciado.");
}

void updateLcdDisplay(String line1, String line2, String networkName) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1.substring(0, 20));
    lcd.setCursor(0, 1);
    String network_line = (WiFi.getMode() == WIFI_AP ? "Hotspot: " : "Conectado a: ") + networkName;
    lcd.print(network_line.substring(0, 20));
    lcd.setCursor(0, 2);
    String ip_line = "IP: " + (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
    lcd.print(ip_line.substring(0, 20));
    lcd.setCursor(0, 3);
    lcd.print("Clave: ");
    lcd.print(currentPIN);
}

void generateNewPIN() {
    randomSeed(micros());
    long pinNum = random(100000, 1000000);
    currentPIN = String(pinNum);
    Serial.print("Nuevo PIN generado: ");
    Serial.println(currentPIN);
}

long readUltrasonic(int trigPin, int echoPin) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    long duration = pulseIn(echoPin, HIGH, 30000); 
    if (duration == 0) {
        return 999;
    }
    return duration / 58;
}

void notifyClients(String message) {
    ws.textAll(message);
}

long degreesToSteps(float degrees) {
    return (long)(degrees * STEPS_PER_DEGREE);
}

// #############################################################################
// ############## LÓGICA DE MOVIMIENTO DEL MOTOR (VERSIÓN 1.2) #################
// #############################################################################
void handleOrderSequence() {
    switch (currentStep) {
        case CHECK_CUP_POPCORN:
            if (!order_popcorn) { currentStep = CHECK_CUP_DRINK; sensorTimer = millis(); break; }
            updateLcdDisplay("Verificando vaso...", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
            notifyClients("{\"action\":\"status_update\", \"message\":\"Por favor, coloque un vaso para las cotufas...\", \"progress\":5, \"emoji\":\"컵\"}");
            if (readUltrasonic(US_1_TRIG_PIN, US_1_ECHO_PIN) < CUP_DETECTION_DISTANCE_CM) {
                if (millis() - sensorTimer >= DETECTION_CONFIRM_TIME_MS) { currentStep = MOVE_TO_POPCORN; }
            } else { sensorTimer = millis(); }
            break;

        case CHECK_CUP_DRINK:
            if (!order_drink) { currentStep = MOVE_TO_HOME; break; }
            updateLcdDisplay("Verificando vaso...", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
            notifyClients("{\"action\":\"status_update\", \"message\":\"Por favor, coloque un vaso para la bebida...\", \"progress\":85, \"emoji\":\"🥤\"}");
            if (readUltrasonic(US_2_TRIG_PIN, US_2_ECHO_PIN) < CUP_DETECTION_DISTANCE_CM) {
                if (millis() - sensorTimer >= DETECTION_CONFIRM_TIME_MS) { currentStep = DISPENSE_DRINK; }
            } else { sensorTimer = millis(); }
            break;

        case MOVE_TO_POPCORN:
            updateLcdDisplay("Moviendo a Cotufas", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
            notifyClients("{\"action\":\"status_update\", \"message\":\"Moviendo a la estación de cotufas...\", \"progress\":10, \"emoji\":\"➡️\"}");
            stepper.setSpeed(stepper.maxSpeed());
            stepper.moveTo(degreesToSteps(POS_POPCORN));
            currentStep = DISPENSE_POPCORN;
            break;

        case DISPENSE_POPCORN:
            if (stepper.distanceToGo() == 0) {
                updateLcdDisplay("Sirviendo Cotufas", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
                notifyClients("{\"action\":\"status_update\", \"message\":\"Sirviendo cotufas...\", \"progress\":25, \"emoji\":\"🍿\"}");
                servoPopcorn.write(SERVO_POPCORN_OPEN_ANGLE);
                digitalWrite(RELAY_VIBRATOR_PIN, LOW);
                delay(TIME_DISPENSE_POPCORN);
                servoPopcorn.write(SERVO_POPCORN_CLOSED_ANGLE);
                digitalWrite(RELAY_VIBRATOR_PIN, HIGH);
                currentStep = order_chocolate ? MOVE_TO_CHOCOLATE : (order_cookie ? MOVE_TO_COOKIE : MOVE_TO_HOME);
            }
            break;

        case MOVE_TO_CHOCOLATE:
            updateLcdDisplay("Moviendo a Choco.", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
            notifyClients("{\"action\":\"status_update\", \"message\":\"Añadiendo chocolate oscuro...\", \"progress\":50, \"emoji\":\"🍫\"}");
            stepper.setSpeed(stepper.maxSpeed());
            stepper.moveTo(degreesToSteps(POS_CHOCOLATE));
            currentStep = DISPENSE_CHOCOLATE;
            break;

        case DISPENSE_CHOCOLATE:
            if (stepper.distanceToGo() == 0) {
                updateLcdDisplay("Sirviendo Choco.", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
                notifyClients("{\"action\":\"status_update\", \"message\":\"Sirviendo chocolate...\", \"progress\":65, \"emoji\":\"🍫\"}");
                digitalWrite(RELAY_CHOCOLATE_PIN, LOW);
                delay(TIME_DISPENSE_CHOCOLATE);
                digitalWrite(RELAY_CHOCOLATE_PIN, HIGH);
                currentStep = order_cookie ? MOVE_TO_COOKIE : MOVE_TO_HOME;
            }
            break;
            
        case MOVE_TO_COOKIE:
            updateLcdDisplay("Moviendo a Galleta", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
            notifyClients("{\"action\":\"status_update\", \"message\":\"Añadiendo galleta triturada...\", \"progress\":75, \"emoji\":\"🍪\"}");
            stepper.setSpeed(stepper.maxSpeed());
            stepper.moveTo(degreesToSteps(POS_COOKIE));
            currentStep = DISPENSE_COOKIE;
            break;

        case DISPENSE_COOKIE:
            if (stepper.distanceToGo() == 0) {
                updateLcdDisplay("Sirviendo Galleta", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
                notifyClients("{\"action\":\"status_update\", \"message\":\"Sirviendo galleta...\", \"progress\":85, \"emoji\":\"🍪\"}");
                servoCookie.write(SERVO_COOKIE_OPEN_ANGLE);
                delay(TIME_DISPENSE_COOKIE);
                servoCookie.write(SERVO_COOKIE_CLOSED_ANGLE);
                currentStep = MOVE_TO_HOME;
            }
            break;

        case MOVE_TO_HOME:
            updateLcdDisplay("Moviendo a Inicio", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
            notifyClients("{\"action\":\"status_update\", \"message\":\"Finalizando...\", \"progress\":90, \"emoji\":\"🔄\"}");
            stepper.setSpeed(-stepper.maxSpeed());
            stepper.moveTo(degreesToSteps(POS_HOME));
            currentStep = order_drink ? CHECK_CUP_DRINK : ORDER_COMPLETE;
            if(currentStep == CHECK_CUP_DRINK) sensorTimer = millis();
            break;

        case DISPENSE_DRINK:
            updateLcdDisplay("Sirviendo Bebida", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
            notifyClients("{\"action\":\"status_update\", \"message\":\"Sirviendo tu bebida...\", \"progress\":95, \"emoji\":\"🥤\"}");
            digitalWrite(RELAY_DRINK_PIN, LOW);
            delay(TIME_DISPENSE_DRINK);
            digitalWrite(RELAY_DRINK_PIN, HIGH);
            currentStep = ORDER_COMPLETE;
            break;

        case ORDER_COMPLETE:{
            if(stepper.distanceToGo() == 0){
                updateLcdDisplay("Retire su pedido", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
                String completeMsg = "tu pedido está listo! Ya puedes retirarlo.";
                notifyClients("{\"action\":\"order_complete\", \"message\":\"" + completeMsg + "\"}");
                machineState = WAITING_FOR_PICKUP;
                sensorTimer = millis();
                currentStep = NONE;
            }
            break;
        }

        default:
            break;
    }
}

void checkPickup() {
    bool popcornCupPresent = order_popcorn ? (readUltrasonic(US_1_TRIG_PIN, US_1_ECHO_PIN) < CUP_DETECTION_DISTANCE_CM) : false;
    bool drinkCupPresent = order_drink ? (readUltrasonic(US_2_TRIG_PIN, US_2_ECHO_PIN) < CUP_DETECTION_DISTANCE_CM) : false;

    if (!popcornCupPresent && !drinkCupPresent) {
        if (millis() - sensorTimer >= PICKUP_CONFIRM_TIME_MS) {
            Serial.println("Pedido retirado. Reiniciando ciclo.");
            clientName = "";
            generateNewPIN();
            machineState = IDLE;
            updateLcdDisplay("Esperando pedido...", "", String(WiFi.getMode() == WIFI_AP ? AP_SSID : WIFI_SSID));
            notifyClients("{\"action\":\"reset_ui\"}");
        }
    } else {
        sensorTimer = millis();
    }
}

// =============================================================================
// 9. FUNCIONES PARA EFECTOS DE LUCES LED
// =============================================================================
void handleLeds() {
    // ########### LÍNEA CORREGIDA ###########
    static unsigned long lastLedUpdate = 0;
    if (millis() - lastLedUpdate > (1000 / FRAMES_PER_SECOND)) {
        lastLedUpdate = millis();

        gPatterns[gCurrentPatternNumber]();
        FastLED.show();
        
        EVERY_N_SECONDS(5) {
            nextPattern();
        }
        EVERY_N_MILLISECONDS(20) {
            gHue++;
        }
    }
}

void nextPattern() {
    gCurrentPatternNumber = (gCurrentPatternNumber + 1) % ARRAY_SIZE(gPatterns);
    FastLED.clear();
}

void rainbow() {
    fill_rainbow(leds, NUM_LEDS, gHue, 7);
}

void confetti() {
    fadeToBlackBy(leds, NUM_LEDS, 20);
    int pos = random16(NUM_LEDS);
    leds[pos] += CHSV(gHue + random8(64), 200, 255);
}

void sinelon() {
    fadeToBlackBy(leds, NUM_LEDS, 20);
    int pos = beatsin16(13, 0, NUM_LEDS - 1);
    leds[pos] += CHSV(gHue, 255, 192);
}

void juggle() {
    fadeToBlackBy(leds, NUM_LEDS, 20);
    byte dothue = 0;
    for (int i = 0; i < 8; i++) {
        leds[beatsin16(i + 7, 0, NUM_LEDS - 1)] |= CHSV(dothue, 200, 255);
        dothue += 32;
    }
}

void bpm() {
    uint8_t beat = beatsin8(62, 64, 255);
    CRGB color = CHSV(gHue, 255, beat);
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = color;
    }
}