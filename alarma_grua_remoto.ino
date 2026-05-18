#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==================================================
// RADIO
// ==================================================
HardwareSerial RADIO(1);
#define RADIO_RX 26
#define RADIO_TX 27

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

// ==================================================
// RELAY REMOTO (ESPEJO DE ALARMA)
// ==================================================
#define RELAY_PIN 25
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// ==================================================
// BUZZER
// ==================================================
#define BUZZER_PIN 33
#define BUZZER_ON  HIGH
#define BUZZER_OFF LOW

// ==================================================
// BOTON
// ==================================================
#define RESET_BUTTON 32
#define LONG_PRESS_MS 3000

// ==================================================
// LINK WATCHDOG
// ==================================================
#define LINK_TIMEOUT_MS 1500
uint32_t lastRadioRxMs = 0;
bool linkOK = false;

// ==================================================
// ESTADO RECIBIDO DESDE EL NODO LOCAL
// ==================================================
int pata55 = 9999;
int pata56 = 9999;
int pata57 = 9999;
int pata07 = 9999;

bool alarmaActiva = false;
bool alarmaLatch  = false;

bool off1 = false;
bool off2 = false;
bool off3 = false;
bool off4 = false;

// máscara calculada en remoto para mostrar patas en peligro
uint8_t alarmMaskLatched = 0;

// ==================================================
// UI DISPLAY
// ==================================================
bool invNow = false;
bool blinkOn = true;
uint32_t lastBlinkMs = 0;
#define BLINK_PERIOD_MS 400

uint32_t uiMsgUntilMs = 0;
String   uiMsgLine1 = "";
String   uiMsgLine2 = "";

// ==================================================
// BOTON ESTADO
// ==================================================
uint32_t botonPressMs = 0;
bool botonLargoEjecutado = false;

// ==================================================
// FORWARD DECLARATIONS
// ==================================================
void actualizarDisplay();
void drawWarningIcon(int x, int y);
void printOffSensors();
void printMaskLatched();
void leerBoton();
void procesarLineaRadio(const String& line);
bool parseEstado(const String& s);
void enviarComandoRadio(const char* cmd);
const char* labelFromIndex(int idx);
void actualizarBuzzer();

// ==================================================

void setup() {
  Serial.begin(115200);

  // relay remoto
  digitalWrite(RELAY_PIN, RELAY_OFF);
  pinMode(RELAY_PIN, OUTPUT);

  // buzzer
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  pinMode(BUZZER_PIN, OUTPUT);

  // botón
  pinMode(RESET_BUTTON, INPUT_PULLUP);

  // radio
  RADIO.begin(9600, SERIAL_8N1, RADIO_RX, RADIO_TX);

  // OLED
  Wire.begin(I2C_SDA, I2C_SCL);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOK) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("REMOTE PANEL");
    display.setCursor(0, 16);
    display.println("WAITING LINK...");
    display.display();
  }

  Serial.println("REMOTE NODE ONLINE");
}

void loop() {
  // recibir radio
  while (RADIO.available()) {
    String line = RADIO.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      Serial.print("RX RADIO: ");
      Serial.println(line);
      procesarLineaRadio(line);
    }
  }

  // watchdog de enlace
  uint32_t now = millis();
  if (linkOK && (now - lastRadioRxMs > LINK_TIMEOUT_MS)) {
    linkOK = false;
    Serial.println("LINK OFF");
  }

  // relay remoto: espejo del latch si hay enlace, fail indication si no hay enlace
  if (linkOK) {
    digitalWrite(RELAY_PIN, alarmaLatch ? RELAY_ON : RELAY_OFF);
  } else {
    digitalWrite(RELAY_PIN, RELAY_ON);
  }

  leerBoton();
  actualizarBuzzer();
  actualizarDisplay();
}

