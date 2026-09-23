from collections import deque
import threading
import time

from .customer import Customer


QUEUE_ROI = "QUEUE"
SERVICE_ROI = "SERVICE"


class QueueManager:
    """
    Queue/service state machine.

    Typical lifecycle:
        detected in QUEUE
            -> waiting
        detected in SERVICE
            -> serving
        disappears from SERVICE for service_leave_timeout
            -> completed

    If a waiting customer disappears from QUEUE without entering SERVICE,
    the customer is counted as abandoned after leave_timeout.
    """

    def __init__(
        self,
        leave_timeout: float = 3.0,
        service_leave_timeout: float | None = None,
        target_wait_seconds: float = 120.0,
        history_size: int = 5000,
    ):
        self.leave_timeout = float(leave_timeout)

        self.service_leave_timeout = float(
            service_leave_timeout
            if service_leave_timeout is not None
            else leave_timeout
        )

        self.target_wait_seconds = float(
            target_wait_seconds
        )

        self.customers: dict[int, Customer] = {}

        self.completed_history = deque(
            maxlen=history_size
        )

        self.abandoned_history = deque(
            maxlen=history_size
        )

        self.lock = threading.RLock()


    def process_frame(self, payload: dict):
        now = time.time()

        detections = payload.get(
            "detections",
            []
        )

        with self.lock:

            for detection in detections:

                track_id = detection.get(
                    "track_id"
                )

                if track_id is None:
                    continue

                class_id = detection.get(
                    "class_id"
                )

                class_name = detection.get(
                    "class_name",
                    ""
                )

                # Person only.
                if (
                    class_id is not None
                    and class_id != 0
                ):
                    continue

                if (
                    class_id is None
                    and class_name != "person"
                ):
                    continue

                confidence = float(
                    detection.get(
                        "confidence",
                        0.0
                    ) or 0.0
                )

                roi = detection.get(
                    "roi",
                    []
                ) or []

                in_queue = (
                    QUEUE_ROI in roi
                    or bool(
                        detection.get(
                            "in_queue",
                            False
                        )
                    )
                )

                in_service = (
                    SERVICE_ROI in roi
                )

                customer = self.customers.get(
                    track_id
                )

                # --------------------------------------------------
                # SERVICE has priority if zones touch or overlap.
                # --------------------------------------------------
                if in_service:

                    if customer is None:

                        customer = Customer(
                            track_id=track_id,
                            first_seen=now,
                            last_seen=now,
                            confidence=confidence,
                            status="serving",
                            service_started_at=now,
                            last_service_seen=now,
                        )

                        self.customers[
                            track_id
                        ] = customer

                        print(
                            f"SERVICE DIRECT: customer {track_id}"
                        )

                    else:

                        customer.last_seen = now
                        customer.last_service_seen = now
                        customer.confidence = confidence

                        if (
                            customer.status
                            != "serving"
                        ):

                            customer.status = "serving"

                            if (
                                customer.service_started_at
                                is None
                            ):
                                customer.service_started_at = now

                            print(
                                f"SERVICE: customer {track_id}"
                            )

                    continue


                # --------------------------------------------------
                # QUEUE
                # --------------------------------------------------
                if in_queue:

                    if customer is None:

                        customer = Customer(
                            track_id=track_id,
                            first_seen=now,
                            last_seen=now,
                            confidence=confidence,
                            status="waiting",
                            queue_joined_at=now,
                            last_queue_seen=now,
                        )

                        self.customers[
                            track_id
                        ] = customer

                        print(
                            f"JOIN: customer {track_id}"
                        )

                    else:

                        customer.last_seen = now
                        customer.confidence = confidence

                        # Do not move a serving customer backwards.
                        if (
                            customer.status
                            == "waiting"
                        ):

                            customer.last_queue_seen = now

                    continue


                # --------------------------------------------------
                # Person visible, but outside QUEUE and SERVICE.
                # Keep last_seen updated; timeout logic decides
                # whether this is abandonment/completion.
                # --------------------------------------------------
                if customer is not None:
                    customer.last_seen = now
                    customer.confidence = confidence


            # Cleanup is also called by get_stats(), so customers
            # can expire even when MQTT temporarily sends no frames.
            self._cleanup_locked(now)


    def _cleanup_locked(
        self,
        now: float
    ):

        to_remove = []

        for (
            track_id,
            customer
        ) in self.customers.items():

            # ----------------------------------------------
            # WAITING -> abandoned
            # ----------------------------------------------
            if customer.status == "waiting":

                last_queue = (
                    customer.last_queue_seen
                    if customer.last_queue_seen
                    is not None
                    else customer.last_seen
                )

                if (
                    now - last_queue
                    >= self.leave_timeout
                ):

                    wait_time = (
                        customer.waiting_time(
                            now
                        )
                    )

                    self.abandoned_history.append(
                        {
                            "track_id": track_id,
                            "timestamp": now,
                            "waiting_time": wait_time,
                        }
                    )

                    print(
                        f"ABANDONED: customer {track_id} "
                        f"after {wait_time:.1f}s"
                    )

                    to_remove.append(
                        track_id
                    )


            # ----------------------------------------------
            # SERVING -> completed
            # ----------------------------------------------
            elif customer.status == "serving":

                last_service = (
                    customer.last_service_seen
                    if customer.last_service_seen
                    is not None
                    else customer.last_seen
                )

                if (
                    now - last_service
                    >= self.service_leave_timeout
                ):

                    waiting_time = (
                        customer.waiting_time(
                            now
                        )
                    )

                    service_time = (
                        customer.service_time(
                            last_service
                        )
                    )

                    self.completed_history.append(
                        {
                            "track_id": track_id,
                            "timestamp": now,
                            "waiting_time": waiting_time,
                            "service_time": service_time,
                        }
                    )

                    print(
                        f"SERVED: customer {track_id} | "
                        f"wait={waiting_time:.1f}s | "
                        f"service={service_time:.1f}s"
                    )

                    to_remove.append(
                        track_id
                    )


        for track_id in to_remove:
            self.customers.pop(
                track_id,
                None
            )


    def get_stats(self) -> dict:
        now = time.time()

        with self.lock:

            self._cleanup_locked(
                now
            )

            waiting = [
                c
                for c
                in self.customers.values()
                if c.status == "waiting"
            ]

            serving = [
                c
                for c
                in self.customers.values()
                if c.status == "serving"
            ]

            waiting_times = [
                c.waiting_time(now)
                for c
                in waiting
            ]

            completed = list(
                self.completed_history
            )

            abandoned = list(
                self.abandoned_history
            )

            service_times = [
                item["service_time"]
                for item
                in completed
            ]

            average_waiting = (
                sum(waiting_times)
                / len(waiting_times)
                if waiting_times
                else 0.0
            )

            longest_waiting = (
                max(waiting_times)
                if waiting_times
                else 0.0
            )

            average_service = (
                sum(service_times)
                / len(service_times)
                if service_times
                else 0.0
            )

            one_hour_ago = (
                now - 3600.0
            )

            served_last_hour = [
                item
                for item
                in completed
                if item["timestamp"]
                >= one_hour_ago
            ]

            abandoned_last_hour = [
                item
                for item
                in abandoned
                if item["timestamp"]
                >= one_hour_ago
            ]

            # Operational completion efficiency:
            # served / (served + abandoned)
            finished_last_hour = (
                len(served_last_hour)
                + len(abandoned_last_hour)
            )

            service_efficiency = (
                (
                    len(served_last_hour)
                    / finished_last_hour
                )
                * 100.0
                if finished_last_hour
                else 100.0
            )

            waits_with_service = [
                item["waiting_time"]
                for item
                in served_last_hour
                if item["waiting_time"]
                > 0
            ]

            service_level = (
                (
                    sum(
                        wait
                        <= self.target_wait_seconds
                        for wait
                        in waits_with_service
                    )
                    / len(
                        waits_with_service
                    )
                )
                * 100.0
                if waits_with_service
                else 100.0
            )

            queue_length = len(
                waiting
            )

            if queue_length <= 2:
                status = "normal"
            elif queue_length <= 5:
                status = "busy"
            else:
                status = "critical"


            active_customers = []

            for customer in sorted(
                self.customers.values(),
                key=lambda c: c.first_seen
            ):

                active_customers.append(
                    {
                        "track_id":
                            customer.track_id,

                        "status":
                            customer.status,

                        "waiting_time":
                            round(
                                customer.waiting_time(
                                    now
                                ),
                                1
                            ),

                        "service_time":
                            round(
                                customer.service_time(
                                    now
                                ),
                                1
                            ),

                        "confidence":
                            round(
                                customer.confidence,
                                3
                            ),
                    }
                )


            return {
                "queue_length":
                    queue_length,

                "customers_in_service":
                    len(serving),

                "average_waiting_time":
                    round(
                        average_waiting,
                        1
                    ),

                "longest_waiting_time":
                    round(
                        longest_waiting,
                        1
                    ),

                "average_service_time":
                    round(
                        average_service,
                        1
                    ),

                "customers_served":
                    len(completed),

                "customers_served_last_hour":
                    len(served_last_hour),

                "abandoned_customers":
                    len(abandoned),

                "service_efficiency":
                    round(
                        service_efficiency,
                        1
                    ),

                "service_level":
                    round(
                        service_level,
                        1
                    ),

                "target_wait_seconds":
                    self.target_wait_seconds,

                "status":
                    status,

                "customers":
                    active_customers,
            }
