Potion is a lightweight Notion reader for Kindle. Text selected
within one ordinary paragraph or list item can be highlighted, bolded, or
underlined on Notion when the connection has update-content access.

USB installation
================

This release archive is laid out like the Kindle USB drive. Extract it on your
computer, then copy the archive's CONTENTS (potion, extensions, and documents)
to the top level of the mounted Kindle drive. Do not copy the enclosing dist or
release directory.

After copying, the Kindle drive must contain:

    potion/potion.sh
    extensions/Potion/config.xml
    documents/Potion.sh

Safely eject the Kindle. Potion then appears as "Potion" in the Kindle Library
and as "Launch Potion" in KUAL. The Library entry requires PEKI, the same
launcher support used by a Library-installed KUAL.sh. KUAL remains an optional
second way to launch Potion.

To connect without typing on the Kindle, create this file from your computer:

    potion/notion-token.txt

Put only the Notion access token in the file (no "Bearer", quotes, or label),
safely eject the Kindle, and launch Potion from the Library or KUAL. Potion validates and
stores the token privately under /var/local/potion, then deletes the temporary
notion-token.txt copy. Settings are also stored under /var/local/potion.

If the token is rejected, Potion leaves notion-token.txt in place and shows an
error. Reconnect the Kindle to replace or remove that exposed file.

Potion is free software under GNU AGPL v3 or later, without warranty. License,
source, and third-party notices are included in this directory and at:

    https://github.com/nimamox/Potion
