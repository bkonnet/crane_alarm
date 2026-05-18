#include <HardwareSerial.h>

// ======== SOLO PANTALLA ========
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==================================================
// RS485 (placa auto-direccional)
// ==================================================
// WT32-ETH01:
#define RX2_PIN 16
#define TX2_PIN 17

HardwareSerial RS485(2);
HardwareSerial RADIO(1);

#define RADIO_RX 26
#define RADIO_TX 27
uint32_t lastRadioTxMs = 0;
#define RADIO_TX_PERIOD_MS 100

// ==================================================
// RELAY
// ==================================================
#define RELAY_PIN 25
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// ==================================================
// RESET BUTTON (ACK/SILENCE)
// ==================================================
#define RESET_BUTTON 32

// ==================================================
// ALARMA (histéresis)
// ==================================================
#define ALARM_ON_MM   3000
#define ALARM_OFF_MM  3100

bool alarmaActiva = false;   // condición real (con histéresis)
bool alarmaLatch  = false;   // mantiene la alarma hasta reset

// ======== SOLO PANTALLA ========
// Pata(s) que dispararon la alarma al activarse
uint8_t alarmMaskLatched = 0; // bit0=55, bit1=56, bit2=57, bit3=07

// ==================================================
// DISTANCIAS (mm)
// ==================================================
int pata55 = 9999;
int pata56 = 9999;
int pata57 = 9999;
int pata07 = 9999;

// ==================================================
// IDS
// ==================================================
const char* IDS[4] = {"55", "56", "57", "07"};

// ==================================================
// ETIQUETAS VISIBLES (solo display/serial)
// 55=01, 56=02, 57=03 y 07=04
// ==================================================
const char* labelFromId(const String& id) {
  if (id == "55") return "01";
  if (id == "56") return "02";
  if (id == "57") return "03";
  if (id == "07") return "04";
  return "??";
}

const char* label55() { return "01"; }
const char* label56() { return "02"; }
const char* label57() { return "03"; }
const char* label07() { return "04"; }

// ==================================================
// “descartar una vez” por sensor cuando la lectura viene inválida
// ==================================================
uint8_t bad55 = 0, bad56 = 0, bad57 = 0, bad07 = 0;

uint8_t* badCounterFor(const String& id) {
  if (id == "55") return &bad55;
  if (id == "56") return &bad56;
  if (id == "57") return &bad57;
  if (id == "07") return &bad07;
  return nullptr;
}

// ======== SOLO PANTALLA ========
// Sensores “desactivados” por no respuesta
#define MISS_LIMIT 6
uint16_t miss55 = 0, miss56 = 0, miss57 = 0, miss07 = 0;

// OLED (SSD1306/SSD1309 I2C, 128x64)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define I2C_SDA 21
#define I2C_SCL 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool oledOK = false;

// Parpadeo / invertido (solo en PELIGRO)
bool invNow = false;
bool blinkOn = true;
uint32_t lastBlinkMs = 0;
#define BLINK_PERIOD_MS 400

// Mensaje temporal cuando el operador presiona RESET
uint32_t uiMsgUntilMs = 0;
String uiMsgLine1 = "";
String uiMsgLine2 = "";

// ==================================================
// Forward declarations
// ==================================================
void leerReset();
void evaluarAlarma();

void procesarDato(const String& trama);
void pollOne(const String& cmd);
bool readLineRS485(String &out, uint32_t timeoutMs);

bool isHexChar(char c);
int hexNibble(char c);
uint8_t parseHexByte(const String& s, int idx);

uint8_t calcLRC_sensor_digits(const String& frameNoLRC);
String makeCmd(const String& id, const String& fn);

void actualizarDisplay();
void drawWarningIcon(int x, int y);
void printOffSensors();
void printMaskLatched();

void leerComandosRadio();
void procesarComandoRadio(const String& cmd);
void enviarEstadoRadio();

// ==================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  // RELAY: primero nivel seguro, luego OUTPUT
  digitalWrite(RELAY_PIN, RELAY_OFF);
  pinMode(RELAY_PIN, OUTPUT);

  // TEST RELAY
  for (int i = 0; i < 6; i++) {
    Serial.println("Relay ON");
    digitalWrite(RELAY_PIN, RELAY_ON);
    delay(300);

    Serial.println("Relay OFF");
    digitalWrite(RELAY_PIN, RELAY_OFF);
    delay(300);
  }

  pinMode(RESET_BUTTON, INPUT_PULLUP);

  // RS485 auto-direccional: SIN pin EN_485
  RS485.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);

  // Si no responde nada, prueba esta línea en lugar de la anterior:
  // RS485.begin(9600, SERIAL_8E1, RX2_PIN, TX2_PIN);

  RADIO.begin(9600, SERIAL_8N1, RADIO_RX, RADIO_TX);

  // Init OLED
  Wire.begin(I2C_SDA, I2C_SCL);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOK) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("CA109 ONLINE");
    display.display();
  }

  Serial.println("CA109 ONLINE - RS485 AUTO");
}

