FROM ubuntu:noble

ARG build_type=build_release

RUN apt update && apt install -y ca-certificates
RUN sed -i 's/archive.ubuntu.com/mirror.yandex.ru/g' /etc/apt/sources.list

RUN apt-get update -o Acquire::AllowInsecureRepositories=true && apt-get install -y --no-install-recommends --allow-unauthenticated \
    git \
    libssl-dev \
    libldap-common \
    openssl \
    libpam0g-dev \
    libldap-dev \
    build-essential \
    cmake \
    postgresql-server-dev-all

RUN mkdir /odyssey
WORKDIR /odyssey

COPY . ./

RUN make clean
RUN make ${build_type}

WORKDIR /odyssey/build/test

ENTRYPOINT [ "/odyssey/build/test/odyssey_test" ]
