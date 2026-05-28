#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==================================================
// RS485
// ==================================================
#define RX2_PIN 16
#define TX2_PIN 17

HardwareSerial RS485(2);
HardwareSerial RADIO(1);

// ==================================================
// RADIO
// ==================================================
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
// RESET BUTTON
// ==================================================
#define RESET_BUTTON 32

// ==================================================
// ALARMA
// ==================================================
#define ALARM_ON_MM   3000
#define ALARM_OFF_MM  3100

// Tiempo máximo sonando cuando la condición YA se normalizó
#define ALARM_AUTO_SILENCE_MS 15000UL

bool alarmaActiva = false;  // condición física (con histéresis)
bool alarmaLatch  = false;  // relay enclavado hasta reset o timeout

uint32_t alarmaStartMs    = 0;  // momento en que alarmaActiva se puso true
uint32_t alarmaSeguraMs   = 0;  // momento en que la condición se normalizó
bool     condSegura       = false; // true cuando físicamente ya está bien

uint8_t alarmMaskLatched = 0;

// ==================================================
// DISTANCIAS
// ==================================================
int pata55 = 9999;
int pata56 = 9999;
int pata57 = 9999;
int pata07 = 9999;

const char* IDS[4] = {"55", "56", "57", "07"};

// ==================================================
// ETIQUETAS
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
// ERRORES / NO RESPUESTA
// ==================================================
uint8_t bad55 = 0, bad56 = 0, bad57 = 0, bad07 = 0;

uint8_t* badCounterFor(const String& id) {
  if (id == "55") return &bad55;
  if (id == "56") return &bad56;
  if (id == "57") return &bad57;
  if (id == "07") return &bad07;
  return nullptr;
}

#define MISS_LIMIT 6
uint16_t miss55 = 0, miss56 = 0, miss57 = 0, miss07 = 0;

// ==================================================
// OLED
// ==================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define I2C_SDA 21
#define I2C_SCL 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool oledOK = false;

bool invNow = false;
bool blinkOn = true;
uint32_t lastBlinkMs = 0;
#define BLINK_PERIOD_MS 400

uint32_t uiMsgUntilMs = 0;
String uiMsgLine1 = "";
String uiMsgLine2 = "";

// ==================================================
// DECLARACIONES
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
// SETUP
// ==================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  digitalWrite(RELAY_PIN, RELAY_OFF);
  pinMode(RELAY_PIN, OUTPUT);

  for (int i = 0; i < 6; i++) {
    Serial.println("Relay ON");
    digitalWrite(RELAY_PIN, RELAY_ON);
    delay(300);
    Serial.println("Relay OFF");
    digitalWrite(RELAY_PIN, RELAY_OFF);
    delay(300);
  }

  pinMode(RESET_BUTTON, INPUT_PULLUP);

  RS485.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);
  RADIO.begin(9600, SERIAL_8N1, RADIO_RX, RADIO_TX);

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

// ==================================================
// LOOP
// ==================================================
void loop() {
  for (int i = 0; i < 4; i++) {
    String cmd = makeCmd(String(IDS[i]), "03");
    pollOne(cmd);
      // leerReset();   // DESACTIVADO: la caja local no tiene botón físico
    delay(10);
  }

  leerComandosRadio();
  evaluarAlarma();

  if (millis() - lastRadioTxMs >= RADIO_TX_PERIOD_MS) {
    lastRadioTxMs = millis();
    enviarEstadoRadio();
  }

  actualizarDisplay();

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.print(label55()); Serial.print("="); Serial.print(pata55);
    Serial.print(" "); Serial.print(label56()); Serial.print("="); Serial.print(pata56);
    Serial.print(" "); Serial.print(label57()); Serial.print("="); Serial.print(pata57);
    Serial.print(" "); Serial.print(label07()); Serial.print("="); Serial.print(pata07);
    Serial.print(" mm | activa="); Serial.print(alarmaActiva ? "1" : "0");
    Serial.print(" latch="); Serial.print(alarmaLatch ? "1" : "0");
    Serial.print(" segura="); Serial.println(condSegura ? "1" : "0");
  }
}

