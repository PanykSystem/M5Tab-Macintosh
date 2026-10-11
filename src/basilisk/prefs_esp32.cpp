/*
 *  prefs_esp32.cpp - Preferences handling for ESP32
 *
 *  BasiliskII ESP32 Port
 */

#include "sysdeps.h"
#include "prefs.h"
#include "boot_gui.h"
#include "board_config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define DEBUG 0
#include "debug.h"

// Platform-specific preferences items
prefs_desc platform_prefs_items[] = {
    {"powermgr", TYPE_BOOLEAN, false, "emulate a Power Manager on boards with a battery"},
    {NULL, TYPE_END, false, NULL}  // End marker
};

/*
 *  Load preferences from settings file
 */
void LoadPrefs(const char *vmdir)
{
    UNUSED(vmdir);
    
    Serial.println("[PREFS] Loading preferences...");
    
    // Set ROM file path
    PrefsReplaceString("rom", "/Q650.ROM");
    
    // Set model ID to Quadra 900 (14) for MacOS 8 compatibility
    // Quadra 650 is similar architecture
    PrefsReplaceInt32("modelid", 14);
    
    // Set CPU type to 68040
    PrefsReplaceInt32("cpu", 4);
    
    // Disable FPU (not implemented on ESP32)
    PrefsReplaceBool("fpu", false);
    
    // Get RAM size from Boot GUI selection
    uint32_t ram_size = BootGUI_GetRAMSize();
    if (ram_size == 0) {
        ram_size = 8 * 1024 * 1024;  // Default 8MB if GUI not initialized
    }
    PrefsReplaceInt32("ramsize", ram_size);
    Serial.printf("[PREFS] RAM: %d MB\n", ram_size / (1024 * 1024));
    
    // Set screen configuration
    PrefsReplaceString("screen", "win/640/480");
    
    // Get hard disk path from Boot GUI selection
    const char* disk_path = BootGUI_GetDiskPath();
    if (disk_path && strlen(disk_path) > 0) {
        PrefsReplaceString("disk", disk_path);
        Serial.printf("[PREFS] Disk: %s (read-write)\n", disk_path);
    } else {
        // Fallback to default if no disk selected
        PrefsReplaceString("disk", "/Macintosh8.dsk");
        Serial.println("[PREFS] Disk: /Macintosh8.dsk (default, read-write)");
    }
    
    // Audio toggle comes from preboot settings
    const bool audio_enabled = BootGUI_GetAudioEnabled();
    PrefsReplaceBool("nosound", !audio_enabled);
    Serial.printf("[PREFS] Audio: %s\n", audio_enabled ? "enabled" : "disabled");
    
    // Power Manager emulation (battery, sleep) on boards with a battery
    const bool power_manager = BootGUI_GetPowerManagerEnabled();
    PrefsReplaceBool("powermgr", power_manager);
    Serial.printf("[PREFS] Power Manager: %s\n", power_manager ? "enabled" : "disabled");

    // Get CD-ROM path from Boot GUI selection
    const char* cdrom_path = BootGUI_GetCDROMPath();
    if (cdrom_path && strlen(cdrom_path) > 0) {
        PrefsReplaceBool("nocdrom", false);
        PrefsReplaceString("cdrom", cdrom_path);
        Serial.printf("[PREFS] CD-ROM: %s\n", cdrom_path);
    } else {
        PrefsReplaceBool("nocdrom", true);
        Serial.println("[PREFS] CD-ROM: None");
    }

    // Get the shared-folder ("Unix root") path for ExtFS. BootGUI stores
    // paths relative to the SD root (e.g. "/Shared"); extfs.cpp / POSIX
    // need the full VFS path so we prepend BOARD_SD_MOUNT_POINT here.
    // Tab5 mounts the card at "/sd" (Arduino SD default) and Waveshare
    // at "/sd" (BSP setting), so for both boards a folder picked as
    // "/Shared" resolves to "/sd/Shared".
    //
    // We also stat() the resolved path and try to mkdir() it if missing,
    // because the silent failure mode (folder picked but not created on
    // SD) was the leading cause of "shared folder doesn't appear on the
    // Mac desktop" reports. If stat() still fails after mkdir() we leave
    // the pref in place - extfs.cpp will log the real errno - so the
    // serial console shows exactly what went wrong instead of just
    // "disabled".
    const char* extfs_rel = BootGUI_GetExtFSPath();
    if (extfs_rel && strlen(extfs_rel) > 0) {
        char extfs_vfs_path[BOOT_GUI_MAX_PATH + 8];
        snprintf(extfs_vfs_path, sizeof(extfs_vfs_path), "%s%s",
                 BOARD_SD_MOUNT_POINT, extfs_rel);

        struct stat st;
        if (stat(extfs_vfs_path, &st) != 0) {
            Serial.printf("[PREFS] ExtFS path '%s' missing (errno=%d), "
                          "auto-creating...\n",
                          extfs_vfs_path, errno);
            if (mkdir(extfs_vfs_path, 0755) != 0) {
                Serial.printf("[PREFS] mkdir('%s') failed: errno=%d\n",
                              extfs_vfs_path, errno);
            } else {
                Serial.printf("[PREFS] ExtFS folder created: %s\n",
                              extfs_vfs_path);
            }
        } else if (!S_ISDIR(st.st_mode)) {
            Serial.printf("[PREFS] ExtFS path '%s' exists but is not a "
                          "directory; ExtFS will be disabled\n",
                          extfs_vfs_path);
        }

        PrefsReplaceString("extfs", extfs_vfs_path);
        Serial.printf("[PREFS] Shared folder (ExtFS): %s\n", extfs_vfs_path);
    } else {
        PrefsRemoveItem("extfs");
        Serial.println("[PREFS] Shared folder (ExtFS): disabled (no folder selected)");
    }

    // No GUI
    PrefsReplaceBool("nogui", true);

    // Boot from first bootable volume by default. The "Boot from CD"
    // checkbox in the boot GUI overrides this with the CD-ROM driver
    // refnum (-62, see CDROMRefNum in cdrom.cpp), which makes Mac OS
    // pick the CD as the startup disk - same behaviour as holding "C"
    // at boot on a real Macintosh. We only honour the override when a
    // CD-ROM image was actually selected; an unattended "boot from CD"
    // with no disk would leave the Mac stuck at the question-mark.
    PrefsReplaceInt32("bootdrive", 0);
    PrefsReplaceInt32("bootdriver", 0);
    if (BootGUI_GetBootFromCD() && cdrom_path && strlen(cdrom_path) > 0) {
        // CDROMRefNum from src/basilisk/cdrom.cpp.
        PrefsReplaceInt32("bootdriver", -62);
        Serial.println("[PREFS] Boot order: CD-ROM (forced via Boot-from-CD)");
    }
    
    // Frame skip (lower = smoother but slower)
    PrefsReplaceInt32("frameskip", 4);
    
    Serial.println("[PREFS] Preferences loaded");
    
    // Debug: Print loaded prefs
    D(bug("  ROM: %s\n", PrefsFindString("rom")));
    D(bug("  Model ID: %d\n", PrefsFindInt32("modelid")));
    D(bug("  CPU: %d\n", PrefsFindInt32("cpu")));
    D(bug("  RAM: %d bytes\n", PrefsFindInt32("ramsize")));
}

/*
 *  Save preferences to settings file (no-op on ESP32)
 */
void SavePrefs(void)
{
    // Preferences are hardcoded, no saving needed
}

/*
 *  Add default preferences items
 */
void AddPlatformPrefsDefaults(void)
{
    // Defaults are set in LoadPrefs
}
