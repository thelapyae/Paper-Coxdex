#!/bin/zsh

set -eu
setopt NULL_GLOB

ports=(/dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial*)

if (( ${#ports[@]} == 0 )); then
  print "PaperS3 serial port not found."
  print "Power it on, keep USB connected, then hold the side button until the rear LED flashes red."
  exit 1
fi

print "Detected serial ports:"
printf '  %s\n' "${ports[@]}"
