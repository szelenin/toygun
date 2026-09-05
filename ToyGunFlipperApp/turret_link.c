#include "turret_link.h"

#include <furi.h>
#include <furi_hal.h>
#include <bt/bt_service/bt.h>
#include <profiles/serial_profile.h>
#include <services/serial_service.h>

#define TAG "TurretLink"

// Repeat held commands this often. Must be comfortably under the turret's
// 1500ms watchdog so a lost packet or two does not trip it.
#define KEEPALIVE_MS 400

#define RX_BUFFER_SIZE 64

// The five commandable letters, in the order their bits are stored.
static const char LETTERS[] = "UDLRF";

struct TurretLink {
    Bt* bt;
    FuriHalBleProfileBase* profile;
    bool connected;

    uint8_t held; // one bit per letter in LETTERS
    uint32_t last_keepalive;

    TurretReplyCallback reply_cb;
    void* reply_ctx;
    char rx[RX_BUFFER_SIZE];
};

// ---------------------------------------------------------------------------

static int letter_index(char letter) {
    for(int i = 0; LETTERS[i]; i++) {
        if(LETTERS[i] == letter) return i;
    }
    return -1;
}

static uint16_t rx_callback(SerialServiceEvent event, void* context) {
    TurretLink* link = context;

    if(event.event == SerialServiceEventTypeDataReceived) {
        uint16_t n = event.data.size;
        if(n > RX_BUFFER_SIZE - 1) n = RX_BUFFER_SIZE - 1;
        memcpy(link->rx, event.data.buffer, n);
        link->rx[n] = '\0';
        if(link->reply_cb) link->reply_cb(link->rx, link->reply_ctx);
    }

    // Flow control: how many more bytes we are willing to accept.
    return RX_BUFFER_SIZE;
}

static void status_callback(BtStatus status, void* context) {
    TurretLink* link = context;
    link->connected = (status == BtStatusConnected);
}

// ---------------------------------------------------------------------------

TurretLink* turret_link_alloc(void) {
    TurretLink* link = malloc(sizeof(TurretLink));
    memset(link, 0, sizeof(TurretLink));

    link->bt = furi_record_open(RECORD_BT);
    bt_set_status_changed_callback(link->bt, status_callback, link);

    bt_disconnect(link->bt);
    furi_delay_ms(200); // changing profile restarts the radio core

    link->profile = bt_profile_start(link->bt, ble_profile_serial, NULL);
    if(link->profile) {
        // Without this the Flipper's remote-control layer swallows every byte
        // before it reaches the radio: the link looks fine, nothing happens.
        ble_profile_serial_set_rpc_active(link->profile, false);
        ble_profile_serial_set_event_callback(
            link->profile, RX_BUFFER_SIZE, rx_callback, link);
        FURI_LOG_I(TAG, "serial profile started");
    } else {
        FURI_LOG_E(TAG, "could not start the serial profile");
    }

    link->last_keepalive = furi_get_tick();
    return link;
}

void turret_link_free(TurretLink* link) {
    if(!link) return;

    // Stop the gun explicitly. The turret's watchdog would catch this anyway,
    // but a watchdog is a safety net, not a plan.
    turret_link_send(link, "F0");
    turret_link_send(link, "U0 D0 L0 R0");
    furi_delay_ms(100);

    bt_profile_restore_default(link->bt);
    furi_record_close(RECORD_BT);
    free(link);
}

void turret_link_set_reply_callback(TurretLink* link, TurretReplyCallback cb, void* context) {
    link->reply_cb = cb;
    link->reply_ctx = context;
}

bool turret_link_connected(TurretLink* link) {
    return link->connected;
}

void turret_link_send(TurretLink* link, const char* cmd) {
    if(!link->profile) return;
    ble_profile_serial_tx(link->profile, (uint8_t*)cmd, strlen(cmd));
    FURI_LOG_D(TAG, "sent %s", cmd);
}

void turret_link_hold(TurretLink* link, char letter, bool pressed) {
    int i = letter_index(letter);
    if(i < 0) return;

    char cmd[3] = {letter, pressed ? '1' : '0', '\0'};
    turret_link_send(link, cmd);

    if(pressed) {
        link->held |= (1 << i);
    } else {
        link->held &= ~(1 << i);
    }
}

void turret_link_tick(TurretLink* link) {
    if(furi_get_tick() - link->last_keepalive < furi_ms_to_ticks(KEEPALIVE_MS)) return;
    link->last_keepalive = furi_get_tick();

    if(link->held == 0) {
        turret_link_send(link, "K"); // nothing held, just say we are still here
        return;
    }

    for(int i = 0; LETTERS[i]; i++) {
        if(link->held & (1 << i)) {
            char cmd[3] = {LETTERS[i], '1', '\0'};
            turret_link_send(link, cmd);
        }
    }
}