void loop() {
  // Poll secuencial
  for (int i = 0; i < 4; i++) {
    String cmd = makeCmd(String(IDS[i]), "03");
    pollOne(cmd);
    leerReset();
    delay(10);
  }

  leerComandosRadio();
  evaluarAlarma();

  if (millis() - lastRadioTxMs >= RADIO_TX_PERIOD_MS) {
    lastRadioTxMs = millis();
    enviarEstadoRadio();
  }

  actualizarDisplay();

  // Print estado cada 1s
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.print(label55()); Serial.print("="); Serial.print(pata55);
    Serial.print(" "); Serial.print(label56()); Serial.print("="); Serial.print(pata56);
    Serial.print(" "); Serial.print(label57()); Serial.print("="); Serial.print(pata57);
    Serial.print(" "); Serial.print(label07()); Serial.print("="); Serial.print(pata07);
    Serial.print(" mm | alarmaActiva="); Serial.print(alarmaActiva ? "1" : "0");
    Serial.print(" latch="); Serial.println(alarmaLatch ? "1" : "0");
  }
}

// ==================================================
// Enviar 1 comando y leer 1 respuesta (línea)
// RS485 auto-direccional: SIN DE/RE
// ==================================================
void pollOne(const String& cmd) {
  while (RS485.available()) (void)RS485.read();

  Serial.print("TX: ");
  Serial.println(cmd);

  RS485.print(cmd);
  RS485.flush();

  // Dale tiempo al módulo a volver a recepción
  delay(30);

  String line;
  if (readLineRS485(line, 200)) {
    Serial.print("RX raw: ");
    Serial.println(line);
    procesarDato(line);
  } else {
    if (cmd.length() >= 3) {
      String id = cmd.substring(1, 3);
      if (id == "55") miss55++;
      else if (id == "56") miss56++;
      else if (id == "57") miss57++;
      else if (id == "07") miss07++;
    }

    Serial.print("SIN RESPUESTA a: ");
    Serial.println(cmd);
  }
}

// ==================================================
// Lee una línea completa hasta '\r' o '\n'
// ==================================================
bool readLineRS485(String &out, uint32_t timeoutMs) {
  out = "";
  uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    while (RS485.available()) {
      char c = (char)RS485.read();

      if (c == '\r' || c == '\n') {
        out.trim();
        return out.length() > 0;
      }

      out += c;

      if (out.length() > 80) {
        out = "";
        return false;
      }
    }
  }
  return false;
}

// ==================================================
// Parser según manual:
//  : + ID(2) + DIST(6) + LRC(2 hex)
// Ej: :55012345E7
// ==================================================
void procesarDato(const String& tramaIn) {
  String trama = tramaIn;
  trama.trim();

  if (!trama.startsWith(":")) return;

  // esperado mínimo 11 chars: : +2 +6 +2
  if (trama.length() < 11) {
    if (trama.length() >= 3) {
      String id = trama.substring(1, 3);
      uint8_t* bad = badCounterFor(id);
      if (bad) {
        if (*bad == 0) {
          *bad = 1;
          Serial.print("TRAMA CORTA descartada (1ra) PATA "); Serial.print(labelFromId(id));
          Serial.print(" raw="); Serial.println(trama);
          return;
        } else {
          Serial.print("TRAMA CORTA repetida (sin update) PATA "); Serial.print(labelFromId(id));
          Serial.print(" raw="); Serial.println(trama);
          *bad = 0;
          return;
        }
      }
    }
    return;
  }

  String id = trama.substring(1, 3);
  uint8_t* bad = badCounterFor(id);
  if (!bad) return;

  String distStr = trama.substring(3, 9);
  String lrcStr  = trama.substring(9, 11);

  bool distOK = true;
  for (int i = 0; i < 6; i++) {
    char c = distStr[i];
    if (c < '0' || c > '9') { distOK = false; break; }
  }

  bool lrcHexOK = (isHexChar(lrcStr[0]) && isHexChar(lrcStr[1]));

  bool lrcOK = false;
  if (distOK && lrcHexOK) {
    String frameNoLRC = trama.substring(0, 9);
    uint8_t expected = calcLRC_sensor_digits(frameNoLRC);
    uint8_t got = parseHexByte(trama, 9);
    lrcOK = (expected == got);
  }

  if (!distOK || !lrcHexOK || !lrcOK) {
    if (*bad == 0) {
      *bad = 1;
      Serial.print("TRAMA INVALIDA descartada (1ra) PATA "); Serial.print(labelFromId(id));
      Serial.print(" distOK="); Serial.print(distOK ? "1" : "0");
      Serial.print(" lrcOK=");  Serial.print(lrcOK ? "1" : "0");
      Serial.print(" raw=");    Serial.println(trama);
      return;
    } else {
      Serial.print("TRAMA INVALIDA repetida -> ACEPTADA PATA "); Serial.print(labelFromId(id));
      Serial.print(" raw="); Serial.println(trama);
      *bad = 0;

      if (!distOK) return;
    }
  } else {
    *bad = 0;
  }

  long distancia = distStr.toInt();
  if (distancia <= 0) return;

  if (id == "55") { pata55 = (int)distancia; miss55 = 0; }
  else if (id == "56") { pata56 = (int)distancia; miss56 = 0; }
  else if (id == "57") { pata57 = (int)distancia; miss57 = 0; }
  else if (id == "07") { pata07 = (int)distancia; miss07 = 0; }

  Serial.print("PATA "); Serial.print(labelFromId(id));
  Serial.print(" = "); Serial.print(distancia);
  Serial.print(" mm | LRC="); Serial.print(lrcStr);
  Serial.print(" "); Serial.print(lrcOK ? "(OK)" : "(BAD)");
  Serial.print(" raw="); Serial.println(trama);
}

