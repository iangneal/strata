#! /usr/bin/sudo /bin/bash
OUT=log.out
b=16
for i in $(seq 0 2); do
  for l in $(seq 0 2); do
    ./run.sh extent_test $i $l $b >> $OUT 2>&1
  done
done