// ==================================================
// RS485
// ==================================================
void pollOne(const String& cmd) {
  while (RS485.available()) (void)RS485.read();

  Serial.print("TX: ");
  Serial.println(cmd);

  RS485.print(cmd);
  RS485.flush();
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
// PARSER
// ==================================================
void procesarDato(const String& tramaIn) {
  String trama = tramaIn;
  trama.trim();

  if (!trama.startsWith(":")) return;

  if (trama.length() < 11) {
    if (trama.length() >= 3) {
      String id = trama.substring(1, 3);
      uint8_t* bad = badCounterFor(id);
      if (bad) {
        if (*bad == 0) {
          *bad = 1;
          Serial.print("TRAMA CORTA descartada PATA ");
          Serial.println(labelFromId(id));
          return;
        } else {
          *bad = 0;
          return;
        }
      }
    }
    return;
  }

  String id      = trama.substring(1, 3);
  uint8_t* bad   = badCounterFor(id);
  if (!bad) return;

  String distStr = trama.substring(3, 9);
  String lrcStr  = trama.substring(9, 11);

  bool distOK = true;
  for (int i = 0; i < 6; i++) {
    char c = distStr[i];
    if (c < '0' || c > '9') { distOK = false; break; }
  }

  bool lrcHexOK = isHexChar(lrcStr[0]) && isHexChar(lrcStr[1]);

  bool lrcOK = false;
  if (distOK && lrcHexOK) {
    String frameNoLRC = trama.substring(0, 9);
    uint8_t expected  = calcLRC_sensor_digits(frameNoLRC);
    uint8_t got       = parseHexByte(trama, 9);
    lrcOK = (expected == got);
  }

  if (!distOK || !lrcHexOK || !lrcOK) {
    if (*bad == 0) {
      *bad = 1;
      Serial.print("TRAMA INVALIDA descartada PATA ");
      Serial.println(labelFromId(id));
      return;
    } else {
      *bad = 0;
      if (!distOK) return;
    }
  } else {
    *bad = 0;
  }

  long distancia = distStr.toInt();
  if (distancia <= 0) return;

  if      (id == "55") { pata55 = (int)distancia; miss55 = 0; }
  else if (id == "56") { pata56 = (int)distancia; miss56 = 0; }
  else if (id == "57") { pata57 = (int)distancia; miss57 = 0; }
  else if (id == "07") { pata07 = (int)distancia; miss07 = 0; }

  Serial.print("PATA "); Serial.print(labelFromId(id));
  Serial.print(" = "); Serial.print(distancia);
  Serial.print(" mm | LRC "); Serial.println(lrcOK ? "OK" : "BAD");
}

// ==================================================
// ALARMA
// Reglas:
//  1. Disparo: cualquier pata < ALARM_ON_MM  → alarmaActiva=true, latch=true
//  2. Condición se normaliza (todas > ALARM_OFF_MM) → alarmaActiva=false
//     pero el relay (latch) SIGUE encendido.
//  3. Latch se apaga SOLO si:
//     a) Botón reset presionado (con condición ya normalizada), O
//     b) Han pasado 15 s DESDE QUE LA CONDICIÓN SE NORMALIZÓ
//  4. Mientras la condición siga baja, el relay nunca se apaga por timeout.
// ==================================================
void evaluarAlarma() {
  uint32_t now = millis();

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

  // --- Activación ---
  if (!alarmaActiva && algunaBaja) {
    alarmaActiva  = true;
    alarmaLatch   = true;
    condSegura    = false;
    alarmaStartMs = now;

    alarmMaskLatched = 0;
    if (pata55 < ALARM_ON_MM) alarmMaskLatched |= (1 << 0);
    if (pata56 < ALARM_ON_MM) alarmMaskLatched |= (1 << 1);
    if (pata57 < ALARM_ON_MM) alarmMaskLatched |= (1 << 2);
    if (pata07 < ALARM_ON_MM) alarmMaskLatched |= (1 << 3);

    Serial.println(">>> ALARMA ACTIVADA");
  }

  // --- Condición física normalizada ---
  if (alarmaActiva && todasAltas) {
    alarmaActiva  = false;
    condSegura    = true;
    alarmaSeguraMs = now;   // arranca el contador de 15 s
    Serial.println(">>> CONDICION SEGURA - iniciando timeout 15s");
  }

  // --- Timeout de 15 s (solo cuando la condición ya está bien) ---
  if (alarmaLatch && condSegura &&
      (now - alarmaSeguraMs >= ALARM_AUTO_SILENCE_MS)) {

    alarmaLatch      = false;
    condSegura       = false;
    alarmMaskLatched = 0;

    uiMsgLine1   = "SILENCIO";
    uiMsgLine2   = "AUTOMATICO";
    uiMsgUntilMs = now + 2000;

    Serial.println(">>> LATCH APAGADO POR TIMEOUT 15s");
  }

  // --- Si la condición vuelve a bajar estando en espera de timeout, reactivar ---
  if (!alarmaActiva && condSegura && algunaBaja) {
    alarmaActiva  = true;
    condSegura    = false;
    alarmaStartMs = now;

    alarmMaskLatched = 0;
    if (pata55 < ALARM_ON_MM) alarmMaskLatched |= (1 << 0);
    if (pata56 < ALARM_ON_MM) alarmMaskLatched |= (1 << 1);
    if (pata57 < ALARM_ON_MM) alarmMaskLatched |= (1 << 2);
    if (pata07 < ALARM_ON_MM) alarmMaskLatched |= (1 << 3);

    Serial.println(">>> ALARMA RE-ACTIVADA durante espera de timeout");
  }

  digitalWrite(RELAY_PIN, alarmaLatch ? RELAY_ON : RELAY_OFF);
  Serial.print("RELAY="); Serial.println(alarmaLatch ? "ON" : "OFF");
}

// ==================================================
// RESET LOCAL
// Solo funciona cuando la condición física ya está normalizada
// ==================================================
void leerReset() {
  static bool last = HIGH;
  bool estado = digitalRead(RESET_BUTTON);

  if (last == HIGH && estado == LOW) {
    Serial.println("RESET PRESIONADO");

    if (!alarmaActiva) {
      // Condición normalizada: apagar latch
      alarmaLatch      = false;
      condSegura       = false;
      alarmMaskLatched = 0;

      uiMsgLine1   = "OK: ALARMA";
      uiMsgLine2   = "CORTADA";
      uiMsgUntilMs = millis() + 2000;

      Serial.println("ALARMA RESETEADA (latch=0)");
    } else {
      // Condición sigue activa: no se puede resetear
      uiMsgLine1   = "NO SE PUEDE";
      uiMsgLine2   = "SIGUE PELIGRO";
      uiMsgUntilMs = millis() + 2500;

      Serial.println("CONDICION SIGUE ACTIVA - reset rechazado");
    }

    delay(250);
  }

  last = estado;
}

// ==================================================
// HEX / LRC
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
  return (uint8_t)((hexNibble(s[idx]) << 4) | hexNibble(s[idx + 1]));
}

