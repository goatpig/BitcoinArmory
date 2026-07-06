FROM ubuntu:24.04 as armorydb_build_machine

ENV TZ="Etc/UTC"

RUN sed -Ei 's/^# deb-src /deb-src /' /etc/apt/sources.list \
    && apt update \
    # timezone thing
    && apt install -y tzdata \
    && ln -snf /usr/share/zoneinfo/$TZ /etc/localtime \
    && echo $TZ > /etc/timezone && dpkg-reconfigure -f noninteractive tzdata \
    # main libs
    && apt install -y python3 \
    python3-pip cmake libmysqlclient-dev autoconf libtool yasm nasm libgmp3-dev libdouble-conversion-dev \
    qttools5-dev-tools libfreetype-dev libfontconfig-dev libcups2-dev xcb \
    libx11-xcb-dev libxcb-xkb-dev libxcb-xinput-dev libxcb-sync-dev libxcb-render-util0-dev libxcb-xfixes0-dev \    
    libxcb-xinerama0-dev libxcb-randr0-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-glx0-dev libxkbcommon-x11-dev \
    libudev-dev libxi-dev libsm-dev libxrender-dev libdbus-1-dev \
    # some new stuff
    liblmdb-dev libuv1-dev libwebsockets-dev libcapnp-dev capnproto \
    # we probably don't need this package
    #libwebsockets-evlib-uv \
    # for libbtc
    build-essential libevent-dev \
    # free up space
    #&& rm -rf /var/lib/apt/lists/* \
    # link python
    && ln -s /usr/bin/python3 /usr/bin/python

WORKDIR /app

COPY . .

WORKDIR /app/libbtc
RUN ./autogen.sh
RUN ./configure
RUN make

WORKDIR /app
RUN ./autogen.sh
RUN export PKG_CONFIG=pkg-config
RUN export CFLAGS="-I`pwd`/libbtc/include"
RUN ./configure --with-own-libbtc=`pwd`/libbtc
RUN make -j`nproc`
