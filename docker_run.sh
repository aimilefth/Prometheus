#!/usr/bin/env bash
set -euo pipefail

# Capture host user ID and group ID
HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

# 1. Load UUID from .env (if present)
# Create a file named .env in the same folder as this script
# and add: AMPL_UUID=your-uuid-here
ENV_FILE="$(pwd)/.env"
if [[ -f "${ENV_FILE}" ]]; then
  set -a
  source "${ENV_FILE}"
  set +a
fi

# 2. Define directory for the license inside the container
# Based on your tree, the binaries are here:
AMPL_DIR_CONT="/home/ampl/ampl.linux-intel64"

echo "Starting Docker container..."

# 3. Run Container
docker run --rm -it \
  -v "$(pwd)":/home \
  -w /home \
  -e HOST_UID="${HOST_UID}" \
  -e HOST_GID="${HOST_GID}" \
  -e AMPL_UUID="${AMPL_UUID:-}" \
  -e AMPL_LICFILE="${AMPL_DIR_CONT}/ampl.lic" \
  aimilefth/ucla_prometheus:latest \
  bash -c "
    set -e
    
    # 4. Add AMPL binaries to the system PATH so 'ampl' and 'amplkey' commands work
    export PATH=\"\$PATH:${AMPL_DIR_CONT}\"

    # 5. Activate License if UUID is provided
    if [[ -n \"\${AMPL_UUID:-}\" ]]; then
      echo 'Found AMPL_UUID, checking license...'
      # We attempt activation. '|| true' ensures the script doesn't crash 
      # if the license is already active or valid.
      amplkey activate --uuid \"\${AMPL_UUID}\" || true
    else
      echo 'No AMPL_UUID set. Skipping activation.'
      echo 'Ensure .env exists with AMPL_UUID=... if you need a fresh license.'
    fi

    echo
    echo 'AMPL Environment Ready.'
    echo 'Location: ${AMPL_DIR_CONT}'
    echo

    # 6. Drop into interactive shell
    exec bash"