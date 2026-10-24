# Build the production executable on Linux first (both SDK adapters enabled).
# SDK binaries are supplied by the operator, never downloaded or redistributed here.
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 libavahi-client3 ca-certificates && rm -rf /var/lib/apt/lists/*
COPY stage/bin/ndi-mxl-ingest /usr/local/bin/ndi-mxl-ingest
USER 65532:65532
ENTRYPOINT ["/usr/local/bin/ndi-mxl-ingest"]
CMD ["--config", "/etc/ndi-mxl-ingest/ingest.conf"]
