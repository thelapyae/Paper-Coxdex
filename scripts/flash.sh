#!/bin/zsh

set -eu
setopt NULL_GLOB

project_dir=${0:A:h:h}
cd "$project_dir"

ports=(/dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial*)
if (( ${#ports[@]} == 0 )); then
  print "PaperS3 serial port not found. Enter download mode first:"
  print "1. Connect USB."
  print "2. Hold the side button until the rear LED flashes red."
  print "3. Run this script again."
  exit 1
fi

print "Flashing through ${ports[1]}"
pio run -e papers3 -t upload --upload-port "${ports[1]}"
