ARG BUILD_FROM=ghcr.io/seb5594/home-assistant-base:3.24-2026.11.0@sha256:d116b9e5b612e6fcd997c86642d21b61953fcbe390c39c9b03443a577ed5e2a6

# The compiler only exists while the proxy is built and is removed again.
# renovate: datasource=repology depName=alpine_3_24/gcc
ARG GCC_VERSION=15.2.0-r5
# renovate: datasource=repology depName=alpine_3_22/gcc
ARG GCC_VERSION_LEGACY=14.2.0-r6
# renovate: datasource=repology depName=alpine_3_24/musl
ARG MUSL_DEV_VERSION=1.2.6-r3
# renovate: datasource=repology depName=alpine_3_22/musl
ARG MUSL_DEV_VERSION_LEGACY=1.2.5-r13

FROM ${BUILD_FROM}

ARG GCC_VERSION
ARG MUSL_DEV_VERSION
COPY proxy.c /usr/src/proxy.c
RUN apk add --no-cache --virtual .build-deps gcc=${GCC_VERSION} musl-dev=${MUSL_DEV_VERSION} \
    && gcc -Os -Wall -Wextra -static -s -o /usr/local/bin/docker-socket-proxy /usr/src/proxy.c \
    && apk del .build-deps \
    && rm -f /usr/src/proxy.c

ARG BUILD_VERSION=2026.11.21
ARG BUILD_ARCH=amd64
LABEL io.hass.version="${BUILD_VERSION}" \
    io.hass.type="addon" \
    io.hass.arch="${BUILD_ARCH}" \
    org.opencontainers.image.source="https://github.com/seb5594/Home-Assistant-Docker-Socket-Proxy-App"

ENTRYPOINT [ "/usr/local/bin/docker-socket-proxy" ]
