#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <TM1637Display.h>
#include <IRrecv.h>
#include <IRutils.h>

// --- Configuración de Pines ---
#define CLK 4         // TM1637 CLK (Pin D2)
#define DIO 5         // TM1637 DIO (Pin D1)
#define IR_PIN 14     // IR1838 OUT (Pin D5)

TM1637Display display(CLK, DIO);
IRrecv irrecv(IR_PIN);
decode_results results;

// --- Configuración WiFi y Portal Cautivo ---
const char* ssid = "rc-control";
const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer server(80);
IPAddress apIP(192, 168, 4, 1);

// --- Variables del Cronómetro ---
bool isRunning = false;
bool isCountingDown = false; 
bool displayEnabled = false; 
unsigned long startTime = 0;
unsigned long elapsedMillis = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long countdownStart = 0; 

// --- ANIMACIÓN "COCHE EN PISTA" ---
const uint8_t snakeTrack[12][4] = {
  {0x01, 0x00, 0x00, 0x00}, {0x00, 0x01, 0x00, 0x00}, {0x00, 0x00, 0x01, 0x00}, {0x00, 0x00, 0x00, 0x01}, 
  {0x00, 0x00, 0x00, 0x02}, {0x00, 0x00, 0x00, 0x04}, {0x00, 0x00, 0x00, 0x08}, {0x00, 0x00, 0x08, 0x00}, 
  {0x00, 0x08, 0x00, 0x00}, {0x08, 0x00, 0x00, 0x00}, {0x10, 0x00, 0x00, 0x00}, {0x20, 0x00, 0x00, 0x00}
};

