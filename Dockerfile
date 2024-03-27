FROM alpine:latest as build-env
LABEL description="Build container - TgBot"

ARG VCPKG_GIT_PATH=https://github.com/Microsoft/vcpkg.git

RUN apk update && \
    apk add --no-cache \
        wget ca-certificates git ninja build-base curl zip unzip tar cmake linux-headers pkgconfig perl bash

ENV VCPKG_FEATURE_FLAGS=manifests
ENV VCPKG_FORCE_SYSTEM_BINARIES=1

RUN cd /tmp && \
    git config --global core.compression 0 && \
    git clone --depth 1 --branch 2024.02.14 ${VCPKG_GIT_PATH} && \
    cd vcpkg && \
    ./bootstrap-vcpkg.sh

WORKDIR /build
COPY vcpkg-configuration.json   .
COPY vcpkg.json                 .
RUN /tmp/vcpkg/vcpkg install

COPY src        ./src
COPY include    ./include
COPY CMakeLists.txt .

RUN mkdir out && \
    cd out && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_BUILD_PARALLEL_LEVEL=$(nproc) -DCMAKE_TOOLCHAIN_FILE=/tmp/vcpkg/scripts/buildsystems/vcpkg.cmake && \
    make -j$(nproc)

COPY config ./out/config

FROM alpine:latest as runtime
LABEL description="Run container - TgBot"

RUN apk update && \
    apk add --no-cache \
    libstdc++

RUN addgroup -S bot && adduser -S bot -G bot
USER bot

WORKDIR /app
COPY --chown=bot:bot --from=build-env /build/out    .

CMD ./tgbot


