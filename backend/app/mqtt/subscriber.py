import json

import paho.mqtt.client as mqtt


class MQTTSubscriber:

    def __init__(
        self,
        queue_manager,
        broker="127.0.0.1",
        port=1883,
        topic="deepstream/queue-monitoring",
    ):

        self.queue_manager = (
            queue_manager
        )

        self.broker = broker
        self.port = port
        self.topic = topic

        self.client = mqtt.Client(
            client_id="fastapi_queue_monitor"
        )

        self.client.on_connect = (
            self.on_connect
        )

        self.client.on_message = (
            self.on_message
        )


    def on_connect(
        self,
        client,
        userdata,
        flags,
        rc
    ):

        if rc == 0:

            print(
                "MQTT connected"
            )

            client.subscribe(
                self.topic
            )

            print(
                f"Subscribed: {self.topic}"
            )

        else:

            print(
                f"MQTT connection error: {rc}"
            )


    def on_message(
        self,
        client,
        userdata,
        msg
    ):

        try:

            payload = json.loads(
                msg.payload.decode(
                    "utf-8"
                )
            )

            self.queue_manager.process_frame(
                payload
            )

        except Exception as exc:

            print(
                f"MQTT message error: {exc}"
            )


    def start(self):

        self.client.connect(
            self.broker,
            self.port,
            60
        )

        self.client.loop_start()


    def stop(self):

        self.client.loop_stop()

        self.client.disconnect()
