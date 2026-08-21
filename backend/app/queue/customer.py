from dataclasses import dataclass
from time import time


@dataclass
class Customer:
    track_id: int
    joined_at: float
    last_seen: float

    class_name: str = "person"
    confidence: float = 0.0
    status: str = "waiting"

    def waiting_time(self) -> float:
        return max(0.0, time() - self.joined_at)
