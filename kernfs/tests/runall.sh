#! /usr/bin/sudo /bin/bash

# Abort script immediately on failure.
set -e

COMPUTE_AVERAGE_AWK='
{
  while (match($0, /\( [0-9], ([0-9]+\.[0-9]+) \)/, a) )
  {
    sum += a[1];
    num += 1;
    $0 = substr($0, RSTART + RLENGTH);
  }
} END {printf(" ( 9, %.2f ) ", sum/num)}'

if [ "$1" == "" ]; then
  echo "Argument 1 must contain INSERT_DATA_FILENAME_PREFIX."
  exit 1
fi

if [ "$2" == "" ]; then
  echo "Argument 2 must contain LOOKUP_DATA_FILENAME_PREFIX."
  exit 1
fi

make clean scrub all

for b in 1 255 512 16384; do
  for i in `seq 0 2`; do
    for l in `seq 0 2`; do
      sudo ../../libfs/bin/mkfs.mlfs 1
      sync
      sudo ./run.sh extent_test $i $l $b $1_$b.txt $2_$b.txt
    done
  done
  # Calculate the average.
  cat $1_$b.txt | awk "$COMPUTE_AVERAGE_AWK" >> $1_$b.txt
  cat $2_$b.txt | awk "$COMPUTE_AVERAGE_AWK" >> $2_$b.txt
done

