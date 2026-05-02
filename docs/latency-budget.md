# Latency Budget

Initial target: 1080p60, 15-30 Mbps, LAN.

| Stage | Target |
| --- | ---: |
| Capture | 1-4 ms |
| Color conversion | 0-2 ms |
| Encode | 3-8 ms |
| Packetization/send | <1 ms |
| LAN transit | <2 ms |
| Reassembly | <1 ms |
| Decode | 3-8 ms |
| Render/present | 1-8 ms |

End-to-end MVP target is below one frame queue of avoidable latency. The system should drop stale video rather than allow queues to grow.

Telemetry timestamps:

- host capture start/done
- host encode start/done
- packet send
- client receive
- frame reassembled
- decode done
- present done
- RTT ping/pong

