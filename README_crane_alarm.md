# Crane Alarm – Sistema de Alarma Anticolisión para Grúas

Proyecto desarrollado para detección de obstáculos y prevención de colisiones en grúas mediante sensores láser RS485, comunicación inalámbrica y alarmas locales/remotas.

Repositorio:
https://github.com/bkonnet/crane_alarm

---

# Descripción General

El sistema monitorea continuamente obstáculos mediante sensores láser RS485 instalados en la estructura de la grúa.

Cuando un sensor detecta un obstáculo peligroso:

- Se activa una alarma local de 12V.
- Se transmite el estado a una caja remota.
- La cabina del operador muestra visualmente la condición de peligro.
- El operador puede reconocer/resetear la alarma desde la cabina.

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

# Caja Local

Funciones:

- Lectura de sensores RS485
- Lógica de alarma
- Histéresis
- Activación de relay
- Envío de estado a caja remota
- Gestión de sensores offline

La caja local NO tiene pantalla.

---

# Caja Remota

Funciones:

- Recepción de datos
- Pantalla OLED
- Reset/reconocimiento de alarma
- Buzzer de cabina

---

# Sensores RS485

| Sensor | ID |
|---|---|
| Sensor 1 | 55 |
| Sensor 2 | 56 |
| Sensor 3 | 57 |
| Sensor 4 | 07 |

Configuración serial:

```text
9600 8N1
```

---

# Pines Utilizados – Caja Local

## RS485

| Señal | GPIO |
|---|---|
| RX RS485 | GPIO16 |
| TX RS485 | GPIO17 |

## Radio UART

| Señal | GPIO |
|---|---|
| RX Radio | GPIO26 |
| TX Radio | GPIO27 |

## Relay

| Señal | GPIO |
|---|---|
| Relay alarma | GPIO25 |

## RESET

| Señal | GPIO |
|---|---|
| RESET | GPIO32 |

---

# Pines Utilizados – Caja Remota

## OLED I2C

| Señal | GPIO |
|---|---|
| SDA | GPIO21 |
| SCL | GPIO22 |

Dirección:

```text
0x3C
```

## Radio UART

| Señal | GPIO |
|---|---|
| RX Radio | GPIO26 |
| TX Radio | GPIO27 |

---

# Comunicación RS485

Recomendaciones:

- Cable par trenzado blindado
- GND común
- Daisy chain / bus
- Evitar estrella
- Terminación 120Ω en extremos

---

# Lógica de Alarma

Activación:

```text
Distancia < 3000 mm
```

Normalización:

```text
Distancia > 3100 mm
```

---

# Timeout Automático

```cpp
#define ALARM_AUTO_SILENCE_MS 15000UL
```

---

# Relay con Transistor

```text
GPIO ESP32 -- 1kΩ -- Base transistor
Emisor ---------------- GND
Colector -------------- Bobina relay
Otra pata relay ------- +5V
```

Diodo flyback:

```text
1N4007 en paralelo con la bobina
```

---

# Pantalla OLED

Pantalla utilizada:

```text
SSD1306 128x64 I2C
```

Configuración:

```cpp
#define OLED_ADDR 0x3C
#define I2C_SDA 21
#define I2C_SCL 22
```

---

# Librerías

- Adafruit GFX
- Adafruit SSD1306

---

# Licencia

Proyecto desarrollado por Alejandro Hugo / B-Kontrol.
