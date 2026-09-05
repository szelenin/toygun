/*
 * Toy Gun Remote - EXAMPLE app for the Flipper Zero
 *
 * This is a worked example, not the app you have to use. It exists so you can
 * see one way of doing it and steal the bits you want. Write your own - it will
 * be better, because it will be yours.
 *
 * All the Bluetooth lives in turret_link.c/h. This file is only screen and
 * buttons, and it is deliberately small so you can read the whole thing.
 *
 * The three lines that matter:
 *
 *     TurretLink* link = turret_link_alloc();     // open the link
 *     turret_link_hold(link, 'R', pressed);       // pan right while held
 *     turret_link_tick(link);                     // call from your main loop
 *
 * That last one is not optional. The turret stops after 1500ms of silence, on
 * purpose, so a dropped connection can never leave the gun firing.
 *
 * Commands are in ../ToyGunTurretBLE/PROTOCOL.md.
 *
 * Build:  ufbt launch      (Flipper plugged in by USB)
 */

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

#include "turret_link.h"

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriMutex* mutex;

    TurretLink* link;
    char last_reply[64];
    bool running;
} ToyGunRemote;

// ---------------------------------------------------------------------------

// The turret answered. This runs on the Bluetooth thread, so it only copies the
// text - drawing happens later, on our own thread.
static void on_reply(const char* text, void* context) {
    ToyGunRemote* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    strncpy(app->last_reply, text, sizeof(app->last_reply) - 1);
    app->last_reply[sizeof(app->last_reply) - 1] = '\0';
    furi_mutex_release(app->mutex);
}

static void draw_callback(Canvas* canvas, void* context) {
    ToyGunRemote* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Toy Gun Remote");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(
        canvas, 2, 26, turret_link_connected(app->link) ? "connected" : "waiting...");
    canvas_draw_str(canvas, 2, 40, app->last_reply[0] ? app->last_reply : "(no reply yet)");
    canvas_draw_str(canvas, 2, 62, "arrows=aim  OK=fire  back=exit");

    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    ToyGunRemote* app = context;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

// Flipper button -> turret command letter
static char key_to_letter(InputKey key) {
    switch(key) {
    case InputKeyUp: return 'U';
    case InputKeyDown: return 'D';
    case InputKeyLeft: return 'L';
    case InputKeyRight: return 'R';
    case InputKeyOk: return 'F';
    default: return 0;
    }
}

// ---------------------------------------------------------------------------

int32_t toygun_remote_app(void* p) {
    UNUSED(p);

    ToyGunRemote* app = malloc(sizeof(ToyGunRemote));
    memset(app, 0, sizeof(ToyGunRemote));
    app->running = true;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    // Everything Bluetooth is behind this one call
    app->link = turret_link_alloc();
    turret_link_set_reply_callback(app->link, on_reply, app);

    // Ask the turret who it is. Its answer appears on screen, which is how you
    // know the whole chain works.
    turret_link_send(app->link, "V");

    while(app->running) {
        InputEvent event;
        // The 100ms timeout is what gives the loop a heartbeat when nobody is
        // touching a button, so tick() still gets called.
        if(furi_message_queue_get(app->input_queue, &event, 100) == FuriStatusOk) {
            if(event.key == InputKeyBack && event.type == InputTypeShort) {
                app->running = false;
            } else {
                char letter = key_to_letter(event.key);
                if(letter && event.type == InputTypePress) {
                    turret_link_hold(app->link, letter, true);
                } else if(letter && event.type == InputTypeRelease) {
                    turret_link_hold(app->link, letter, false);
                }
            }
        }

        turret_link_tick(app->link);
        view_port_update(app->view_port);
    }

    turret_link_free(app->link);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
