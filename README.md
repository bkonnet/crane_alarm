# Crane Alarm – Sistema de Alarma Anticolisión para Grúas

Proyecto desarrollado para detección de obstáculos y prevención de colisiones en grúas mediante sensores láser RS485, comunicación inalámbrica y alarmas locales/remotas.

Repositorio:

https://github.com/bkonnet/crane_alarm

---

# Descripción General

El sistema está diseñado para monitorear continuamente la distancia de obstáculos mediante sensores láser RS485 instalados en la estructura de la grúa.

Cuando uno o más sensores detectan un obstáculo dentro de una distancia peligrosa:

- Se activa una alarma local de 12V.
- Se transmite el estado de alarma a una caja remota.
- La cabina del operador muestra visualmente la condición de peligro.
- El operador puede reconocer/resetear la alarma desde la cabina.

El sistema fue diseñado para ambientes industriales con alto ruido eléctrico y largas distancias de cableado.

---

# Arquitectura General

```text
[Sensores Láser RS485 x4]
            │
            ▼
      [Caja Local]
  ├── ESP32
  ├── RS485 Master
  ├── Lógica de alarma
  ├── Relay alarma 12V
  ├── Radio UART
  └── Sirena local

            ))))) Enlace inalámbrico (((((

      [Caja Remota]
  ├── ESP32
  ├── Pantalla OLED
  ├── Botón RESET
  ├── Buzzer local
  └── Radio UART
```

---

# Funciones del Sistema

## Caja Local

La caja local es el controlador principal del sistema.

Funciones:

- Consulta continuamente los sensores RS485.
- Evalúa condiciones de peligro.
- Ejecuta la lógica de histéresis de alarma.
- Activa un relay para sirena/alarma externa.
- Mantiene el estado de enclavamiento o latch de alarma.
- Transmite el estado de sensores y alarma a la caja remota.
- Gestiona sensores fuera de línea por falta de respuesta RS485.
- Puede aplicar timeout automático de silencio de alarma.

La caja local NO tiene pantalla. Su función es controlar sensores, lógica de alarma y salida física de alarma.

---

## Caja Remota

La caja remota se instala en la cabina del operador.

Funciones:

- Recibe datos desde la caja local.
- Muestra el estado en pantalla OLED.
- Indica visualmente las alarmas.
- Muestra sensores fuera de línea.
- Permite resetear o reconocer alarmas desde la cabina.
- Puede activar buzzer local de cabina.

---

# Hardware Utilizado

## Caja Local

| Elemento | Descripción |
|---|---|
| ESP32 | Controlador principal |
| Módulo RS485 TTL | Comunicación con sensores láser |
| Relay 5V | Activación de alarma externa |
| Transistor NPN | Manejo del relay desde ESP32 |
| Diodo flyback | Protección de bobina del relay |
| Radio UART / LoRa serial | Comunicación inalámbrica con caja remota |
| Fuente DC-DC | Alimentación 5V para electrónica |
| Sensores láser RS485 | Detección de obstáculos |
| Sirena / alarma 12V | Alarma sonora externa |
| Borneras | Conexión de sensores, alimentación y alarma |
| Fusible | Protección de alimentación |
| Caja IP65/IP66 | Gabinete industrial |

---

## Caja Remota

| Elemento | Descripción |
|---|---|
| ESP32 | Controlador remoto |
| Display OLED SSD1306 I2C | Visualización de estado |
| Pulsador RESET | Reconocimiento / reset de alarma |
| Radio UART / LoRa serial | Recepción de datos desde caja local |
| Buzzer | Alarma local de cabina |
| Fuente DC-DC | Alimentación de electrónica |
| Caja IP54/IP65 | Gabinete de cabina |

---

# Sensores RS485

El sistema utiliza 4 sensores láser RS485.

IDs configurados:

| Sensor | ID RS485 | Etiqueta visible |
|---|---|---|
| Sensor 1 | 55 | 01 |
| Sensor 2 | 56 | 02 |
| Sensor 3 | 57 | 03 |
| Sensor 4 | 07 | 04 |

Configuración serial:

```text
9600 baud
8N1
```

---

# Pines Utilizados – Caja Local

## RS485

| Señal | GPIO ESP32 |
|---|---|
| RX RS485 | GPIO16 |
| TX RS485 | GPIO17 |

---

## Radio UART

| Señal | GPIO ESP32 |
|---|---|
| RX Radio | GPIO26 |
| TX Radio | GPIO27 |

---

## Relay de Alarma

| Señal | GPIO ESP32 |
|---|---|
| Control relay | GPIO25 |

---

