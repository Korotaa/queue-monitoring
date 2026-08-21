import threading
import time

from .customer import Customer


class QueueManager:

    def __init__(self, leave_timeout=3.0):

        self.customers = {}

        self.leave_timeout = leave_timeout

        self.lock = threading.Lock()


    def process_frame(self, payload: dict):

        now = time.time()

        seen_in_queue = set()

        detections = payload.get(
            "detections",
            []
        )

        with self.lock:

            # ==========================================
            # JOIN / WAIT
            # ==========================================

            for detection in detections:

                track_id = detection.get(
                    "track_id"
                )

                if track_id is None:
                    continue

                if detection.get(
                    "class_name"
                ) != "person":
                    continue

                in_queue = detection.get(
                    "in_queue",
                    False
                )

                if not in_queue:
                    continue


                seen_in_queue.add(
                    track_id
                )


                # Nouveau client
                if track_id not in self.customers:

                    self.customers[track_id] = Customer(
                        track_id=track_id,
                        joined_at=now,
                        last_seen=now,
                        confidence=detection.get(
                            "confidence",
                            0.0
                        ),
                    )

                    print(
                        f"JOIN: customer {track_id}"
                    )

                else:

                    customer = self.customers[
                        track_id
                    ]

                    customer.last_seen = now

                    customer.confidence = (
                        detection.get(
                            "confidence",
                            customer.confidence
                        )
                    )


            # ==========================================
            # LEAVE
            # ==========================================

            to_remove = []

            for track_id, customer in (
                self.customers.items()
            ):

                if track_id in seen_in_queue:
                    continue

                if (
                    now - customer.last_seen
                    >= self.leave_timeout
                ):

                    waiting = (
                        now -
                        customer.joined_at
                    )

                    print(
                        f"LEAVE: customer {track_id} "
                        f"after {waiting:.1f}s"
                    )

                    to_remove.append(
                        track_id
                    )


            for track_id in to_remove:

                del self.customers[
                    track_id
                ]


    def get_stats(self):

        now = time.time()

        with self.lock:

            waiting_times = [

                now - customer.joined_at

                for customer
                in self.customers.values()
            ]


            queue_length = len(
                waiting_times
            )


            if queue_length:

                average_wait = (
                    sum(waiting_times)
                    / queue_length
                )

                longest_wait = max(
                    waiting_times
                )

            else:

                average_wait = 0.0
                longest_wait = 0.0


            # ==========================================
            # Queue status
            # ==========================================

            if queue_length <= 2:

                status = "normal"

            elif queue_length <= 5:

                status = "busy"

            else:

                status = "critical"


            return {

                "queue_length":
                    queue_length,

                "average_waiting_time":
                    round(
                        average_wait,
                        1
                    ),

                "longest_waiting_time":
                    round(
                        longest_wait,
                        1
                    ),

                "status":
                    status,

                "customers": [

                    {
                        "track_id":
                            customer.track_id,

                        "waiting_time":
                            round(
                                now -
                                customer.joined_at,
                                1
                            ),

                        "confidence":
                            round(
                                customer.confidence,
                                3
                            )
                    }

                    for customer
                    in self.customers.values()
                ]
            }
