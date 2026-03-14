## Conexiones del semáforo

### Diagrama de Conexiones (Hardware)

Para el montaje use un NodeMCU (ESP8266), un módulo de pantalla TM1637 (4 dígitos) y un receptor infrarrojo IR1838.

| Componente | Pin del Componente | Pin NodeMCU | Notas / Función |
| :--- | :--- | :--- | :--- |
| **TM1637 (Pantalla)** | VCC | **VIN** | con 3v funciona pero uso los 5V directos del USB para asegurar el brillo de los LEDs. |
| | GND | **GND** | Tierra común. |
| | CLK | **D2** (GPIO 4) | Señal de Reloj. |
| | DIO | **D1** (GPIO 5) | Señal de Datos. |

<br><hr><br>

| Componente | Pin del Componente | Pin NodeMCU | Notas / Función |
| :--- | :--- | :--- | :--- |
| **IR1838 (Receptor)** | VCC (Derecha)* | **3V3** | Alimentación lógica (3.3V). |
| | GND (Centro)* | **GND** | Tierra común. |
| | OUT (Izquierda)* | **D5** (GPIO 14) | Envía los pulsos IR al procesador. |

> **\*Nota sobre el IR1838:** La orientación de los pines se asume mirando el componente de frente (con la "burbuja" del sensor apuntando hacia ti).
