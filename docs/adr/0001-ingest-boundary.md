# 0001 — SDK-owned MXL and a host-clock cadence boundary

Status: Accepted

## Context

The research document is a conceptual specification, not an MXL ABI definition.
MXL 1.1 supports video/v210 and video/v210a, not video/rgba. Its real directory
layout is `<uuid>.mxl-flow/{data,flow_def.json,grains/...}`. Grain metadata has no
public per-frame creation_timestamp or sync_timestamp fields. CasparCG includes
copyleft code; copying it would conflict with the requested permissive license.

## Decision

Write original MIT C++20 code. Link operator-supplied SDKs only when enabled.
Use libmxl's open/commit/cancel API; never fabricate shared headers or futexes.
Store video/v210a, with BT.709 limited-range 10-bit 4:2:2 fill and SDK-defined
packed key plane. Alpha uses straight video-range 64–940, like luma, with zeroed
unused bits. NDI ingress is explicitly requested as progressive 8-bit RGBA/RGBX;
BGRA/BGRX are also normalized row-by-row, dropping stride padding. Unsupported
fields, dimensions or formats fail explicitly rather than silently corrupting a flow.

The receiver thread samples host TAI immediately after capture and frees each
SDK frame through RAII. It replaces an immutable latest-frame mailbox. The
publisher samples host TAI and publishes at most once per rational epoch index.
Remote timestamps are never used. Multiple arrivals in a tick coalesce; no
`last_index+1` collision workaround can manufacture future timestamps. Fractional
rates use 128-bit integer arithmetic, not rounded frame durations. The spec's
duration equation has an extra factor of 1000: 60000/1001 fps is exactly
`1e9 * 1001 / 60000` ns per frame.

Sync time is implicit in MXL index/rate. Arrival time stays in the local mailbox
for gap policy; no undocumented metadata bytes are written. At a boundary a
frame arriving during a commit is available on the next tick (up to one output
period of cadence delay). This is an asynchronous boundary, not genlock.

## Alternatives

* Private RGBA shared-memory ABI: lossless RGB, but not interoperable with MXL 1.1.
* Fork MXL to add RGBA: possible future work, not a stock SDK integration.
* GStreamer: useful conversion ecosystem but unnecessary dependency/licensing burden.
* Immediate per-arrival commits: causes burst collisions and cannot produce a
  stable cadence for VFR or disconnected sources.

## Consequences and limits

* v210a converts RGB to subsampled YCbCr; RGB is not bit-exact. 8-bit ingress
  alpha is retained in a 10-bit range; **16-bit NDI alpha is not supported**.
  No UYVA/16-bit conversion, HDR or color-management negotiation is claimed.
  Source graphics must use the agreed BT.709 SDR/straight-alpha convention.
* Conversion writes directly into the SDK grain, but capture normalization and
  the retained last frame require memory copies: this is not end-to-end zero-copy.
* Non-real-time scheduling can miss deadlines. Skipped ticks are logged and not
  backfilled in a burst. A backward clock step pauses indices until time catches
  up; a forward step skips to the actual grid. Run on a disciplined host.
* SDK connection state triggers transparent output; optionally a stale-frame
  timeout catches silent sender failures. A timeout cannot distinguish static
  graphics from a stuck sender, so it is disabled by default.
* MXL domain history controls ring depth; no private per-flow depth option is
  invented. Existing descriptors must match exactly; writer startup is fail-fast.
* Service hardening preserves access to shared tmpfs. Operators own users,
  permissions, clock discipline, network discovery and proprietary SDK licenses.
