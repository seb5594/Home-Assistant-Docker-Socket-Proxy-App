ARG BUILD_FROM=ghcr.io/seb5594/home-assistant-base:3.24
ARG BUILD_VERSION=1.0.0

FROM ${BUILD_FROM}

COPY proxy.c /usr/src/proxy.c
RUN apk add --no-cache --virtual .build-deps gcc musl-dev \
    && gcc -Os -Wall -Wextra -static -s -o /usr/local/bin/docker-socket-proxy /usr/src/proxy.c \
    && apk del .build-deps \
    && rm -f /usr/src/proxy.c

ARG BUILD_VERSION
LABEL io.hass.version="${BUILD_VERSION}" \
    io.hass.type="addon" \
    org.opencontainers.image.source="https://github.com/seb5594/Home-Assistant-Docker-Socket-Proxy-App"

ENTRYPOINT [ "/usr/local/bin/docker-socket-proxy" ]