## Botón RESET Local

| Señal | GPIO ESP32 |
|---|---|
| RESET | GPIO32 |

---

# Pines Utilizados – Caja Remota

## Display OLED I2C

| Señal | GPIO ESP32 |
|---|---|
| SDA | GPIO21 |
| SCL | GPIO22 |

Dirección OLED:

```text
0x3C
```

---

## Radio UART

| Señal | GPIO ESP32 |
|---|---|
| RX Radio | GPIO26 |
| TX Radio | GPIO27 |

---

## RESET

| Señal | GPIO ESP32 |
|---|---|
| RESET | GPIO32 |

---

## Buzzer

| Señal | GPIO ESP32 |
|---|---|
| Buzzer | GPIO33 |

---

## Relay Remoto Opcional

| Señal | GPIO ESP32 |
|---|---|
| Relay remoto | GPIO25 |

---

# Comunicación RS485

La caja local actúa como maestro RS485 y consulta secuencialmente los sensores.

Configuración:

```text
9600 baud
8N1
```

Topología recomendada:

```text
DAISY CHAIN / BUS
```

Ejemplo recomendado:

```text
Caja Local ─── Sensor 1 ─── Sensor 2 ─── Sensor 3 ─── Sensor 4
```

Evitar topología estrella:

```text
             ├── Sensor 1
Caja Local ──┼── Sensor 2
             ├── Sensor 3
             └── Sensor 4
```

La topología estrella puede funcionar en distancias cortas, pero no es recomendable porque puede producir reflexiones, respuestas intermitentes, sensores offline o errores de comunicación.

Recomendaciones:

- Usar cable par trenzado blindado.
- Compartir GND entre controlador y sensores.
- Mantener derivaciones cortas.
- Usar resistencia terminal de 120Ω solo en los extremos del bus.
- No poner terminaciones en cada rama.
- Evitar pasar RS485 junto a cables de potencia o motores.
- Mantener A con A y B con B en todos los sensores.

---

# Comunicación Inalámbrica

Las cajas se comunican mediante módulos seriales UART / LoRa.

La caja local transmite periódicamente:

- Distancia sensor 1.
- Distancia sensor 2.
- Distancia sensor 3.
- Distancia sensor 4.
- Estado de alarma activa.
- Estado de latch.
- Estado offline de cada sensor.

Formato de estado transmitido:

```text
$pata55,pata56,pata57,pata07,alarmaActiva,alarmaLatch,off1,off2,off3,off4
```

Ejemplo:

```text
$3500,4200,1800,5000,1,1,0,0,0,0
```

La caja remota puede enviar comandos:

```text
BORRA
RESET
```

---

# Lógica de Alarma

## Activación

La alarma se activa cuando cualquier sensor detecta:

```text
Distancia < 3000 mm
```

Parámetro:

```cpp
#define ALARM_ON_MM 3000
```

---

## Normalización

La condición vuelve a normal cuando todos los sensores están por encima de:

```text
Distancia > 3100 mm
```

Parámetro:

```cpp
#define ALARM_OFF_MM 3100
```

Esto genera histéresis y evita que la alarma oscile cuando la lectura está cerca del umbral.

---

## Enclavamiento

Cuando ocurre una alarma:

- `alarmaActiva` indica la condición física de peligro.
- `alarmaLatch` mantiene activado el relay.
- El relay puede quedar activo aunque la condición física ya haya desaparecido.
- El operador puede resetear desde la caja remota.
- También puede configurarse un silencio automático por tiempo.

---

# Timeout Automático

El sistema puede apagar automáticamente el relay después de un tiempo configurable.

Parámetro:

```cpp
#define ALARM_AUTO_SILENCE_MS 15000UL
```

Ejemplo:

```text
15000 ms = 15 segundos
```

Dependiendo de la versión del firmware, el tiempo puede contarse:

- Desde que se activa la alarma.
- O desde que la condición se normaliza.

Para corte a los 15 segundos desde la activación, el temporizador debe usar:

```cpp
alarmaStartMs
```

Para corte 15 segundos después de que la condición queda segura, debe usar:

```cpp
alarmaSeguraMs
```

---

# Sensores Offline

Si un sensor deja de responder múltiples consultas RS485, se marca como offline.

Parámetro:

```cpp
#define MISS_LIMIT 6
```

Esto significa que después de 6 consultas sin respuesta, el sensor se muestra como fuera de línea.

Posibles causas:

- Sensor sin alimentación.
- Sensor desconectado.
- A/B RS485 invertidos.
- Falta de GND común.
- Dirección RS485 incorrecta.
- Dos sensores con la misma dirección.
- Cableado en estrella.
- Ruido eléctrico.
- Timeout de lectura muy corto.
- Sensor dañado.

