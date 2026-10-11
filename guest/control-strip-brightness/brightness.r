/*
 * brightness.r - resources for the Brightness Control Strip module
 */
#include "MacTypes.r"
#include "Finder.r"
#include "Script.r"

/* The module's code, built by build.sh as a flat code resource. */
data 'sdev' (0, purgeable) {
    $$read("build/brightness.code")
};

/* Owner resource and Finder bundle (file type 'sdev', creator 'M5Br'). */
data 'M5Br' (0, purgeable) {
    $"1B" "M5Tab Brightness module 1.0"
};

resource 'BNDL' (128, purgeable) {
    'M5Br', 0,
    {
        'ICN#', {0, 128},
        'FREF', {0, 128}
    }
};

resource 'FREF' (128, purgeable) {
    'sdev', 0, ""
};

resource 'vers' (1, purgeable) {
    0x01, 0x00, release, 0x00, verUS,
    "1.0",
    "1.0, M5Tab-Macintosh"
};

#include "icons.r"
