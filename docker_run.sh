docker run --rm -it \
  -v "$(pwd)":/home \
  aimilefth/ucla_prometheus:latest \
  bash -lc "cd /home && exec bash"
