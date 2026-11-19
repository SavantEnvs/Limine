# syntax=docker/dockerfile:1.7
#
# Commit image for Limine. Builds on the org base; the dependencies upstream's ./bootstrap clones
# are fetched once here at the exact commits bootstrap pins (sha256-checked), into
# /opt/toolchains/deps/<name>-<commit7>/src, so mayhem/build.sh runs offline (SPEC 6.2 item 19).
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

USER root
# name repo commit sha256-of-codeload-tarball  (commits from ./bootstrap)
RUN set -eu; \
    fetch() { \
      d="/opt/toolchains/deps/$1-$(echo "$3" | cut -c1-7)"; mkdir -p "$d/src"; \
      curl -fsSL -o /tmp/dep.tgz "https://codeload.github.com/$2/tar.gz/$3"; \
      echo "$4  /tmp/dep.tgz" | sha256sum -c -; \
      tar -xzf /tmp/dep.tgz --strip-components=1 -C "$d/src"; rm -f /tmp/dep.tgz; chmod -R a+rX "$d"; \
    }; \
    fetch freestanding-c-hdrs osdev0/freestanding-c-hdrs 59d91c6ed2f97f7de619e2c85a376aea0304d207 a8b7270b16acc2ada3684ba3e24c4c3ce115d92cb8d4e9416b498e9b11234b0e; \
    fetch picoefi PicoEFI/PicoEFI 9065da08260729e932531110a5a7fb90c0022aa9 1a0ddffb1a23296b4b4241a43238be3bee28cb35beab93753dd0fd0a9e00e26f; \
    fetch limine-protocol Limine-Bootloader/limine-protocol 877c2d4c50e170575f8b8e7fbf7b8fa89b1dc157 a28197cf2b726543e4be816c7ce70a0094d2b25f393aa22ec93549294cff6fc2; \
    fetch flanterm Mintsuki/Flanterm 3e311fe2ce4f663273c22d1a229745bc53afa930 292187bec460f875537c0e82d7e93dc48cc31e240c5e9b3ca9a48a39a3a3613d; \
    fetch libfdt osdev0/libfdt 7bf94e6347129d17eca263112296ad170dec28a9 3cc7db4d2bb7b71b2149697bfd327d07f8a699cf702f5c5d514424dbe44765c0; \
    fetch pdgzip iczelia/pdgzip 010197664688f071b923615ea235ecb24963b61a 04cdcd3cd985cd0c6d61ef861c7d2234c971307c77218da5d0d0efda77a1454a; \
    fetch stbi-hardened Mintsuki/stbi-hardened b85b7e87f6f87300f32f7de3d94e1ed083a71263 d93737a93ad239448dfc4ace36ae3ccbcd0b704c9c5ea306277a5853e70ba9f6

USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