// ==================================================
// Evaluación de alarma con histéresis + latch manual
// ==================================================
void evaluarAlarma() {
  bool algunaBaja =
    pata55 < ALARM_ON_MM ||
    pata56 < ALARM_ON_MM ||
    pata57 < ALARM_ON_MM ||
    pata07 < ALARM_ON_MM;

  bool todasAltas =
    pata55 > ALARM_OFF_MM &&
    pata56 > ALARM_OFF_MM &&
    pata57 > ALARM_OFF_MM &&
    pata07 > ALARM_OFF_MM;

  if (!alarmaActiva && algunaBaja) {
    alarmaActiva = true;
    alarmaLatch = true;

    alarmMaskLatched = 0;
    if (pata55 < ALARM_ON_MM) alarmMaskLatched |= (1 << 0);
    if (pata56 < ALARM_ON_MM) alarmMaskLatched |= (1 << 1);
    if (pata57 < ALARM_ON_MM) alarmMaskLatched |= (1 << 2);
    if (pata07 < ALARM_ON_MM) alarmMaskLatched |= (1 << 3);

    Serial.println(">>> ALARMA ACTIVADA (latch=1)");
  }

  if (alarmaActiva && todasAltas) {
    alarmaActiva = false;
    Serial.println(">>> CONDICION SEGURA (alarmaActiva=0). Esperando RESET para apagar latch.");
  }

  digitalWrite(RELAY_PIN, alarmaLatch ? HIGH : LOW);
  Serial.print("RELAY="); Serial.println(alarmaLatch ? "ON" : "OFF");
}

// ==================================================
// Botón reset (ACK/SILENCE)
// ==================================================
void leerReset() {
  static bool last = HIGH;
  bool estado = digitalRead(RESET_BUTTON);

  if (last == HIGH && estado == LOW) {
    Serial.println("RESET PRESIONADO");

    if (!alarmaActiva) {
      alarmaLatch = false;
      alarmMaskLatched = 0;

      uiMsgLine1 = "OK: ALARMA";
      uiMsgLine2 = "CORTADA";
      uiMsgUntilMs = millis() + 2000;

      Serial.println("ALARMA RESETEADA (latch=0)");
    } else {
      uiMsgLine1 = "NO SE PUEDE";
      uiMsgLine2 = "SIGUE PELIGRO";
      uiMsgUntilMs = millis() + 2500;

      Serial.println("CONDICION SIGUE ACTIVA (no se puede resetear latch)");
    }

    delay(250);
  }
  last = estado;
}

// ==================================================
// Helpers HEX
// ==================================================
bool isHexChar(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return 0;
}

uint8_t parseHexByte(const String& s, int idx) {
  int hi = hexNibble(s[idx]);
  int lo = hexNibble(s[idx + 1]);
  return (uint8_t)((hi << 4) | lo);
}

