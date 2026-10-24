# Verification

## Implementation verification (2026-10-24)

* Apple Clang 17: Debug ASan/UBSan and Release builds; four CTest cases passed.
* NDI adapter: strict-warning syntax check against locally available real SDK headers.
* MXL adapter: strict-warning syntax check against upstream v1.1.0 headers
  (commit `8f3f1096e804bfb822eb959cdf2fc23199a7d15d`).
* Go CI driver: `go test ./...` passed (compile check; no Go test cases).
* Dagger execution attempted, but the engine connection failed with HTTP 502 /
  `unexpected EOF` while evaluating the pipeline. Linux CI results are unverified;
  rerun `cd ci && go run .` with a healthy engine.
* Live SDK linking, NDI receive/MXL reader interoperability, systemd/container
  deployment and production throughput are not yet verified.

## Coverage and acceptance

Automated core coverage: fractional-rate grid boundaries at contemporary epoch
sizes; malformed configuration/numbers; padded BGRA and opaque BGRX; truncated
frames; v210a fill/key values and trailing key padding at several widths;
VFR hold/stale/disconnect/backward-time policies; concurrent latest-frame mailbox;
CLI simulation/config loading and invalid arguments. Assertions are active in
Release builds. ASan/UBSan are available on both supported Clang targets.

SDK compilation is separate from a real receive/publish acceptance test. Before
production deployment:

1. Build with both real SDK adapters, confirm runtime MXL 1.1 and loader paths.
2. Verify kernel TAI offset and chrony/PTP synchronization independently.
3. Provision domain tmpfs/history; use an MXL reader to confirm eight slots,
   video/v210a geometry, epoch indices and valid complete grain commits.
4. Send a known RGBA alpha ramp/checkerboard at the configured progressive raster.
   Decode the MXL key plane and verify transparent=64, opaque=940 and monotonic ramp.
5. Test padded raster, source cadence different from output, burst arrivals, VFR
   static graphics, stale timeout enabled/disabled and hold/transparent policies.
6. Stop/reboot the source: verify cadence continues transparently after SDK
   disconnection and graphics return after rediscovery. Verify discovery server
   and extra-IP behavior on the actual installed SDK and routed network.
7. SIGTERM the worker and restart with the same UUID. Check downstream recovery,
   incompatible descriptor rejection, service permissions and no other flow removal.
8. Measure full-resolution conversion throughput, missed ticks, CPU and memory
   under load on the production host. No real-time performance guarantee is assumed.
9. Repeat through a container and systemd with an independent shared-domain reader.

Live NDI/MXL interoperability, alpha convention against specific senders and
production performance require these checks; dependency-free simulation does not
establish them.
