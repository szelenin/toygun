/*
 * Toy Gun Remote - Flipper Zero app
 *
 * Sends commands to the turret over Bluetooth. The turret is an ESP32 that
 * CONNECTS TO US: we are the peripheral, it is the central. So this app does not
 * scan or connect to anything. It just opens the Flipper's Serial profile and
 * writes bytes into it; the turret is already listening.
 *
 * The commands are plain text, described in ../ToyGunTurretBLE/PROTOCOL.md:
 *
 *   U1 / U0   up      press / release
 *   D1 / D0   down
 *   L1 / L0   left
 *   R1 / R0   right
 *   F1 / F0   fire
 *   K         keepalive
 *   V         handshake, turret replies "VER 1 LizardGun3000"
 *
 * TWO THINGS THAT WILL WASTE YOUR EVENING IF YOU CHANGE THEM
 *
 * 1. ble_profile_serial_set_rpc_active(profile, false)
 *    Without it, the Flipper's remote-control layer eats every byte before it
 *    reaches the radio. Everything looks connected and nothing happens.
 *
 * 2. The keepalive is sent from the MAIN LOOP, not from the button handler.
 *    The turret stops moving and firing after 1500ms of silence - on purpose,
 *    so a dropped connection cannot leave the gun firing. Something must keep
 *    talking while a button is held down.
 *
 * Build:  ufbt launch      (with the Flipper plugged in by USB)
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>

#include <bt/bt_service/bt.h>
#include <profiles/serial_profile.h>
#include <services/serial_service.h>

#define TAG "ToyGunRemote"

// How often to repeat a held command. Must be well under the turret's 1500ms
// watchdog - 400ms leaves plenty of room if a packet goes missing.
#define KEEPALIVE_MS 400

// How long to block waiting for a button before looping round again. This is
// what gives the main loop a heartbeat when nobody is pressing anything.
#define LOOP_TICK_MS 100

#define RX_BUFFER_SIZE 64

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriMutex* mutex;

    Bt* bt;
    FuriHalBleProfileBase* profile;
    bool connected;

    // Which direction is being held right now, as the two-character command to
    // repeat. Empty string means nothing is held.
    char held[3];
    bool firing;

    // Last line the turret sent back, shown on screen so you can see it working
    char last_reply[RX_BUFFER_SIZE];

    bool running;
} ToyGunRemote;

// ---------------------------------------------------------------------------
// Talking to the turret
// ---------------------------------------------------------------------------

static void send_command(ToyGunRemote* app, const char* cmd) {
    if(!app->profile) return;
    ble_profile_serial_tx(app->profile, (uint8_t*)cmd, strlen(cmd));
    FURI_LOG_D(TAG, "sent %s", cmd);
}

// Called by the BLE stack when the turret sends us something. Runs on the
// Bluetooth thread, NOT on our thread, so it only copies the text and lets the
// draw callback show it later.
static uint16_t serial_rx_callback(SerialServiceEvent event, void* context) {
    ToyGunRemote* app = context;

    if(event.event == SerialServiceEventTypeDataReceived) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        uint16_t n = event.data.size;
        if(n > RX_BUFFER_SIZE - 1) n = RX_BUFFER_SIZE - 1;
        memcpy(app->last_reply, event.data.buffer, n);
        app->last_reply[n] = '\0';
        furi_mutex_release(app->mutex);
    }

    // Return value is flow control: how many more bytes we are willing to take.
    return RX_BUFFER_SIZE;
}

static void bt_status_callback(BtStatus status, void* context) {
    ToyGunRemote* app = context;
    app->connected = (status == BtStatusConnected);
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

static void draw_callback(Canvas* canvas, void* context) {
    ToyGunRemote* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Toy Gun Remote");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, app->connected ? "Turret: connected" : "Turret: waiting...");

    // What we are doing right now
    char state[32];
    snprintf(
        state,
        sizeof(state),
        "%s%s",
        app->held[0] ? app->held : "idle",
        app->firing ? "  FIRING" : "");
    canvas_draw_str(canvas, 2, 36, state);

    // What the turret said back
    canvas_draw_str(canvas, 2, 48, app->last_reply[0] ? app->last_reply : "(no reply yet)");

    canvas_draw_str(canvas, 2, 62, "arrows=aim  OK=fire  back=exit");

    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* context) {
    ToyGunRemote* app = context;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

// Turn a key into the two letters the turret expects. Returns NULL for keys we
// do not use.
static const char* key_to_letter(InputKey key) {
    switch(key) {
    case InputKeyUp: return "U";
    case InputKeyDown: return "D";
    case InputKeyLeft: return "L";
    case InputKeyRight: return "R";
    case InputKeyOk: return "F";
    default: return NULL;
    }
}

static void handle_input(ToyGunRemote* app, InputEvent* event) {
    const char* letter = key_to_letter(event->key);
    if(!letter) return;

    char cmd[3] = {letter[0], '0', '\0'};

    if(event->type == InputTypePress) {
        cmd[1] = '1';
        send_command(app, cmd);

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(event->key == InputKeyOk) {
            app->firing = true;
        } else {
            strncpy(app->held, cmd, sizeof(app->held) - 1);
        }
        furi_mutex_release(app->mutex);

    } else if(event->type == InputTypeRelease) {
        cmd[1] = '0';
        send_command(app, cmd);

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(event->key == InputKeyOk) {
            app->firing = false;
        } else {
            app->held[0] = '\0';
        }
        furi_mutex_release(app->mutex);
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int32_t toygun_remote_app(void* p) {
    UNUSED(p);

    ToyGunRemote* app = malloc(sizeof(ToyGunRemote));
    memset(app, 0, sizeof(ToyGunRemote));
    app->running = true;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    // Screen
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    // Bluetooth: hand the radio the Serial profile and take it off RPC duty.
    app->bt = furi_record_open(RECORD_BT);
    bt_set_status_changed_callback(app->bt, bt_status_callback, app);
    bt_disconnect(app->bt);
    furi_delay_ms(200); // the second core restarts when the profile changes

    app->profile = bt_profile_start(app->bt, ble_profile_serial, NULL);
    if(app->profile) {
        ble_profile_serial_set_rpc_active(app->profile, false); // see note at top
        ble_profile_serial_set_event_callback(app->profile, RX_BUFFER_SIZE, serial_rx_callback, app);
        FURI_LOG_I(TAG, "serial profile started");
    } else {
        FURI_LOG_E(TAG, "could not start the serial profile");
    }

    // Ask the turret who it is. Its reply appears on screen.
    send_command(app, "V");

    uint32_t last_keepalive = furi_get_tick();

    while(app->running) {
        InputEvent event;
        if(furi_message_queue_get(app->input_queue, &event, LOOP_TICK_MS) == FuriStatusOk) {
            if(event.key == InputKeyBack && event.type == InputTypeShort) {
                app->running = false;
            } else {
                handle_input(app, &event);
            }
        }

        // Heartbeat. Repeat whatever is held, or send a bare K, so the turret's
        // watchdog knows a human is still in control.
        if(furi_get_tick() - last_keepalive >= furi_ms_to_ticks(KEEPALIVE_MS)) {
            last_keepalive = furi_get_tick();

            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bool busy = app->held[0] || app->firing;
            char repeat[3];
            strncpy(repeat, app->held, sizeof(repeat));
            bool firing = app->firing;
            furi_mutex_release(app->mutex);

            if(busy) {
                if(repeat[0]) send_command(app, repeat);
                if(firing) send_command(app, "F1");
            } else {
                send_command(app, "K");
            }
        }

        view_port_update(app->view_port);
    }

    // Always stop the gun on the way out. Do not rely on the turret's watchdog
    // for this - it is the safety net, not the plan.
    send_command(app, "F0");
    send_command(app, "U0 D0 L0 R0");
    furi_delay_ms(100);

    bt_profile_restore_default(app->bt);
    furi_record_close(RECORD_BT);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
