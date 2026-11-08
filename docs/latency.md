# Latency design

Bluetooth callbacks perform minimal parsing and publish the latest normalized state. The output layer keeps only the newest pending state; it never replays a queue of old inputs. A 1 ms processing timer updates AUTOFIRE even without new Bluetooth reports. Logging and flash writes are excluded from the report hot path.

Internal timestamps can measure callback-to-USB-queue processing. They are not controller-to-game latency. No end-to-end percentile, long-duration stability result, or guaranteed sub-5-ms figure has been measured for this repository.

`tools/measure-autofire.ps1` reports its own Windows polling cadence and may miss transitions if the host poll rate is too low. It is a functional cadence tool, not physical latency instrumentation.