// ==================================================
// LRC según manual
// ==================================================
uint8_t calcLRC_sensor_digits(const String& frameNoLRC) {
  uint8_t sum = 0;

  for (int i = 0; i < frameNoLRC.length(); i++) {
    char c = frameNoLRC[i];
    if (c >= '0' && c <= '9') {
      sum = (uint8_t)(sum + (uint8_t)(c - '0'));
    }
  }

  return (uint8_t)(~sum + 1);
}

String makeCmd(const String& id, const String& fn) {
  String base = ":" + id + fn;
  uint8_t lrc = calcLRC_sensor_digits(base);

  char buf[20];
  sprintf(buf, "%s%02X\r\n", base.c_str(), lrc);
  return String(buf);
}

// ==================================================
// ================== SOLO PANTALLA ==================
// ==================================================
void drawWarningIcon(int x, int y) {
  display.drawTriangle(x + 6, y, x, y + 12, x + 12, y + 12, SSD1306_WHITE);
  display.drawLine(x + 6, y + 4, x + 6, y + 9, SSD1306_WHITE);
  display.drawPixel(x + 6, y + 11, SSD1306_WHITE);
}

void printOffSensors() {
  bool any = false;
  if (miss55 >= MISS_LIMIT) { display.print("01 "); any = true; }
  if (miss56 >= MISS_LIMIT) { display.print("02 "); any = true; }
  if (miss57 >= MISS_LIMIT) { display.print("03 "); any = true; }
  if (miss07 >= MISS_LIMIT) { display.print("04 "); any = true; }
  if (!any) display.print("-");
}

void printMaskLatched() {
  if (alarmMaskLatched == 0) { display.print("-"); return; }
  if (alarmMaskLatched & (1 << 0)) display.print("01 ");
  if (alarmMaskLatched & (1 << 1)) display.print("02 ");
  if (alarmMaskLatched & (1 << 2)) display.print("03 ");
  if (alarmMaskLatched & (1 << 3)) display.print("04 ");
}

void actualizarDisplay() {
  if (!oledOK) return;

  uint32_t now = millis();

  if (now < uiMsgUntilMs) {
    if (invNow) { invNow = false; display.invertDisplay(false); }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(uiMsgLine1);

    display.setTextSize(2);
    display.setCursor(0, 28);
    display.println(uiMsgLine2);

    display.display();
    return;
  }

  if (alarmaActiva && (now - lastBlinkMs >= BLINK_PERIOD_MS)) {
    lastBlinkMs = now;
    blinkOn = !blinkOn;
  }
  if (!alarmaActiva) blinkOn = true;

  bool shouldInvert = alarmaActiva;
  if (shouldInvert != invNow) {
    invNow = shouldInvert;
    display.invertDisplay(invNow);
  }

  static uint32_t lastDraw = 0;
  if (now - lastDraw < 150) return;
  lastDraw = now;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (alarmaActiva) {
    if (blinkOn) {
      drawWarningIcon(2, 0);

      display.setTextSize(2);
      display.setCursor(18, 0);
      display.print("ALERTA");

      display.setTextSize(1);
      display.setCursor(0, 22);
      display.print("COND: PELIGRO");

      display.setCursor(0, 34);
      display.print("PATA: ");
      printMaskLatched();
  );
}

void leerComandosRadio() {
  while (RADIO.available()) {
    String cmd = RADIO.readStringUntil('\n');
    cmd.trim();

    if (cmd.length() > 0) {
      Serial.print("CMD RADIO RX: ");
      Serial.println(cmd);
      procesarComandoRadio(cmd);
    }
  }
}

void procesarComandoRadio(const String& cmd) {
  if (cmd == "BORRA") {
    Serial.println("COMANDO RADIO: BORRA");

    if (!alarmaActiva) {
      alarmaLatch = false;
      alarmMaskLatched = 0;

      uiMsgLine1 = "OK: ALARMA";
      uiMsgLine2 = "CORTADA";
      uiMsgUntilMs = millis() + 2000;

      Serial.println("ALARMA RESETEADA POR RADIO (latch=0)");
    } else {
      uiMsgLine1 = "NO SE PUEDE";
      uiMsgLine2 = "SIGUE PELIGRO";
      uiMsgUntilMs = millis() + 2500;

      Serial.println("BORRA POR RADIO RECHAZADO: SIGUE PELIGRO");
    }
  }
  else if (cmd == "RESET") {
    Serial.println("COMANDO RADIO: RESET");

    alarmaActiva = false;
    alarmaLatch = false;
    alarmMaskLatched = 0;

    uiMsgLine1 = "RESET TOTAL";
    uiMsgLine2 = "POR RADIO";
    uiMsgUntilMs = millis() + 2000;

    Serial.println("RESET TOTAL POR RADIO EJECUTADO");
  }
}