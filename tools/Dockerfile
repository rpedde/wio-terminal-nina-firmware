FROM ubuntu:22.04@sha256:281c5745f657873d78e5531fc5ba8575f46ab7769b94550ac99543f122679986
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl python3 python3-pip git build-essential xz-utils bzip2 file && rm -rf /var/lib/apt/lists/*
COPY tools/install-toolchain.py tools/toolchain.lock.json tools/system-packages.lock /opt/fw-tools/
RUN python3 /opt/fw-tools/install-toolchain.py
COPY tools/requirements.lock /opt/fw-tools/requirements.lock
RUN pip3 install --no-cache-dir --only-binary=:all: --require-hashes -r /opt/fw-tools/requirements.lock
COPY tools /opt/fw-tools
WORKDIR /work
ENTRYPOINT ["python3", "/opt/fw-tools/container.py"]
