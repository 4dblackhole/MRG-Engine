# Performance statistics

`mrg::Run` measures completed render frames and `IGameClient::Update` calls
continuously.  It publishes a new one-second sample through
`mrg::UpdateContext::performance`.

`PerformanceStatistics::hasMeasurement` remains false until the first full
interval is complete.  `measurementIndex` increases once for each published
sample, so a Client can update its strings only when a new sample arrives.

The Engine does not bind F1, load fonts, or draw an FPS/UPS overlay.  Those are
presentation decisions made by the Client.  This keeps the engine usable by
games with different debug UIs, telemetry, or no visible counter at all.

`SceneGameClient` retains final scene-lifecycle methods and offers four safe
template hooks for root-client behavior:

- `OnClientInitialized` for Client-wide resource creation;
- `OnClientUpdated` for input and performance data;
- `OnClientRendered` after the active Scene has submitted its rendering;
- `OnClientShuttingDown` for release while Engine services are alive.
