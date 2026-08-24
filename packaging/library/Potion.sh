#!/bin/sh
# Name: Potion
# Author: Potion
# Description: Open the Potion Notion client.
# DontUseFBInk

POTION_LAUNCHER=/mnt/us/potion/potion.sh

if [ ! -x "$POTION_LAUNCHER" ]; then
    eips 2 4 "Potion is not installed."
    eips 2 6 "Copy the dist contents to the Kindle drive."
    exit 1
fi

exec "$POTION_LAUNCHER"
