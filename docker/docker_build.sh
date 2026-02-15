#!/usr/bin/env bash

# IMPORTANT: This ensures the script exits with an error if 'docker build' fails,
# even though we are piping output to 'tee'.
set -o pipefail

# Image name
IMAGE_NAME="aimilefth/ucla_prometheus:latest"
LOG_FILE="build.log"

echo ">>> Building Docker Image: $IMAGE_NAME"
echo ">>> Logs will be saved to: $LOG_FILE"

# Build the docker image
# 1. --progress=plain : Makes the log file readable (avoids dynamic progress bars)
# 2. 2>&1           : Captures errors (stderr) into the same stream as output (stdout)
# 3. | tee ...      : Splits the stream to console and file
docker build --progress=plain -t $IMAGE_NAME . 2>&1 | tee "$LOG_FILE" 

# Check exit status of the build command
if [ $? -eq 0 ]; then
  echo ""
  echo ">>> Build Complete!"
else
  echo ""
  echo ">>> Build Failed! Check $LOG_FILE for details."
  exit 1
fi