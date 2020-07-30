FROM debian:buster as build

RUN apt-get update && \
  apt-get install -y --no-install-recommends \
  build-essential make libelf-dev libunwind-dev

WORKDIR /build
COPY src/*.h /build/src/
COPY src/*.c /build/src/
COPY docs/*.1 /build/docs/
COPY Makefile /build/Makefile

RUN make && make install

FROM gcr.io/distroless/cc-debian10

# Required dynamic libs.
COPY --from=build /lib/x86_64-linux-gnu/libz.so* /lib/x86_64-linux-gnu/
COPY --from=build /lib/x86_64-linux-gnu/liblzma.so* /lib/x86_64-linux-gnu/
COPY --from=build /lib/x86_64-linux-gnu/liblzma.so* /lib/x86_64-linux-gnu/
COPY --from=build /usr/lib/x86_64-linux-gnu/libelf.so* /usr/lib/x86_64-linux-gnu/
COPY --from=build /usr/lib/x86_64-linux-gnu/libunwind* /usr/lib/x86_64-linux-gnu/

COPY --from=build /usr/local/bin/xrprof /usr/local/bin/xrprof

ENTRYPOINT ["/usr/local/bin/xrprof"]
