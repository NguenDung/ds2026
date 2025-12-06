#!/bin/bash
MNT=${1:-/mnt/gv0}
N=${2:-1000}
DIR=$MNT/smalltest_$RANDOM

mkdir -p "$DIR"

echo "Target dir: $DIR"
echo "Writing $N small files..."
t1=$(date +%s.%N)
for i in $(seq 1 $N); do
  echo "hello_$i" > "$DIR/file_$i"
done
sync
t2=$(date +%s.%N)

echo "Reading $N small files..."
t3=$(date +%s.%N)
for i in $(seq 1 $N); do
  cat "$DIR/file_$i" > /dev/null
done
t4=$(date +%s.%N)

w_time=$(echo "$t2-$t1" | bc)
r_time=$(echo "$t4-$t3" | bc)

w_ops=$(echo "scale=2; $N / $w_time" | bc)
r_ops=$(echo "scale=2; $N / $r_time" | bc)

echo "Write time: $w_time s  => $w_ops ops/s"
echo "Read  time: $r_time s  => $r_ops ops/s"
