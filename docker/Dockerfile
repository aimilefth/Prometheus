FROM ghcr.io/ucla-vast/pocc

RUN apt-get update && apt-get install -y --no-install-recommends pip && rm -rf /var/lib/apt/lists/*
COPY requirements.txt requirements.txt
RUN pip install -r requirements.txt