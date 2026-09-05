/*
 * turret_link - talking to the toy gun turret over Bluetooth
 *
 * Drop these two files (turret_link.h + turret_link.c) into any Flipper app and
 * you can drive the turret without thinking about BLE at all. Nothing in here
 * knows about screens or buttons, so it fits whatever app you are writing.
 *
 * Minimal use:
 *
 *     TurretLink* link = turret_link_alloc();
 *
 *     while(running) {
 *         // ... your app, your buttons, your screen ...
 *         turret_link_hold(link, 'R', pressed);   // pan right while held
 *         turret_link_tick(link);                 // call this often!
 *     }
 *
 *     turret_link_free(link);
 *
 * turret_link_tick() is the one you must not forget. The turret stops moving
 * and firing after 1500ms of silence - deliberately, so a dropped connection
 * can never leave the gun firing - so something has to keep talking. tick()
 * does that for you, but only if you call it from your main loop.
 *
 * The command letters are U, D, L, R for the four directions and F for fire.
 * The whole protocol is in ../ToyGunTurretBLE/PROTOCOL.md.
 */

#pragma once

#include <stdbool.h>

typedef struct TurretLink TurretLink;

/** Called when the turret sends something back, e.g. "POS 95 88".
 *  Runs on the Bluetooth thread, not yours - copy what you need, do not draw. */
typedef void (*TurretReplyCallback)(const char* text, void* context);

/** Open the link. Takes over the Flipper's Bluetooth serial channel. */
TurretLink* turret_link_alloc(void);

/** Stop the gun, hand Bluetooth back, and clean up. */
void turret_link_free(TurretLink* link);

/** Hear what the turret says back. Optional. */
void turret_link_set_reply_callback(TurretLink* link, TurretReplyCallback cb, void* context);

/** True once the turret has connected to us. */
bool turret_link_connected(TurretLink* link);

/** Send a raw command, e.g. "V" or "B64". Most of the time use hold() instead. */
void turret_link_send(TurretLink* link, const char* cmd);

/** Press or release one of U D L R F. Sends the command and remembers it, so
 *  turret_link_tick() can keep it alive while the button is down. */
void turret_link_hold(TurretLink* link, char letter, bool pressed);

/** Call this from your main loop, at least every few hundred milliseconds.
 *  Repeats whatever is held, or sends a keepalive when nothing is. */
void turret_link_tick(TurretLink* link);
