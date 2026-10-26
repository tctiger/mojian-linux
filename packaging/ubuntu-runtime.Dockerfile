FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
COPY mojian_1.0.0-4_amd64.deb /tmp/mojian.deb
RUN apt-get update && apt-get install -y /tmp/mojian.deb desktop-file-utils python3 \
    && rm -rf /var/lib/apt/lists/*
COPY verify-deb-package.py /tmp/verify-deb-package.py
CMD ["python3", "/tmp/verify-deb-package.py", "/tmp/mojian.deb", "--output", "/evidence/ubuntu-verification.json"]