uint8_t calcLRC_sensor_digits(const String& frameNoLRC) {
  uint8_t sum = 0;
  for (int i = 0; i < frameNoLRC.length(); i++) {
    char c = frameNoLRC[i];
    if (c >= '0' && c <= '9') sum = (uint8_t)(sum + (uint8_t)(c - '0'));
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
// DISPLAY
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

  // Mensaje temporal (reset, silencio, etc.)
  if (now < uiMsgUntilMs) {
    if (invNow) { invNow = false; display.invertDisplay(false); }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println(uiMsgLine1);
    display.setCursor(0, 28);
    display.println(uiMsgLine2);
    display.display();
    return;
  }

  // Parpadeo en alarma activa
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
    // ---- PELIGRO ACTIVO ----
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
      display.setCursor(0, 46);
      display.print("AUTO OFF 15s");
    } else {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("ALERTA...");
    }

  } else if (alarmaLatch && condSegura) {
    // ---- CONDICIÓN NORMALIZADA, ESPERANDO RESET O TIMEOUT ----
    drawWarningIcon(2, 0);
    display.setTextSize(1);
    display.setCursor(18, 2);
    display.print("COND: SEGURA");
    display.setCursor(0, 18);
    display.print("ALARMA ENCLAVADA");
    display.setCursor(0, 30);
    display.print("PATA: ");
    printMaskLatched();

    // Cuenta regresiva
    uint32_t elapsed   = now - alarmaSeguraMs;
    int remaining = (int)((ALARM_AUTO_SILENCE_MS - elapsed) / 1000) + 1;
    if (remaining < 0) remaining = 0;
    display.setCursor(0, 44);
    display.print("AUTO OFF en ");
    display.print(remaining);
    display.print("s");

    display.setCursor(0, 56);
    display.print("o apriete RESET");

  } else {
    // ---- NORMAL ----
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("CA109  NORMAL");
    display.setCursor(0, 16);
    display.print("01:"); display.print(pata55); display.print("mm");
    display.setCursor(64, 16);
    display.print("02:"); display.print(pata56); display.print("mm");
    display.setCursor(0, 28);
    display.print("03:"); display.print(pata57); display.print("mm");
    display.setCursor(64, 28);
    display.print("04:"); display.print(pata07); display.print("mm");
    display.setCursor(0, 44);
    display.print("OFF: ");
    printOffSensors();
  }

  display.display();
}

