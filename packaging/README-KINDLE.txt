Potion is a read-only Notion client for jailbroken Kindle.

Copy this directory to /mnt/us/potion and copy the Potion KUAL extension to
/mnt/us/extensions/Potion.

To connect without typing on the Kindle, create this file from your computer:

    /mnt/us/potion/notion-token.txt

Put only the Notion access token in the file (no "Bearer", quotes, or label),
safely eject the Kindle, and launch Potion from KUAL. Potion validates and
stores the token privately under /var/local/potion, then deletes the temporary
notion-token.txt copy. Settings are also stored under /var/local/potion.

If the token is rejected, Potion leaves notion-token.txt in place and shows an
error. Reconnect the Kindle to replace or remove that exposed file.