// --- Interfaz Web (PROGMEM) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <meta charset="utf-8"><title>Control RC</title>
  <style>
    body { font-family: sans-serif; text-align: center; background-color: #1e1e24; color: white; margin-top: 20px; }
    h1 { font-size: 2.2em; margin-bottom: 20px; color: #fca311; }
    button { padding: 15px 20px; font-size: 1.2em; margin: 10px; border-radius: 12px; border: none; cursor: pointer; color: white; font-weight: bold; }
    button:active { transform: scale(0.95); }
    .btn-main { width: 85%; max-width: 350px; padding: 20px 30px; font-size: 1.5em; }
    .btn-start { background-color: #28a745; } .btn-stop { background-color: #dc3545; } .btn-reset { background-color: #007bff; }
    .btn-disp { background-color: #6c757d; width: 40%; max-width: 160px; display: inline-block; }
  </style>
</head>
<body>
  <h1>Control RC</h1>
  <button class="btn-main btn-start" onclick="fetch('/start')">INICIAR</button><br>
  <button class="btn-main btn-stop" onclick="fetch('/stop')">PARAR (Juez)</button><br>
  <button class="btn-main btn-reset" onclick="fetch('/reset')">RESETEAR</button><hr>
  <button class="btn-disp" onclick="fetch('/display_on')">ON</button>
  <button class="btn-disp" onclick="fetch('/display_off')">OFF</button>
</body>
</html>
)rawliteral";

// --- FUNCIONES NÚCLEO ---
void actualizarPantallaTiempo() {
  if (!displayEnabled) return; 
  if (elapsedMillis < 60000) {
    display.showNumberDecEx(((elapsedMillis / 1000) * 100) + ((elapsedMillis % 1000) / 10), 0b01000000, true);
  } else {
    display.showNumberDecEx((((elapsedMillis / 1000) / 60) % 100 * 100) + ((elapsedMillis / 1000) % 60), 0b01000000, true);
  }
}

void accionIniciar() {
  displayEnabled = true;
  display.setBrightness(0x0f, true);
  if (!isRunning && !isCountingDown) {
    isCountingDown = true;
    countdownStart = millis(); 
  }
  Serial.println("ACCIÓN: Iniciar");
}

void accionParar() {
  if (isCountingDown) { isCountingDown = false; display.showNumberDecEx(0, 0b01000000, true); }
  if (isRunning) { elapsedMillis = millis() - startTime; isRunning = false; actualizarPantallaTiempo(); }
  Serial.println("ACCIÓN: Parar");
}

void accionResetear() {
  isRunning = false; isCountingDown = false; elapsedMillis = 0;
  if (displayEnabled) display.showNumberDecEx(0, 0b01000000, true);
  Serial.println("ACCIÓN: Resetear");
}

void accionTogglePantalla() {
  if (displayEnabled) {
    displayEnabled = false;
    display.clear();
    display.setBrightness(0x0f, false);
    Serial.println("ACCIÓN: Pantalla OFF (Toggle)");
  } else {
    displayEnabled = true;
    display.setBrightness(0x0f, true);
    if (!isCountingDown) actualizarPantallaTiempo();
    Serial.println("ACCIÓN: Pantalla ON (Toggle)");
  }
}

// --- Rutas Servidor Web ---
void handleRoot() { server.send(200, "text/html", index_html); }
void handleStart() { accionIniciar(); server.send(200, "text/plain", "OK"); }
void handleStop() { accionParar(); server.send(200, "text/plain", "OK"); }
void handleReset() { accionResetear(); server.send(200, "text/plain", "OK"); }
void handleDisplayOn() { if(!displayEnabled) accionTogglePantalla(); server.send(200, "text/plain", "OK"); }
void handleDisplayOff() { if(displayEnabled) accionTogglePantalla(); server.send(200, "text/plain", "OK"); }
void handleNotFound() { server.sendHeader("Location", String("http://") + apIP.toString(), true); server.send(302, "text/plain", ""); }

// --- Setup Inicial ---
void setup() {
  Serial.begin(115200);
  
  display.clear();
  display.setBrightness(0x0f, false);

  irrecv.enableIRIn();  
  Serial.println("\n📡 Receptor IR Listo.");

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid);
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/reset", handleReset);
  server.on("/display_on", handleDisplayOn);
  server.on("/display_off", handleDisplayOff);
  server.onNotFound(handleNotFound);
  server.begin();
}

// --- Bucle Principal ---
void loop() {
  dnsServer.processNextRequest(); 
  server.handleClient();          

  // --- LECTOR DE INFRARROJOS ---
  if (irrecv.decode(&results)) {
    if (results.value != 0xFFFFFFFF) {
      Serial.print("Boton IR pulsado. Código HEX: 0x");
      serialPrintUint64(results.value, HEX);
      Serial.println("");

      // Mapeo de los botones botones del control remoto IR 
      switch(results.value) {
        case 0xFF02FD: accionTogglePantalla(); break; // Prender/Apagar
        case 0xFF22DD: accionIniciar(); break;        // Iniciar
        case 0xFF12ED: accionParar(); break;          // Parar
        case 0xFF32CD: accionResetear(); break;       // Resetear
      }
    }
    irrecv.resume(); 
  }

  // --- LÓGICA DEL SEMÁFORO ---
  if (isCountingDown && displayEnabled) {
    unsigned long elapsedCountdown = millis() - countdownStart;
    
    // Fase 1: Serpiente
    if (elapsedCountdown < 1800) { display.setSegments(snakeTrack[(elapsedCountdown / 50) % 12]); } 
    // Fase 2: Apagón inicial
    else if (elapsedCountdown < 2200) { display.clear(); } 
    // Fases 3 a 6: Cuenta atrás 4, 3, 2, 1
    else if (elapsedCountdown < 3200) { uint8_t data[] = { display.encodeDigit(4), 0, 0, 0 }; display.setSegments(data); } 
    else if (elapsedCountdown < 4200) { uint8_t data[] = { 0, display.encodeDigit(3), 0, 0 }; display.setSegments(data); } 
    else if (elapsedCountdown < 5200) { uint8_t data[] = { 0, 0, display.encodeDigit(2), 0 }; display.setSegments(data); } 
    else if (elapsedCountdown < 6200) { uint8_t data[] = { 0, 0, 0, display.encodeDigit(1) }; display.setSegments(data); } 
    // Fase 7: Breve apagón final para dar tensión a la salida (300ms)
    else if (elapsedCountdown < 6500) { display.clear(); } 
    // ¡ARRANCA!
    else { isCountingDown = false; isRunning = true; startTime = millis() - elapsedMillis; }
  }
  
  // --- LÓGICA DEL CRONÓMETRO ---
  else if (isRunning) {
    elapsedMillis = millis() - startTime;
    if (millis() - lastDisplayUpdate > 40) {
      lastDisplayUpdate = millis();
      actualizarPantallaTiempo();
    }
  }
}
