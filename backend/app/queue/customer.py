from dataclasses import dataclass
from typing import Optional


@dataclass
class Customer:
    track_id: int
    first_seen: float
    last_seen: float

    confidence: float = 0.0
    class_name: str = "person"

    # waiting | serving
    status: str = "waiting"

    queue_joined_at: Optional[float] = None
    service_started_at: Optional[float] = None

    last_queue_seen: Optional[float] = None
    last_service_seen: Optional[float] = None

    def waiting_time(self, now: float) -> float:
        if self.queue_joined_at is None:
            return 0.0

        end = (
            self.service_started_at
            if self.service_started_at is not None
            else now
        )

        return max(0.0, end - self.queue_joined_at)

    def service_time(self, now: float) -> float:
        if self.service_started_at is None:
            return 0.0

        return max(0.0, now - self.service_started_at)