// ==================================================
// RADIO
// ==================================================
void procesarLineaRadio(const String& line) {
  if (parseEstado(line)) {
    lastRadioRxMs = millis();
    linkOK = true;

    // recalcula patas que están bajo alarma actualmente
    alarmMaskLatched = 0;
    if (pata55 < 2000 && !off1) alarmMaskLatched |= (1 << 0);
    if (pata56 < 2000 && !off2) alarmMaskLatched |= (1 << 1);
    if (pata57 < 2000 && !off3) alarmMaskLatched |= (1 << 2);
    if (pata07 < 2000 && !off4) alarmMaskLatched |= (1 << 3);
  }
}

bool parseEstado(const String& s) {
  if (!s.startsWith("$")) return false;

  String payload = s.substring(1);

  int values[10];
  int idx = 0;
  int start = 0;

  for (int i = 0; i <= payload.length(); i++) {
    if (i == payload.length() || payload[i] == ',') {
      if (idx >= 10) return false;
      String token = payload.substring(start, i);
      token.trim();
      values[idx++] = token.toInt();
      start = i + 1;
    }
  }

  if (idx != 10) return false;

  pata55 = values[0];
  pata56 = values[1];
  pata57 = values[2];
  pata07 = values[3];

  alarmaActiva = (values[4] != 0);
  alarmaLatch  = (values[5] != 0);

  off1 = (values[6] != 0);
  off2 = (values[7] != 0);
  off3 = (values[8] != 0);
  off4 = (values[9] != 0);

  return true;
}

void enviarComandoRadio(const char* cmd) {
  RADIO.println(cmd);
  Serial.print("TX RADIO: ");
  Serial.println(cmd);
}

// ==================================================
// BOTON
// corto -> BORRA
// largo -> RESET
// ==================================================
void leerBoton() {
  static bool last = HIGH;
  bool estado = digitalRead(RESET_BUTTON);
  uint32_t now = millis();

  // pulsado
  if (last == HIGH && estado == LOW) {
    botonPressMs = now;
    botonLargoEjecutado = false;
  }

  // largo
  if (estado == LOW && !botonLargoEjecutado) {
    if (now - botonPressMs >= LONG_PRESS_MS) {
      botonLargoEjecutado = true;

      uiMsgLine1 = "ENVIANDO";
      uiMsgLine2 = "RESET";
      uiMsgUntilMs = millis() + 1200;

      enviarComandoRadio("RESET");
    }
  }

  // soltado
  if (last == LOW && estado == HIGH) {
    uint32_t duracion = now - botonPressMs;

    if (!botonLargoEjecutado && duracion < LONG_PRESS_MS) {
      uiMsgLine1 = "ENVIANDO";
      uiMsgLine2 = "BORRA";
      uiMsgUntilMs = millis() + 1000;

      enviarComandoRadio("BORRA");
    }

    delay(50);
  }

  last = estado;
}

