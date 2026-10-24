# NDI → MXL graphics ingest

Headless C++20 worker, MIT licensed. Linux/systemd is the deployment target;
Apple Clang/macOS is supported for development. No GStreamer, FFmpeg or C++
package manager dependencies. Production requires externally installed **NDI
5/6 SDK** (proprietary) and **libmxl 1.1.x** (Apache-2.0).

## Build and test

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DINGEST_SANITIZE=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/ndi-mxl-ingest --simulate --frames 10
```

Default builds exercise the dependency-free core; **they cannot ingest or publish
MXL**. Simulation runs transparent raster conversion, without clock/network/IPC,
and is not an end-to-end test.

Production (install SDKs first; no SDK is fetched by CMake):

```sh
cmake -S . -B build-release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
  -DINGEST_WITH_NDI=ON -DINGEST_WITH_MXL=ON -DNDI_ROOT=/opt/ndi \
  -DCMAKE_PREFIX_PATH=/opt/mxl
cmake --build build-release -j
sudo cmake --install build-release
```

Configure the platform dynamic loader to find `libndi` and `libmxl` and their
dependencies (`ldconfig` on Linux; suitable install names/rpaths on macOS).
macOS live development needs an explicit `tai_offset` and a suitable MXL domain;
there is no silent UTC fallback. Linux uses `CLOCK_TAI` unless explicitly overridden.
The operator must configure chrony/PTP and verify the kernel TAI offset and clock
discipline before running; this worker does not discipline or certify the clock.

## Configuration and systemd

Copy `config/ingest.conf` to `/etc/ndi-mxl-ingest/ingest.conf`; values are unquoted,
plain `key=value`. Unknown/duplicate keys and invalid dimensions/rates are errors.
Use an exact NDI source name, stable unique UUID per worker, and a matching
progressive raster. Source rate may differ from the configured output rate.

* `extra_ips`: comma-separated discovery IPs passed to NDI Find.
* `discovery_server`: sets `NDI_DISCOVERY_SERVER` before SDK initialization.
  Verify discovery against the installed SDK/network; firewall/mDNS/Avahi setup
  remains an operator responsibility.
* `gap_policy=hold`: repeat the last graphic while connected, including VFR/static
  graphics. `transparent`: clear after one output period without a new frame.
* `disconnect_ms=0`: trust NDI connection state. A positive value also clears
  stale frames after that timeout, even while connected.
* Before the first frame and after NDI reports disconnection, output is transparent.

Create system user `ndi-ingest` and shared group `mxl`; add downstream readers to
that group. Install `deploy/ndi-mxl-ingest.tmpfiles.conf` into `/etc/tmpfiles.d/`
and run `systemd-tmpfiles --create`. Install the service into `/etc/systemd/system/`.
Do not enable a private `/dev/shm` mount for this service.

MXL controls ring depth using **domain** `options.json`. For eight slots at
60000/1001 fps put this in `/dev/shm/mxl/options.json` (recreate after reboot):

```json
{"urn:x-mxl:option:history_duration/v1.0": 133466667}
```

For other rates use `ceil(slots * 1e9 * denominator / numerator)`. This is
domain-wide: coordinate with other workers, or give this worker its own domain.
The adapter checks the resulting ring count. A 1080p eight-slot v210a flow needs
about 67 MB plus metadata; provision tmpfs capacity for all flows.

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now ndi-mxl-ingest
journalctl -u ndi-mxl-ingest -f
```

SIGTERM/SIGINT stop capture and release SDK objects. The worker does not delete
flows or garbage-collect other applications' resources. Incompatible existing
descriptors are rejected; use a new UUID or MXL tooling for inactive-flow cleanup.

## Containers

Build/install on Linux with `--prefix "$PWD/stage"`, then `docker build -t ndi-mxl-ingest .`.
The image intentionally contains no proprietary SDK or MXL binaries: supply
compatible libraries/dependencies in a private derived image or read-only mounts,
with `LD_LIBRARY_PATH` as needed. Observe NDI redistribution terms.
Run with host networking on Linux for discovery, a bind-mounted host MXL domain,
a read-only configuration mount and a UID/GID permitted to write that domain.
The consumer must mount **the same domain**; private container `/dev/shm` is not
sufficient. Do not grant clock-setting capabilities; containers share host time.

## CI and scope

`cd ci && go run .` runs Linux Clang build/tests with ASan/UBSan through Dagger's
Go SDK. Requires Go 1.24+ and a working container engine. SDK adapters are opt-in;
the public CI does not download proprietary NDI assets.

See [architecture decisions](docs/adr/0001-ingest-boundary.md) for corrections to
the research specification, timing/alpha semantics and current limitations, and
[verification](docs/verification.md) for deployment acceptance checks.
