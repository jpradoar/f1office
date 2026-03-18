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
#define BUZZER_PIN 12 // Zumbador (Pin D6)

TM1637Display display(CLK, DIO);
IRrecv irrecv(IR_PIN);
decode_results results;

// --- Configuración WiFi y Portal Cautivo ---
const char* ssid = "rc-control";
const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer server(80);
IPAddress apIP(192, 168, 4, 1);

// --- Variables del Sistema ---
bool displayEnabled = false; 
bool isMuted = false;

// Variables Cronómetro Normal
bool isRunning = false;
bool isCountingDown = false; // Para el semáforo 4-3-2-1
unsigned long startTime = 0;
unsigned long elapsedMillis = 0;
unsigned long countdownStart = 0; 
unsigned long lastDisplayUpdate = 0;

// Variables Cuenta Regresiva (Tandas / Heats)
bool isReverseCounting = false;
unsigned long reverseTargetMillis = 0;
unsigned long reverseStartTime = 0;

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
    .btn-heat { background-color: #444; width: 28%; font-size: 1em; padding: 15px 5px; }
    hr { width: 85%; max-width: 350px; border: 1px solid #444; margin: 15px auto; }
  </style>
</head>
<body>
  <h1>Control RC</h1>
  <button class="btn-main btn-start" onclick="fetch('/start')">INICIAR</button><br>
  <button class="btn-main btn-stop" onclick="fetch('/stop')">PARAR</button><br>
  <button class="btn-main btn-reset" onclick="fetch('/reset')">RESETEAR</button>
  
  <hr>
  <p style="margin-bottom: 5px; color: #aaa; font-size: 0.9em;">Tandas Regresivas</p>
  <button class="btn-heat" style="border-bottom: 4px solid red;" onclick="fetch('/heat1')">1 Min</button>
  <button class="btn-heat" style="border-bottom: 4px solid green;" onclick="fetch('/heat3')">3 Min</button>
  <button class="btn-heat" style="border-bottom: 4px solid blue;" onclick="fetch('/heat5')">5 Min</button>

  <hr>
  <p style="margin-bottom: 5px; color: #aaa; font-size: 0.9em;">Sistema</p>
  <button class="btn-disp" onclick="fetch('/display_on')">ON</button>
  <button class="btn-disp" onclick="fetch('/display_off')">OFF</button>
  <button class="btn-disp" style="width: 85%; max-width: 350px; margin-top:10px;" onclick="fetch('/mute')">Mute / Sonido</button>
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

void accionEncenderPantalla() {
  if (!displayEnabled) {
    displayEnabled = true;
    display.setBrightness(0x0f, true);
    if (!isCountingDown && !isReverseCounting) actualizarPantallaTiempo();
    Serial.println("Pantalla ON");
  }
}

void accionApagarPantalla() {
  if (displayEnabled) {
    displayEnabled = false;
    display.clear();
    display.setBrightness(0x0f, false);
    Serial.println("Pantalla OFF");
  }
}

void accionMute() {
  isMuted = !isMuted;
  if (isMuted) {
    noTone(BUZZER_PIN);
    Serial.println("Sistema Silenciado");
  } else {
    Serial.println("Sonido Activado");
    tone(BUZZER_PIN, 1500, 150); // Pequeño pitido de confirmación
  }
}

void accionIniciar() {
  displayEnabled = true;
  display.setBrightness(0x0f, true);
  isReverseCounting = false; // Cancela cualquier cuenta atrás si la hubiera
  if (!isRunning && !isCountingDown) {
    isCountingDown = true;
    countdownStart = millis(); 
  }
  Serial.println("INICIAR (Cronómetro)");
}

void accionParar() {
  noTone(BUZZER_PIN); 
  isReverseCounting = false;
  
  if (isCountingDown) { isCountingDown = false; display.showNumberDecEx(0, 0b01000000, true); }
  if (isRunning) { elapsedMillis = millis() - startTime; isRunning = false; actualizarPantallaTiempo(); }
  Serial.println("PARAR");
}

void accionResetear() {
  noTone(BUZZER_PIN);
  isRunning = false; isCountingDown = false; isReverseCounting = false; elapsedMillis = 0;
  if (displayEnabled) display.showNumberDecEx(0, 0b01000000, true);
  Serial.println("RESETEAR");
}

void accionCuentaRegresiva(int minutos) {
  noTone(BUZZER_PIN);
  isRunning = false; 
  isCountingDown = false;
  displayEnabled = true;
  display.setBrightness(0x0f, true);

  reverseTargetMillis = minutos * 60000UL;
  reverseStartTime = millis();
  isReverseCounting = true;
  
  if (!isMuted) tone(BUZZER_PIN, 1000, 200); // Pitido de confirmación
  Serial.print("Tanda de "); Serial.print(minutos); Serial.println(" minutos iniciada.");
}

// --- Rutas Servidor Web ---
void handleRoot() { server.send(200, "text/html", index_html); }
void handleStart() { accionIniciar(); server.send(200, "text/plain", "OK"); }
void handleStop() { accionParar(); server.send(200, "text/plain", "OK"); }
void handleReset() { accionResetear(); server.send(200, "text/plain", "OK"); }
void handleDisplayOn() { accionEncenderPantalla(); server.send(200, "text/plain", "OK"); }
void handleDisplayOff() { accionApagarPantalla(); server.send(200, "text/plain", "OK"); }
void handleMute() { accionMute(); server.send(200, "text/plain", "OK"); }
void handleHeat1() { accionCuentaRegresiva(1); server.send(200, "text/plain", "OK"); }
void handleHeat3() { accionCuentaRegresiva(3); server.send(200, "text/plain", "OK"); }
void handleHeat5() { accionCuentaRegresiva(5); server.send(200, "text/plain", "OK"); }
void handleNotFound() { server.sendHeader("Location", String("http://") + apIP.toString(), true); server.send(302, "text/plain", ""); }

// --- Setup Inicial ---
void setup() {
  Serial.begin(115200);
  
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  display.clear();
  display.setBrightness(0x0f, false);

  irrecv.enableIRIn();  

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
  server.on("/mute", handleMute);
  server.on("/heat1", handleHeat1);
  server.on("/heat3", handleHeat3);
  server.on("/heat5", handleHeat5);
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
      switch(results.value) {
        case 0xF7C03F: accionEncenderPantalla(); break; // ON (Rojo)
        case 0xF740BF: accionApagarPantalla(); break;   // OFF (Negro)
        case 0xF700FF: accionIniciar(); break;          // Sol Arriba (Iniciar cronómetro)
        case 0xF7807F: accionParar(); break;            // Sol Abajo (Pausar)
        case 0xF7E01F: accionResetear(); break;         // W Blanca (Resetear)
        
        // --- EXTRAS ---
        case 0xF720DF: accionCuentaRegresiva(1); break; // R (1 Minuto)
        case 0xF7A05F: accionCuentaRegresiva(3); break; // G (3 Minutos)
        case 0xF7609F: accionCuentaRegresiva(5); break; // B (5 Minutos)
        case 0xF7D02F: accionMute(); break;             // FLASH (Silenciar)
      }
    }
    irrecv.resume(); 
  }

  // --- LÓGICA DEL SEMÁFORO (Cronómetro Hacia Adelante) ---
  if (isCountingDown && displayEnabled) {
    unsigned long elapsedCountdown = millis() - countdownStart;
    
    if (elapsedCountdown < 1800) { display.setSegments(snakeTrack[(elapsedCountdown / 50) % 12]); } 
    else if (elapsedCountdown < 2200) { display.clear(); } 
    else if (elapsedCountdown < 3200) { 
      uint8_t data[] = { display.encodeDigit(4), 0, 0, 0 }; display.setSegments(data); 
      if (elapsedCountdown < 2300 && !isMuted) tone(BUZZER_PIN, 1000); else noTone(BUZZER_PIN);
    } 
    else if (elapsedCountdown < 4200) { 
      uint8_t data[] = { 0, display.encodeDigit(3), 0, 0 }; display.setSegments(data); 
      if (elapsedCountdown < 3300 && !isMuted) tone(BUZZER_PIN, 1000); else noTone(BUZZER_PIN);
    } 
    else if (elapsedCountdown < 5200) { 
      uint8_t data[] = { 0, 0, display.encodeDigit(2), 0 }; display.setSegments(data); 
      if (elapsedCountdown < 4300 && !isMuted) tone(BUZZER_PIN, 1000); else noTone(BUZZER_PIN);
    } 
    else if (elapsedCountdown < 6200) { 
      uint8_t data[] = { 0, 0, 0, display.encodeDigit(1) }; display.setSegments(data); 
      if (elapsedCountdown < 5300 && !isMuted) tone(BUZZER_PIN, 1000); else noTone(BUZZER_PIN);
    } 
    else if (elapsedCountdown < 6500) { display.clear(); } 
    else { 
      isCountingDown = false; 
      isRunning = true; 
      startTime = millis() - elapsedMillis; 
      if (!isMuted) tone(BUZZER_PIN, 2000, 800); 
    }
  }
  
  // --- LÓGICA DEL CRONÓMETRO (Hacia Adelante) ---
  else if (isRunning) {
    elapsedMillis = millis() - startTime;
    if (millis() - lastDisplayUpdate > 40) {
      lastDisplayUpdate = millis();
      actualizarPantallaTiempo();
    }
  }

  // --- LÓGICA DE LAS TANDAS (Cuenta Regresiva) ---
  else if (isReverseCounting) {
    unsigned long elapsed = millis() - reverseStartTime;
    
    // Si el tiempo se ha acabado
    if (elapsed >= reverseTargetMillis) {
      isReverseCounting = false;
      display.showNumberDecEx(0, 0b01000000, true);
      if (!isMuted) tone(BUZZER_PIN, 2500, 2000); // Super pitido final de 2 segundos
      Serial.println("¡TIEMPO DE LA TANDA TERMINADO!");
    } 
    // Si aún queda tiempo
    else {
      unsigned long remainingMillis = reverseTargetMillis - elapsed;
      if (millis() - lastDisplayUpdate > 100) {
        lastDisplayUpdate = millis();
        int remainingSecs = remainingMillis / 1000;
        int minutos = (remainingSecs / 60) % 100;
        int segundos = remainingSecs % 60;
        if (displayEnabled) {
          display.showNumberDecEx((minutos * 100) + segundos, 0b01000000, true);
        }
      }
    }
  }
}
