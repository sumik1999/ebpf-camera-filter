#!/usr/bin/env python3
import unittest

from rapid_adaptive_controller import Actuator, Counter, Port, RapidController, Switch


def topology():
    return [
        Switch("s1", "10.0.1.0/24", [
            Port("trusted", "trusted0", "edge", trusted=True),
            Port("attacker", "attack0", "edge"),
            Port("uplink", "up0", "transit", peer_switch="s2", peer_port="uplink"),
        ]),
        Switch("s2", "10.0.2.0/24", [
            Port("uplink", "up1", "transit", peer_switch="s1", peer_port="uplink"),
        ]),
    ]


class FakeBackend:
    def __init__(self, values):
        self.values = values

    def read(self, port):
        return self.values[port.key]


class RapidControllerTests(unittest.TestCase):
    def test_balance_is_signed_thesis_formula(self):
        self.assertAlmostEqual(RapidController.balance(300, 100), 0.5)
        self.assertAlmostEqual(RapidController.balance(100, 300), -0.5)
        self.assertEqual(RapidController.balance(0, 0), 0)

    def test_edge_is_blocked_after_confirmation(self):
        actuator = Actuator(dry_run=True)
        controller = RapidController(topology(), actuator=actuator,
                                     minimum_packets=10, confirmation_windows=2)
        deltas = {
            "root:trusted0": Counter(50, 50),
            "root:attack0": Counter(1000, 0),
            "root:up0": Counter(0, 1000),
            "root:up1": Counter(1000, 0),
        }
        d1 = {"s1": 0.0, "s2": 0.0}
        d2 = {key: controller.balance(value.rx, value.tx) for key, value in deltas.items()}
        first = controller.evaluate(deltas, d1, d2)
        self.assertFalse(any(event["action"] == "block" for event in first))
        events = controller.evaluate(deltas, d1, d2)
        attack = next(event for event in events if event["port"] == "attacker")
        self.assertEqual(attack["action"], "block")
        self.assertIn("root:attack0", actuator.blocked)

    def test_trusted_edge_requires_administrator_action(self):
        controller = RapidController(topology(), actuator=Actuator(dry_run=True),
                                     minimum_packets=1, confirmation_windows=1)
        deltas = {
            "root:trusted0": Counter(100, 0),
            "root:attack0": Counter(0, 0),
            "root:up0": Counter(0, 0),
            "root:up1": Counter(0, 0),
        }
        d1 = {"s1": 0.0, "s2": 0.0}
        d2 = {key: controller.balance(value.rx, value.tx) for key, value in deltas.items()}
        events = controller.evaluate(deltas, d1, d2)
        self.assertEqual(events[0]["action"], "administrator_action")

    def test_sample_computes_link_balance_from_peer(self):
        ports = topology()
        values = {
            "root:trusted0": Counter(0, 0),
            "root:attack0": Counter(0, 0),
            "root:up0": Counter(100, 0),
            "root:up1": Counter(0, 100),
        }
        backend = FakeBackend(values)
        controller = RapidController(ports, backend=backend, actuator=Actuator(dry_run=True))
        controller.prime()
        values["root:up0"] = Counter(400, 0)   # local RX delta 300
        values["root:up1"] = Counter(0, 200)   # peer TX delta 100
        _deltas, delta1, _delta2 = controller.sample()
        self.assertAlmostEqual(delta1["s1"], 0.5)

    def test_invalid_transit_peer_is_rejected(self):
        bad = [Switch("s1", "10.0.0.0/24", [
            Port("up", "up0", "transit", peer_switch="missing", peer_port="up")
        ])]
        with self.assertRaises(ValueError):
            RapidController(bad)


if __name__ == "__main__":
    unittest.main()