// ==================================================
// RADIO
// ==================================================
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
      alarmaLatch      = false;
      condSegura       = false;
      alarmMaskLatched = 0;
      uiMsgLine1   = "OK: ALARMA";
      uiMsgLine2   = "CORTADA";
      uiMsgUntilMs = millis() + 2000;
      Serial.println("ALARMA RESETEADA POR RADIO");
    } else {
      uiMsgLine1   = "NO SE PUEDE";
      uiMsgLine2   = "SIGUE PELIGRO";
      uiMsgUntilMs = millis() + 2500;
      Serial.println("BORRA POR RADIO RECHAZADO");
    }
  } else if (cmd == "RESET") {
    Serial.println("COMANDO RADIO: RESET");
    alarmaActiva     = false;
    alarmaLatch      = false;
    condSegura       = false;
    alarmMaskLatched = 0;
    uiMsgLine1   = "RESET TOTAL";
    uiMsgLine2   = "POR RADIO";
    uiMsgUntilMs = millis() + 2000;
    Serial.println("RESET TOTAL POR RADIO EJECUTADO");
  }
}

void enviarEstadoRadio() {
  bool off1 = (miss55 >= MISS_LIMIT);
  bool off2 = (miss56 >= MISS_LIMIT);
  bool off3 = (miss57 >= MISS_LIMIT);
  bool off4 = (miss07 >= MISS_LIMIT);

  RADIO.print("$");
  RADIO.print(pata55); RADIO.print(",");
  RADIO.print(pata56); RADIO.print(",");
  RADIO.print(pata57); RADIO.print(",");
  RADIO.print(pata07); RADIO.print(",");
  RADIO.print(alarmaActiva ? 1 : 0); RADIO.print(",");
  RADIO.print(alarmaLatch  ? 1 : 0); RADIO.print(",");
  RADIO.print(off1 ? 1 : 0); RADIO.print(",");
  RADIO.print(off2 ? 1 : 0); RADIO.print(",");
  RADIO.print(off3 ? 1 : 0); RADIO.print(",");
  RADIO.println(off4 ? 1 : 0);
}