void actualizarBuzzer() {
  static uint32_t lastChangeMs = 0;
  static bool buzzState = false;
  uint32_t now = millis();

  // LINK OFF -> suave: pulso corto cada 600 ms
  if (!linkOK) {
    if (!buzzState) {
      // apagado -> esperar 580 ms
      if (now - lastChangeMs >= 580) {
        lastChangeMs = now;
        buzzState = true;
        digitalWrite(BUZZER_PIN, BUZZER_ON);
      }
    } else {
      // encendido -> solo 20 ms
      if (now - lastChangeMs >= 20) {
        lastChangeMs = now;
        buzzState = false;
        digitalWrite(BUZZER_PIN, BUZZER_OFF);
      }
    }
    return;
  }

  // PELIGRO ACTIVO -> más notorio
  if (alarmaActiva) {
    if (!buzzState) {
      if (now - lastChangeMs >= 250) {
        lastChangeMs = now;
        buzzState = true;
        digitalWrite(BUZZER_PIN, BUZZER_ON);
      }
    } else {
      if (now - lastChangeMs >= 250) {
        lastChangeMs = now;
        buzzState = false;
        digitalWrite(BUZZER_PIN, BUZZER_OFF);
      }
    }
    return;
  }

  // ENCLAVADA sin peligro -> lento
  if (alarmaLatch) {
    if (!buzzState) {
      if (now - lastChangeMs >= 700) {
        lastChangeMs = now;
        buzzState = true;
        digitalWrite(BUZZER_PIN, BUZZER_ON);
      }
    } else {
      if (now - lastChangeMs >= 120) {
        lastChangeMs = now;
        buzzState = false;
        digitalWrite(BUZZER_PIN, BUZZER_OFF);
      }
    }
    return;
  }

  // NORMAL -> apagado
  buzzState = false;
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

// ==================================================
// DISPLAY HELPERS
// ==================================================
const char* labelFromIndex(int idx) {
  if (idx == 0) return "01";
  if (idx == 1) return "02";
  if (idx == 2) return "03";
  if (idx == 3) return "04";
  return "??";
}

void drawWarningIcon(int x, int y) {
  display.drawTriangle(x + 6, y, x, y + 12, x + 12, y + 12, SSD1306_WHITE);
  display.drawLine(x + 6, y + 4, x + 6, y + 9, SSD1306_WHITE);
  display.drawPixel(x + 6, y + 11, SSD1306_WHITE);
}

void printOffSensors() {
  bool any = false;
  if (off1) { display.print("01 "); any = true; }
  if (off2) { display.print("02 "); any = true; }
  if (off3) { display.print("03 "); any = true; }
  if (off4) { display.print("04 "); any = true; }
  if (!any) display.print("-");
}

void printMaskLatched() {
  if (alarmMaskLatched == 0) { display.print("-"); return; }
  if (alarmMaskLatched & (1 << 0)) display.print("01 ");
  if (alarmMaskLatched & (1 << 1)) display.print("02 ");
  if (alarmMaskLatched & (1 << 2)) display.print("03 ");
  if (alarmMaskLatched & (1 << 3)) display.print("04 ");
}

// ==================================================
// DISPLAY
// ==================================================
void actualizarDisplay() {
  if (!oledOK) return;

  uint32_t now = millis();

  // mensaje temporal
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

  // sin enlace
  if (!linkOK) {
    if (invNow) { invNow = false; display.invertDisplay(false); }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("LINK OFF");

    display.setTextSize(1);
    display.setCursor(0, 26);
    display.println("SIN DATOS DEL");
    display.setCursor(0, 38);
    display.println("NODO LOCAL");

    display.setCursor(0, 54);
    display.println("ALARMA REMOTA ON");

    display.display();
    return;
  }

  // blink solo si peligro activo
  if (alarmaActiva && (now - lastBlinkMs >= BLINK_PERIOD_MS)) {
    lastBlinkMs = now;
    blinkOn = !blinkOn;
  }
  if (!alarmaActiva) blinkOn = true;

  // invertir solo si peligro activo
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

  // 1) PELIGRO ACTIVO
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

      display.setCursor(0, 46);
      display.print("RESET: NO (aun)");
    } else {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("ALERTA (PELIGRO)...");
    }
    display.display();
    return;
  }

  // 2) SEGURA PERO ENCLAVADA
  if (alarmaLatch) {
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("CONDICION: SEGURA");

    display.setCursor(0, 14);
    display.print("ALARMA ENCLAVADA");

    display.setCursor(0, 28);
    display.print("PATA: ");
    printMaskLatched();

    display.setCursor(0, 42);
    display.print("OFF: ");
    printOffSensors();

    display.setCursor(0, 54);
    display.print("APRIETE RESET");

    display.display();
    return;
  }

  // 3) NORMAL
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("RUN OK");

  display.setCursor(0, 16);
  display.print("CONDICION: NORMAL");

  display.setCursor(0, 32);
  display.print("OFF(sin rsp): ");
  printOffSensors();

  display.setCursor(0, 48);
  display.print("ALARMA: OFF");

  display.display();
}