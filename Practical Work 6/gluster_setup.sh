#!/bin/bash
set -e

echo "[*] Installing GlusterFS server and client..."
sudo apt update
sudo apt install -y glusterfs-server glusterfs-client

echo "[*] Enabling and starting glusterd..."
sudo systemctl enable --now glusterd
sudo systemctl status glusterd --no-pager

echo "[*] Creating bricks on local host..."
sudo mkdir -p /data/brick1/gv0
sudo mkdir -p /data/brick2/gv0
sudo mkdir -p /data/brick3/gv0

HOST=$(hostname)
VOL_NAME=gv0

echo "[*] Creating replicated volume '$VOL_NAME' with 3 bricks on host $HOST..."
sudo gluster volume create $VOL_NAME replica 3 transport tcp \
  $HOST:/data/brick1/gv0 \
  $HOST:/data/brick2/gv0 \
  $HOST:/data/brick3/gv0 force

echo "[*] Starting volume '$VOL_NAME'..."
sudo gluster volume start $VOL_NAME
sudo gluster volume info $VOL_NAME

echo "[*] Mounting GlusterFS volume at /mnt/$VOL_NAME..."
sudo mkdir -p /mnt/$VOL_NAME
sudo mount -t glusterfs $HOST:/$VOL_NAME /mnt/$VOL_NAME

echo "[*] Setting ownership of /mnt/$VOL_NAME to current user ($USER)..."
sudo chown -R "$USER:$USER" /mnt/$VOL_NAME

echo "[*] GlusterFS volume '$VOL_NAME' is ready at /mnt/$VOL_NAME"
