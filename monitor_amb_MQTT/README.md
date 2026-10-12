# Monitor Ambiental com MQTT — Franzininho WiFi LAB01

Lê temperatura e umidade de um DHT11 a cada 60 s, publica os valores num broker MQTT e,
quando algum limite é ultrapassado, publica uma mensagem no tópico de alarme. Um assinante
desse tópico (outra placa, a própria placa ou o script Python) acende um LED.

## Arquitetura

```
 ┌──────────────── ESP32-S2 (sensor) ────────────────┐
 │ DHT11 (GPIO15) ──► sensor_task (60 s)             │
 │                     ├─► <base>/temperatura  "25.0" │
 │                     ├─► <base>/umidade      "61.0" │
 │                     └─► <base>/alarme  JSON (retido, só na mudança de estado)
 └────────────────────────────┬───────────────────────┘
                              ▼
                        Broker MQTT
                              │  assina <base>/alarme
          ┌───────────────────┼─────────────────────┐
          ▼                   ▼                     ▼
  2ª placa (papel ALARME)  mesma placa (AMBOS)  tools/alarm_subscriber.py
  LED GPIO14 aceso         LED GPIO14 aceso     LED virtual no terminal
```

| Tópico | Conteúdo | QoS / retain |
|---|---|---|
| `<base>/temperatura` | `"25.0"` (°C) | 0 / não |
| `<base>/umidade` | `"61.0"` (%) | 0 / não |
| `<base>/alarme` | `{"estado":"ON","motivo":"temperatura","temperatura":31.0,"umidade":55.0,"limite_temperatura":30,"limite_umidade":70}` | 1 / sim |
| `<base>/status` | `online` / `offline` (Last Will) | 1 / sim |

`<base>` é `franzininho/lab01` por padrão. Em broker público, troque por algo único
(ex.: `seu_nome/lab01`).

O alarme usa histerese: liga quando o valor passa do limite e só desliga quando cai abaixo
de `limite - histerese` (padrão 1 °C / 1 %), evitando liga/desliga na fronteira.

## Pinos (Franzininho WiFi LAB01)

| Função | GPIO |
|---|---|
| DHT11 | 15 |
| LED de alarme (vermelho do RGB) | 14 |

Ambos podem ser alterados no `menuconfig`.

## Compilar e gravar (ESP-IDF 5.x)

```bash
idf.py set-target esp32s2
idf.py menuconfig      # "Monitor Ambiental MQTT": Wi-Fi, broker, limites, função da placa
idf.py build
idf.py -p COMx flash monitor
```

A Franzininho WiFi grava pela USB nativa. Se não entrar em modo de gravação, segure **BOOT**,
pressione **RESET** e solte **BOOT**. Após gravar, pressione **RESET**.

### Função da placa (menuconfig → Função desta placa)

- **Sensor + assinante de alarme** (padrão): uma única placa publica os dados e assina o próprio
  tópico de alarme. O LED acende pela mensagem vinda do broker, então o caminho MQTT completo
  é exercitado mesmo sem uma segunda placa.
- **Somente sensor**: só publica.
- **Somente assinante de alarme**: para a segunda placa; só assina `<base>/alarme` e controla o LED.

## Simular o assinante no PC

```bash
pip install paho-mqtt
python tools/alarm_subscriber.py --broker broker.hivemq.com --base franzininho/lab01
```

Também dá para usar o **MQTT Explorer** ou um app de celular (IoT MQTT Panel, MyMQTT):
assine `<base>/#`. Para testar o LED sem esquentar o sensor, publique manualmente `ON` ou
`OFF` em `<base>/alarme`.

## Testando o alarme

- Reduza temporariamente os limites no `menuconfig` (ex.: temperatura 20 °C) e o
  `Intervalo de leitura` para 5 s; ou
- segure o DHT11 entre os dedos / sopre nele para subir temperatura e umidade.
