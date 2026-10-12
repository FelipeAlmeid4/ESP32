#!/usr/bin/env python3
"""
Simula a "segunda placa": assina o topico de alarme e mostra um LED virtual.
Tambem exibe temperatura, umidade e status publicados pelo ESP32.

Uso:
    pip install paho-mqtt
    python alarm_subscriber.py --broker broker.hivemq.com --base franzininho/lab01
"""
import argparse
import json

import paho.mqtt.client as mqtt

RED = "\033[41m\033[97m"
GRAY = "\033[100m\033[97m"
RESET = "\033[0m"


def led(on: bool) -> str:
    return f"{RED} LED ACESO (ALARME) {RESET}" if on else f"{GRAY} LED apagado {RESET}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--broker", default="broker.hivemq.com")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--base", default="franzininho/lab01", help="prefixo dos topicos")
    ap.add_argument("--user")
    ap.add_argument("--password")
    args = ap.parse_args()

    topic_alarm = f"{args.base}/alarme"

    def on_connect(client, userdata, flags, reason_code, properties=None):
        print(f"Conectado a {args.broker}:{args.port}, assinando {args.base}/#")
        client.subscribe(f"{args.base}/#", qos=1)

    def on_message(client, userdata, msg):
        payload = msg.payload.decode(errors="replace")
        if msg.topic == topic_alarm:
            try:
                on = json.loads(payload).get("estado", "").upper() == "ON"
            except (ValueError, AttributeError):
                on = payload.strip().upper() in ("ON", "1")
            print(f"[{msg.topic}] {payload}\n    -> {led(on)}")
        else:
            print(f"[{msg.topic}] {payload}")

    try:  # paho-mqtt >= 2.0
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except AttributeError:  # paho-mqtt 1.x
        client = mqtt.Client()
    if args.user:
        client.username_pw_set(args.user, args.password)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.broker, args.port, keepalive=60)
    client.loop_forever()


if __name__ == "__main__":
    main()
