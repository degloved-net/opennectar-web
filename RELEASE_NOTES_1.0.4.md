# Open Nectar 1.0.4

This release fixes the crashes in Pikmin 2 when entering caves, travelling to
a new area or loading an area after saving. It also makes the area map run
smoothly on Android and lets you control the map by touch. Saves, settings
and texture packs carry over.

**Updating:** press **Update** in your launcher. On Android, install the new
APK over the old one.

## Pikmin 2: crash fixes

* **No more crashes when entering a cave, travelling to a new area, or
  landing after saving.** The game often crashed just as the level started
  (usually right after the landing cutscene). The walk animation kept its
  data in memory that the game frees when it loads a new area. That data now
  stays valid between areas.

## Pikmin 2: area map

* **The map runs at full speed on Android.** On phones it was extremely
  slow because of the way the sides of the screen were filled in widescreen.
* **New widescreen sides.** The map's frame and photo only exist in 4:3, so
  the sides now show the dark blue fabric from the **Extras** menu with a
  vignette, instead of a stretched copy of the frame's edge.
* **Touch controls (Android), like in Pikmin 1:**
  * Tap another unlocked area to fly the ship there.
  * Tap the selected area to bring up the landing prompt.
  * Tap **Piklopedia** or **Treasures** to open them.

## Pikmin 2: menus

* **Controller: LB/RB switch tabs in the F1 menu.** The shoulder buttons did
  nothing in Pikmin 2's settings menu, so you could not change tabs with a
  controller. Holding the arrow keys on a keyboard now repeats in that menu
  too.
* **Options and file select:** in widescreen, the background now simply
  continues to the sides of the screen. This replaces an effect that slowed
  down phones.

## Downloads

| File | System |
|---|---|
| `nectar-windows.zip` | Windows 10 and 11, 64 bit |
| `nectar-linux.tar.gz` | Linux, 64 bit |
| `Open_Nectar-x86_64.AppImage` | Linux, 64 bit, single file |
| `open_nectar_1.0.4.apk` | Android |

You need your own Pikmin and Pikmin 2 discs (ISO, GCM, GCZ or RVZ). No game
data is included or downloaded.
