#! /usr/bin/sudo /bin/bash

# Abort script immediately on failure.
set -e

if [ "$1" == "" ]; then
  echo "Argument 1 must contain INSERT_DATA_FILENAME_PREFIX."
  exit 1
fi

if [ "$2" == "" ]; then
  echo "Argument 2 must contain LOOKUP_DATA_FILENAME_PREFIX."
  exit 1
fi

for b in 1 16; do
  for i in `seq 0 2`; do
    for l in `seq 0 2`; do
      sudo ./run.sh extent_test $i $l $b $1_$b.txt $2_$b.txt
    done
  done
done