---

# Conexión del Relay 5V con Transistor

El ESP32 no debe manejar directamente la bobina del relay, porque sus GPIO trabajan a 3.3V y no entregan suficiente corriente.

Se utiliza un transistor NPN como 2N2222, BC337 o similar.

## Conexión

```text
GPIO ESP32 ----[1kΩ]---- Base transistor NPN

Emisor transistor -------- GND

Colector transistor ------- Una pata de bobina relay

Otra pata de bobina ------- +5V
```

Diodo flyback en paralelo con bobina:

```text
Cátodo del diodo  → +5V
Ánodo del diodo   → lado del colector/transistor
```

Ejemplo:

```text
                 +5V
                  │
              Bobina relay
                  │
                  ├──── Colector NPN
                  │
ESP32 GPIO --1k-- Base
                  │
              Emisor
                  │
                 GND
```

Muy importante:

```text
GND ESP32 y GND fuente 5V deben estar unidos.
```

Cuando el GPIO está en HIGH, el transistor conduce y activa el relay.

---

# Pantalla OLED

La pantalla utilizada en la caja remota es:

```text
SSD1306 128x64 I2C
```

Configuración:

```cpp
#define OLED_ADDR 0x3C
#define I2C_SDA 21
#define I2C_SCL 22
```

La pantalla usa I2C, no SPI.

---

# Estados Visuales en Caja Remota

## Normal

- Pantalla muestra condición normal.
- Relay remoto apagado.
- Buzzer apagado.

## Peligro Activo

- Pantalla muestra ALERTA.
- Pantalla puede parpadear o invertirse.
- Se muestra la pata/sensor que generó la alarma.
- Buzzer puede sonar.

## Alarma Enclavada

- La condición física puede haber vuelto a normal.
- La alarma queda reconocible hasta reset o timeout.
- Se muestra que la alarma está enclavada.

## Sensor Offline

- Se muestra el sensor que no responde.
- El sistema sigue monitoreando los sensores restantes.

---

# Compilación

Entorno recomendado:

- Arduino IDE
- ESP32 Arduino Core

Librerías necesarias:

- Adafruit GFX
- Adafruit SSD1306
- Wire
- HardwareSerial

---

# Instalación ESP32 en Arduino IDE

Agregar el soporte ESP32 desde el gestor de tarjetas.

URL de placas ESP32:

```text
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

---

# Recomendaciones Industriales

- Usar cajas IP65/IP66.
- Usar borneras industriales.
- Separar alimentación de potencia y señales.
- Usar cable blindado para RS485.
- Conectar GND común.
- Evitar estrella en RS485.
- Usar fusible por caja.
- Usar fuente DC-DC industrial.
- Proteger bobinas con diodos flyback.
- Usar supresores TVS si hay mucho ruido eléctrico.
- Mantener antenas separadas de fuentes de ruido.
- Etiquetar sensores, bornes y cables.

---

# BOM Resumido

## Caja Local

| Componente | Cantidad |
|---|---:|
| ESP32 | 1 |
| Módulo RS485 TTL | 1 |
| Radio UART / LoRa serial | 1 |
| Relay 5V | 1 |
| Transistor NPN | 1 |
| Resistencia 1kΩ | 1 |
| Diodo flyback 1N4007/1N4148 | 1 |
| Sirena / alarma 12V | 1 |
| Sensores láser RS485 | 4 |
| Fuente DC-DC | 1 |
| Caja IP65/IP66 | 1 |
| Borneras / prensaestopas / fusible | Según instalación |

## Caja Remota

| Componente | Cantidad |
|---|---:|
| ESP32 | 1 |
| Pantalla OLED SSD1306 I2C | 1 |
| Radio UART / LoRa serial | 1 |
| Botón RESET | 1 |
| Buzzer | 1 |
| Fuente DC-DC | 1 |
| Caja IP54/IP65 | 1 |
| Borneras / prensaestopas / fusible | Según instalación |

---

# Futuras Mejoras

Posibles mejoras:

- Registro histórico de eventos.
- Telemetría por MQTT.
- Integración con Node-RED.
- Integración con PLC.
- Registro en tarjeta SD.
- Sincronización horaria RTC.
- Dashboard web.
- Reporte de sensores offline.
- Alarmas diferenciadas por nivel de riesgo.
- Diagnóstico remoto de comunicación RS485.

---

# Licencia

Proyecto desarrollado por Alejandro Hugo / b-konnet.
