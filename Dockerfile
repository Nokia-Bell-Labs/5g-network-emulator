FROM ubuntu:24.04

RUN DEBIAN_FRONTEND=noninteractive apt-get update && apt-get install -y --no-install-recommends \
  build-essential \
  ca-certificates \
  iperf3 \
  iproute2 \
  iptables \
  libmnl-dev \
  libnetfilter-queue-dev \
  psmisc \
  python3 \
  python3-matplotlib \
  python3-numpy \
  python3-pip \
  python3-venv \
  sudo \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src/5g-network-emulator
COPY . .

RUN make -j"$(nproc)" \
  && make test \
  && make test-transport \
  && make test-transport-integration

# Test dependencies stay out of the shipped API environment.
RUN python3 -m venv /tmp/fikore-api-test \
  && /tmp/fikore-api-test/bin/pip install --no-cache-dir --upgrade pip \
  && /tmp/fikore-api-test/bin/pip install --no-cache-dir -r api/requirements-test.txt \
  && make test-api PYTHON=/tmp/fikore-api-test/bin/python \
  && rm -rf /tmp/fikore-api-test

# The control API ships with the emulator and is built here, in its own venv: it is
# independent of the repo's .venv, which belongs to the analyzers and pulls in numpy and
# matplotlib.
RUN python3 -m venv --clear api/.venv \
  && api/.venv/bin/pip install --no-cache-dir --upgrade pip \
  && api/.venv/bin/pip install --no-cache-dir -r api/requirements.txt

ENTRYPOINT ["/usr/src/5g-network-emulator/entrypoint.sh"]
CMD ["config/control_demo.ini"]
