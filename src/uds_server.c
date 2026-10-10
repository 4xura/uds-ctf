#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include "iso14229.h"
#include <time.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/stat.h>

#define UDS_PHYS_ID 0x7E0
#define UDS_RESP_ID 0x7E8
#define MAX_MEMORY_READ_SIZE 0x1000
#define NEG_RESPONSE 0x7F
#define NRC_SUBFUNCTION_NOT_SUPPORTED 0x12
#define NRC_INCORRECT_LENGTH 0x13
#define NRC_REQUEST_SEQUENCE_ERROR 0x24
#define NRC_CONDITIONS_NOT_CORRECT 0x22
#define NRC_SECURITY_ACCESS_DENIED 0x33
#define NRC_INVALID_KEY 0x35
#define NRC_SUBFUNCTION_NOT_SUPPORTED_IN_SESSION 0x7E
#define BOOT_MARKER_PATH "/opt/udsctf/.boot_window"
#define BOOT_DID 0xB007
#define BOOT_WINDOW_SECONDS 20

enum {
    FLAG_F190 = 0,
    FLAG_C1C2,
    FLAG_D1D2,
    FLAG_BOOT,
    FLAG_MEMORY,
    FLAG_COUNT
};

static const char *fallback_flags[FLAG_COUNT] = {
    "TEST{FAKE_FLAGS_IN_TESTING_PUBLIC}",
    "TEST{FAKE_FLAGS_IN_TESTING_LEVEL1}",
    "TEST{FAKE_FLAGS_IN_TESTING_LEVEL3}",
    "TEST{FAKE_FLAGS_IN_TESTING_RESET}",
    "TEST{FAKE_FLAGS_IN_TESTING_MEMORY_PLACEHOLDER_00000000000000000000}"
};

int wait_fc_frame(int s, int timeout_ms);

// Global ELF file data buffer
static uint8_t *g_elf_data = NULL;
static size_t g_elf_size = 0;

static uint32_t g_seed = 0;
static uint8_t pending_seed_level = 0;
static int security_unlocked = 0;
static uint8_t current_session = 0x01; // default session
static uint8_t security_level = 0; // current security access level
static int rapid_power_down_enabled = 0;
static int boot_window_active = 0;
static time_t boot_window_deadline = 0;
static time_t last_tester_present_time = 0; // record last TesterPresent timestamp
static char flag_values[FLAG_COUNT][96];
static size_t flag_lens[FLAG_COUNT];

void init_fallback_flags(void) {
    for (int i = 0; i < FLAG_COUNT; i++) {
        snprintf(flag_values[i], sizeof(flag_values[i]), "%s", fallback_flags[i]);
        flag_lens[i] = strlen(flag_values[i]);
    }
}

void load_runtime_flags(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        printf("[LOG] using fallback testing flags; runtime flag file not available\n");
        return;
    }

    char line[128];
    for (int i = 0; i < FLAG_COUNT; i++) {
        if (fgets(line, sizeof(line), fp) == NULL) {
            printf("[LOG] runtime flag file ended early; keeping fallback for remaining flags\n");
            break;
        }

        line[strcspn(line, "\r\n")] = '\0';
        const char flag_prefix[7] = { 0x55, 0x44, 0x53, 0x43, 0x54, 0x46, 0x7b };
        if (memcmp(line, flag_prefix, sizeof(flag_prefix)) != 0 || strlen(line) >= sizeof(flag_values[i])) {
            printf("[LOG] invalid runtime flag entry at index %d; keeping fallback\n", i);
            continue;
        }

        snprintf(flag_values[i], sizeof(flag_values[i]), "%s", line);
        flag_lens[i] = strlen(flag_values[i]);
    }

    fclose(fp);
    printf("[LOG] loaded runtime flags from protected file\n");
}

const char *get_flag(int id) {
    if (id < 0 || id >= FLAG_COUNT) {
        return "";
    }

    return flag_values[id];
}

static void write_boot_marker(void) {
    FILE *fp = fopen(BOOT_MARKER_PATH, "w");
    if (fp == NULL) {
        printf("[LOG] failed to write boot marker: %s\n", strerror(errno));
        return;
    }

    fprintf(fp, "boot-window\n");
    fclose(fp);
    chmod(BOOT_MARKER_PATH, 0600);
    printf("[LOG] boot marker written for hidden reset DID\n");
}

static void load_boot_marker(void) {
    if (access(BOOT_MARKER_PATH, F_OK) != 0) {
        return;
    }

    unlink(BOOT_MARKER_PATH);
    boot_window_active = 1;
    boot_window_deadline = time(NULL) + BOOT_WINDOW_SECONDS;
    printf("[LOG] hidden reset DID active for %d seconds\n", BOOT_WINDOW_SECONDS);
}

void patch_runtime_elf_view(void) {
    if (g_elf_data == NULL || g_elf_size == 0) {
        return;
    }

    const char *needle = fallback_flags[FLAG_MEMORY];
    size_t needle_len = strlen(needle);
    const char *flag = get_flag(FLAG_MEMORY);
    size_t flag_len = flag_lens[FLAG_MEMORY];

    for (size_t i = 0; i + needle_len <= g_elf_size; i++) {
        if (memcmp(g_elf_data + i, needle, needle_len) == 0) {
            memset(g_elf_data + i, 0, needle_len);
            memcpy(g_elf_data + i, flag, flag_len < needle_len ? flag_len : needle_len);
            printf("[LOG] patched runtime ELF view for memory DID at offset 0x%zx\n", i);
            return;
        }
    }

    printf("[LOG] memory placeholder not found in runtime ELF view\n");
}

// Signal handler
void segfault_handler(int sig) {
    printf("[LOG] Caught segmentation fault signal %d; exiting safely\n", sig);
    exit(1);
}

uint32_t generate_seed() {
    return (uint32_t)rand();
}

__attribute__((noinline)) uint32_t calc_key(uint32_t seed) {
    return seed ^ 0xdeadbeef;
}

__attribute__((noinline)) uint32_t calc_key_level3(uint32_t seed) {
    // More complex level 3 key algorithm
    uint32_t key = seed;

    // Step 1: rotate left
    key = (key << 7) | (key >> 25);

    // Step 2: XOR operation
    key ^= 0xCAFEBABE;

    // Step 3: addition
    key += 0x12345678;

    // Step 4: bit operation
    key = (key & 0xFFFF0000) | ((key & 0x0000FFFF) ^ 0xABCD);

    // Step 5: final XOR
    key ^= 0xDEADBEEF;

    return key;
}

__attribute__((noinline)) uint32_t calc_key_level5(uint32_t seed) {
    // Level 5 key algorithm - more complex
    uint32_t key = seed;

    // Step 1: multiple XORs
    key ^= 0x12345678;
    key ^= 0x87654321;

    // Step 2: rotate right
    key = (key >> 13) | (key << 19);

    // Step 3: bit operation
    key = (key & 0xFF00FF00) | ((key & 0x00FF00FF) ^ 0x55555555);

    // Step 4: addition
    key += 0xDEADBEEF;

    // Step 5: final XOR
    key ^= 0xCAFEBABE;

    return key;
}

// ISO-TP multi-frame send for raw data without DID
void send_isotp_response_raw(int s, const uint8_t *data, size_t data_len) {
    struct can_frame txf;
    txf.can_id = UDS_RESP_ID;

    if (data_len <= 7) {
        // single-frame send
        txf.data[0] = data_len; // single-frame length field
        memcpy(&txf.data[1], data, data_len);
        txf.can_dlc = 1 + data_len;
        printf("[LOG] [ISOTP] single-frame send: ");
        for (int i = 0; i < txf.can_dlc; ++i) printf("%02X ", txf.data[i]);
        printf("\n");
        write(s, &txf, sizeof(struct can_frame));
        return;
    }

    // multi-frame send
    // 1. send first frame
    txf.data[0] = 0x10 | ((data_len >> 8) & 0x0F);
    txf.data[1] = data_len & 0xFF;
    size_t first_frame_data = 6;
    memcpy(&txf.data[2], data, first_frame_data);
    txf.can_dlc = 8;
    printf("[LOG] [ISOTP] first frame sent: ");
    for (int i = 0; i < txf.can_dlc; ++i) printf("%02X ", txf.data[i]);
    printf("\n");
    write(s, &txf, sizeof(struct can_frame));

    // 2. wait for FC frame
    if (!wait_fc_frame(s, 1000)) {
        printf("[LOG] [ISOTP] FC frame not received; aborting multi-frame send\n");
        return;
    }

    // 3. send consecutive frames
    size_t sent = first_frame_data;
    uint8_t sn = 1;
    while (sent < data_len) {
        txf.data[0] = 0x20 | (sn & 0x0F);
        size_t remain = data_len - sent;
        size_t chunk = remain > 7 ? 7 : remain;
        memcpy(&txf.data[1], data + sent, chunk);
        txf.can_dlc = 1 + chunk;
        printf("[LOG] [ISOTP] consecutive frame SN=%d sent: ", sn);
        for (int i = 0; i < txf.can_dlc; ++i) printf("%02X ", txf.data[i]);
        printf("\n");
        write(s, &txf, sizeof(struct can_frame));
        sent += chunk;
        sn = (sn + 1) & 0x0F;
        usleep(1000 * 10);
    }
}

// ISO-TP multi-frame send with strict flow control
int wait_fc_frame(int s, int timeout_ms) {
    struct can_frame rx;
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(s, &readfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ret = select(s + 1, &readfds, NULL, NULL, &tv);
    if (ret > 0 && FD_ISSET(s, &readfds)) {
        int nbytes = read(s, &rx, sizeof(struct can_frame));
        if (nbytes > 0 && rx.can_id == UDS_PHYS_ID && rx.data[0] == 0x30) {
            printf("[LOG] [ISOTP] received Flow Control frame (FC): ");
            for (int i = 0; i < rx.can_dlc; ++i) printf("%02X ", rx.data[i]);
            printf("\n");
            return 1;
        } else {
            printf("[LOG] [ISOTP] received non-FC frame or mismatched can_id; ignoring\n");
        }
    } else {
        printf("[LOG] [ISOTP] timed out waiting for FC frame\n");
    }
    return 0;
}

// ISO-TPmulti-frame send
void send_isotp_response(int s, uint8_t sid, uint8_t *did, const char *data, size_t data_len) {
    struct can_frame txf;
    txf.can_id = UDS_RESP_ID;
    uint8_t uds_header[3];
    uds_header[0] = sid;
    uds_header[1] = did[0];
    uds_header[2] = did[1];
    size_t uds_header_len = 3;
    size_t total_len = uds_header_len + data_len;
    if (total_len <= 7) {
        txf.data[0] = 0x02 + data_len;
        memcpy(&txf.data[1], uds_header, uds_header_len);
        memcpy(&txf.data[1 + uds_header_len], data, data_len);
        txf.can_dlc = 1 + uds_header_len + data_len;
        printf("[LOG] [ISOTP] single-frame send: ");
        for (int i = 0; i < txf.can_dlc; ++i) printf("%02X ", txf.data[i]);
        printf("\n");
        write(s, &txf, sizeof(struct can_frame));
        return;
    }
    // multi-frame
    // 1. send first frame
    txf.data[0] = 0x10 | ((total_len >> 8) & 0x0F);
    txf.data[1] = total_len & 0xFF;
    size_t first_frame_data = 6;
    memcpy(&txf.data[2], uds_header, uds_header_len);
    size_t copy1 = first_frame_data - uds_header_len;
    memcpy(&txf.data[2 + uds_header_len], data, copy1);
    txf.can_dlc = 8;
    printf("[LOG] [ISOTP] first frame sent: ");
    for (int i = 0; i < txf.can_dlc; ++i) printf("%02X ", txf.data[i]);
    printf("\n");
    write(s, &txf, sizeof(struct can_frame));
    // 2. wait for FC frame
    if (!wait_fc_frame(s, 1000)) {
        printf("[LOG] [ISOTP] FC frame not received; aborting multi-frame send\n");
        return;
    }
    // 3. send consecutive frames
    size_t sent = copy1;
    uint8_t sn = 1;
    while (sent < data_len) {
        txf.data[0] = 0x20 | (sn & 0x0F);
        size_t remain = data_len - sent;
        size_t chunk = remain > 7 ? 7 : remain;
        memcpy(&txf.data[1], data + sent, chunk);
        txf.can_dlc = 1 + chunk;
        printf("[LOG] [ISOTP] consecutive frame SN=%d sent: ", sn);
        for (int i = 0; i < txf.can_dlc; ++i) printf("%02X ", txf.data[i]);
        printf("\n");
        write(s, &txf, sizeof(struct can_frame));
        sent += chunk;
        sn = (sn + 1) & 0x0F;
        usleep(1000 * 10);
    }
}

// Handle 0x22 service
int handle_read_data_by_identifier(uint8_t *req, int req_len, uint8_t *resp, int *resp_len) {
    if (req_len < 3) {
        printf("[LOG] 0x22 request length too short: %d\n", req_len);
        return -1;
    }
    uint16_t did = (req[1] << 8) | req[2];
    printf("[LOG] 0x22 service, DID=0x%04X, security state: %s, security level: %d\n",
           did, security_unlocked ? "unlocked" : "locked", security_level);

    if (did == BOOT_DID) {
        if (!boot_window_active || time(NULL) > boot_window_deadline) {
            printf("[LOG] hidden reset DID unavailable or expired\n");
            boot_window_active = 0;
            resp[0] = NEG_RESPONSE;
            resp[1] = 0x22;
            resp[2] = NRC_CONDITIONS_NOT_CORRECT;
            *resp_len = 3;
            return 0;
        }
        printf("[LOG] returning hidden reset DID\n");
        boot_window_active = 0;
        return 6;
    } else if (did == 0xF190) { // public flag - no security access required
        printf("[LOG] returning public DID\n");
        // multi-frame send
        return 2; // special return value handled by the main loop
    } else if (did == 0xC1C2) { // secure flag - requires security access
        if (!security_unlocked) {
            printf("[LOG] attempted to access secure DID while security access is locked\n");
            resp[0] = NEG_RESPONSE;
            resp[1] = 0x22;
            resp[2] = NRC_SECURITY_ACCESS_DENIED;
            *resp_len = 3;
            return 0;
        }
        printf("[LOG] returning secure DID\n");
        // multi-frame send
        return 3; // special return value handled by the main loop
    } else if (did == 0xD1D2) { // advanced flag - requires level 3 security access
        if (security_level < 3) {
            printf("[LOG] attempted to access advanced DID with insufficient security level (current: %d, required: 3)\n", security_level);
            resp[0] = NEG_RESPONSE;
            resp[1] = 0x22;
            resp[2] = NRC_SECURITY_ACCESS_DENIED;
            *resp_len = 3;
            return 0;
        }
        printf("[LOG] returning advanced DID\n");
        // multi-frame send
        return 4; // special return value handled by the main loop
    }
    printf("[LOG] unknown DID: 0x%04X\n", did);
    return -1;
}

// Handle 0x10 service - DiagnosticSessionControl
int handle_diagnostic_session_control(uint8_t *req, int req_len, uint8_t *resp, int *resp_len) {
    if (req_len < 2) {
        printf("[LOG] 0x10 request length too short: %d\n", req_len);
        return -1;
    }
    uint8_t session_type = req[1];
    printf("[LOG] 0x10 service, session type=0x%02X\n", session_type);

    if (session_type == 0x01) { // default session
        current_session = 0x01;
        pending_seed_level = 0;
        // Note: security access state is preserved across session switches
        // Only an ECU restart resets security state
        printf("[LOG] switched to default session; security state preserved\n");
        last_tester_present_time = 0; // default session does not need keepalive
        resp[0] = 0x50; // positive response
        resp[1] = 0x01; // session type
        resp[2] = 0x00; // p2_server_max (50ms)
        resp[3] = 0x32; // p2_server_max (50ms)
        *resp_len = 4;
        return 0;
    } else if (session_type == 0x02) { // programming session
        current_session = 0x02;
        pending_seed_level = 0;
        printf("[LOG] switched to programming session\n");
        last_tester_present_time = time(NULL); // entered a non-default session; initializing timer
        resp[0] = 0x50; // positive response
        resp[1] = 0x02; // session type
        resp[2] = 0x00; // p2_server_max (50ms)
        resp[3] = 0x32; // p2_server_max (50ms)
        *resp_len = 4;
        return 0;
    } else {
        printf("[LOG] unsupported session type: 0x%02X\n", session_type);
        resp[0] = NEG_RESPONSE;
        resp[1] = 0x10;
        resp[2] = NRC_SUBFUNCTION_NOT_SUPPORTED;
        *resp_len = 3;
        return 0;
    }
}

// Handle 0x11 service - ECUReset
int handle_ecu_reset(uint8_t *req, int req_len, uint8_t *resp, int *resp_len) {
    if (req_len < 2) {
        printf("[LOG] 0x11 request length too short: %d\n", req_len);
        resp[0] = NEG_RESPONSE;
        resp[1] = 0x11;
        resp[2] = NRC_INCORRECT_LENGTH;
        *resp_len = 3;
        return 0;
    }

    uint8_t raw_subfunction = req[1];
    uint8_t suppress_response = raw_subfunction & 0x80;
    uint8_t reset_type = raw_subfunction & 0x7F;
    printf("[LOG] 0x11 service, raw subfunction=0x%02X, reset type=0x%02X, suppress=%d\n",
           raw_subfunction, reset_type, suppress_response ? 1 : 0);

    if (reset_type == 0x04 || reset_type == 0x05) {
        if (current_session != 0x02) {
            printf("[LOG] rapid power-down control denied; programming session required (current: 0x%02X)\n", current_session);
            resp[0] = NEG_RESPONSE;
            resp[1] = 0x11;
            resp[2] = NRC_SUBFUNCTION_NOT_SUPPORTED_IN_SESSION;
            *resp_len = 3;
            return 0;
        }
        if (security_level < 5) {
            printf("[LOG] rapid power-down control denied; level 5 SecurityAccess required (current: %d)\n", security_level);
            resp[0] = NEG_RESPONSE;
            resp[1] = 0x11;
            resp[2] = NRC_SECURITY_ACCESS_DENIED;
            *resp_len = 3;
            return 0;
        }

        rapid_power_down_enabled = (reset_type == 0x04);
        printf("[LOG] rapid power-down mode %s\n", rapid_power_down_enabled ? "enabled" : "disabled");
        resp[0] = 0x51;
        resp[1] = raw_subfunction;
        *resp_len = suppress_response ? 0 : 2;
        return 0;
    }

    if (reset_type == 0x01) { // HardReset
        if (current_session != 0x02) {
            printf("[LOG] hard reset denied; programming session required (current: 0x%02X)\n", current_session);
            resp[0] = NEG_RESPONSE;
            resp[1] = 0x11;
            resp[2] = NRC_SUBFUNCTION_NOT_SUPPORTED_IN_SESSION;
            *resp_len = 3;
            return 0;
        }
        if (security_level < 5) {
            printf("[LOG] hard reset denied; level 5 SecurityAccess required (current: %d)\n", security_level);
            resp[0] = NEG_RESPONSE;
            resp[1] = 0x11;
            resp[2] = NRC_SECURITY_ACCESS_DENIED;
            *resp_len = 3;
            return 0;
        }
        if (!rapid_power_down_enabled || !suppress_response) {
            printf("[LOG] hard reset denied; rapid_pd=%d suppress=%d\n",
                   rapid_power_down_enabled, suppress_response ? 1 : 0);
            resp[0] = NEG_RESPONSE;
            resp[1] = 0x11;
            resp[2] = NRC_CONDITIONS_NOT_CORRECT;
            *resp_len = 3;
            return 0;
        }

        printf("[LOG] silent hard reset accepted; arming hidden reset DID and restarting\n");
        write_boot_marker();
        *resp_len = 0;
        return 5; // main loop exits; supervisor restarts the service
    }

    printf("[LOG] unsupported reset type: 0x%02X\n", reset_type);
    resp[0] = NEG_RESPONSE;
    resp[1] = 0x11;
    resp[2] = NRC_SUBFUNCTION_NOT_SUPPORTED;
    *resp_len = 3;
    return 0;
}

// Handle 0x27 service
int handle_security_access(uint8_t *req, int req_len, uint8_t *resp, int *resp_len) {
    if (req_len < 2) {
        printf("[LOG] 0x27 request length too short: %d\n", req_len);
        return -1;
    }
    uint8_t subfunc = req[1];
    uint8_t level = subfunc & 0xFE; // get security level by clearing the odd/even bit
    uint8_t is_request = subfunc & 0x01; // determine whether this is a seed request or key submission

    printf("[LOG] 0x27 service, subfunc=0x%02X, level=%d, type=%s\n",
           subfunc, level, is_request ? "request seed" : "submit key");

    // check session requirement
    if ((subfunc == 0x03 || subfunc == 0x04) && current_session != 0x02) { // level 3 requires programming session
        printf("[LOG] level 3 SecurityAccess requires programming session\n");
        resp[0] = NEG_RESPONSE;
        resp[1] = 0x27;
        resp[2] = NRC_SUBFUNCTION_NOT_SUPPORTED_IN_SESSION;
        *resp_len = 3;
        return 0;
    }

    if ((subfunc == 0x05 || subfunc == 0x06) && current_session != 0x02) { // level 5 also requires programming session
        printf("[LOG] level 5 SecurityAccess requires programming session\n");
        resp[0] = NEG_RESPONSE;
        resp[1] = 0x27;
        resp[2] = NRC_SUBFUNCTION_NOT_SUPPORTED_IN_SESSION;
        *resp_len = 3;
        return 0;
    }

    if ((subfunc == 0x05 || subfunc == 0x06) && security_level < 3) {
        printf("[LOG] level 5 SecurityAccess requires level 3 first (current: %d)\n", security_level);
        resp[0] = NEG_RESPONSE;
        resp[1] = 0x27;
        resp[2] = NRC_SECURITY_ACCESS_DENIED;
        *resp_len = 3;
        return 0;
    }

    if (is_request) { // request seed
        if (subfunc == 0x01) { // level 1 seed request
            g_seed = generate_seed();
            pending_seed_level = 1;
            printf("[LOG] level 1 generated seed: 0x%08X\n", g_seed);
            resp[0] = 0x67;
            resp[1] = 0x01;
            resp[2] = (g_seed >> 24) & 0xFF;
            resp[3] = (g_seed >> 16) & 0xFF;
            resp[4] = (g_seed >> 8) & 0xFF;
            resp[5] = g_seed & 0xFF;
            *resp_len = 6;
            return 0;
        } else if (subfunc == 0x03) { // level 3 seed request
            g_seed = generate_seed();
            pending_seed_level = 3;
            printf("[LOG] level 3 generated seed: 0x%08X\n", g_seed);
            resp[0] = 0x67;
            resp[1] = 0x03; // level 3 seed request
            resp[2] = (g_seed >> 24) & 0xFF;
            resp[3] = (g_seed >> 16) & 0xFF;
            resp[4] = (g_seed >> 8) & 0xFF;
            resp[5] = g_seed & 0xFF;
            *resp_len = 6;
            return 0;
        } else if (subfunc == 0x05) { // level 5 seed request
            g_seed = generate_seed();
            pending_seed_level = 5;
            printf("[LOG] level 5 generated seed: 0x%08X\n", g_seed);
            resp[0] = 0x67;
            resp[1] = 0x05; // level 5 seed request
            resp[2] = (g_seed >> 24) & 0xFF;
            resp[3] = (g_seed >> 16) & 0xFF;
            resp[4] = (g_seed >> 8) & 0xFF;
            resp[5] = g_seed & 0xFF;
            *resp_len = 6;
            return 0;
        } else {
            printf("[LOG] unsupported SecurityAccess subfunction: 0x%02X\n", subfunc);
            resp[0] = NEG_RESPONSE;
            resp[1] = 0x27;
            resp[2] = NRC_SUBFUNCTION_NOT_SUPPORTED;
            *resp_len = 3;
            return 0;
        }
    } else { // submit key
        if (req_len < 6) {
            printf("[LOG] key length too short\n");
            return -1;
        }

        uint32_t key = (req[2] << 24) | (req[3] << 16) | (req[4] << 8) | req[5];

        if (subfunc == 0x02) { // level 1 send key
            if (pending_seed_level != 1) {
                printf("[LOG] level 1 key submitted without matching seed request\n");
                resp[0] = NEG_RESPONSE;
                resp[1] = 0x27;
                resp[2] = NRC_REQUEST_SEQUENCE_ERROR;
                *resp_len = 3;
                return 0;
            }
            uint32_t expected_key = calc_key(g_seed);
            printf("[LOG] level 1 received key: 0x%08X, current seed: 0x%08X, expected key: 0x%08X\n",
                   key, g_seed, expected_key);
            if (key == expected_key) {
                security_level = 1;
                security_unlocked = 1;
                pending_seed_level = 0;
                printf("[LOG] level 1 SecurityAccess unlocked\n");
                resp[0] = 0x67;
                resp[1] = 0x02;
                *resp_len = 2;
                return 0;
            } else {
                pending_seed_level = 0;
                printf("[LOG] level 1 SecurityAccess invalid key\n");
                resp[0] = NEG_RESPONSE;
                resp[1] = 0x27;
                resp[2] = NRC_INVALID_KEY;
                *resp_len = 3;
                return 0;
            }
        } else if (subfunc == 0x04) { // level 3 send key
            if (pending_seed_level != 3) {
                printf("[LOG] level 3 key submitted without matching seed request\n");
                resp[0] = NEG_RESPONSE;
                resp[1] = 0x27;
                resp[2] = NRC_REQUEST_SEQUENCE_ERROR;
                *resp_len = 3;
                return 0;
            }
            uint32_t expected_key = calc_key_level3(g_seed);
            printf("[LOG] level 3 received key: 0x%08X, current seed: 0x%08X, expected key: 0x%08X\n",
                   key, g_seed, expected_key);
            if (key == expected_key) {
                security_level = 3;
                security_unlocked = 1;
                pending_seed_level = 0;
                printf("[LOG] level 3 SecurityAccess unlocked\n");
                resp[0] = 0x67;
                resp[1] = 0x04;
                *resp_len = 2;
                return 0;
            } else {
                pending_seed_level = 0;
                printf("[LOG] level 3 SecurityAccess invalid key\n");
                resp[0] = NEG_RESPONSE;
                resp[1] = 0x27;
                resp[2] = NRC_INVALID_KEY;
                *resp_len = 3;
                return 0;
            }
        } else if (subfunc == 0x06) { // level 5 send key
            if (pending_seed_level != 5) {
                printf("[LOG] level 5 key submitted without matching seed request\n");
                resp[0] = NEG_RESPONSE;
                resp[1] = 0x27;
                resp[2] = NRC_REQUEST_SEQUENCE_ERROR;
                *resp_len = 3;
                return 0;
            }
            uint32_t expected_key = calc_key_level5(g_seed);
            printf("[LOG] level 5 received key: 0x%08X, current seed: 0x%08X, expected key: 0x%08X\n",
                   key, g_seed, expected_key);
            if (key == expected_key) {
                security_level = 5;
                security_unlocked = 1;
                pending_seed_level = 0;
                printf("[LOG] level 5 SecurityAccess unlocked\n");
                resp[0] = 0x67;
                resp[1] = 0x06;
                *resp_len = 2;
                return 0;
            } else {
                pending_seed_level = 0;
                printf("[LOG] level 5 SecurityAccess invalid key\n");
                resp[0] = NEG_RESPONSE;
                resp[1] = 0x27;
                resp[2] = NRC_INVALID_KEY;
                *resp_len = 3;
                return 0;
            }
        } else {
            printf("[LOG] unsupported SecurityAccess subfunction: 0x%02X\n", subfunc);
            resp[0] = NEG_RESPONSE;
            resp[1] = 0x27;
            resp[2] = NRC_SUBFUNCTION_NOT_SUPPORTED;
            *resp_len = 3;
            return 0;
        }
    }
}

// Handle 0x23 service - ReadMemoryByAddress
int handle_read_memory_by_address(uint8_t *req, int req_len, uint8_t *resp, int *resp_len) {
    printf("[LOG] ===== 0x23 ReadMemoryByAddress service start =====\n");

    // 1. basic parameter validation
    if (req_len < 5) {
        printf("[LOG] error: request length too short (%d < 5)\n", req_len);
        resp[0] = 0x7F;
        resp[1] = 0x23;
        resp[2] = 0x13; // IncorrectMessageLengthOrInvalidFormat
        *resp_len = 3;
        return 0;
    }

    // 2. security access check
    if (security_level < 5) {
        printf("[LOG] error: insufficient security level (current: %d, required: 5)\n", security_level);
        resp[0] = 0x7F;
        resp[1] = 0x23;
        resp[2] = 0x33; // SecurityAccessDenied
        *resp_len = 3;
        return 0;
    }

    printf("[LOG] security access check passed (level: %d)\n", security_level);

    // 3. parse format identifier
    uint8_t format_identifier = req[1];  // format identifier

    printf("[LOG] format identifier: 0x%02X\n", format_identifier);

    // 4. parse address and size field lengths
    uint8_t size_len = (format_identifier >> 4) & 0x0F;  // size field length (high nibble)
    uint8_t addr_len = format_identifier & 0x0F;         // address field length (low nibble)

    printf("[LOG] field lengths: address=%d bytes, size=%d bytes\n", addr_len, size_len);

    // 5. validate request length
    int expected_len = 2 + addr_len + size_len;  // 2-byte header + address + size
    if (req_len < expected_len) {
        printf("[LOG] error: request length mismatch (actual: %d, expected: %d)\n", req_len, expected_len);
        resp[0] = 0x7F;
        resp[1] = 0x23;
        resp[2] = 0x13; // IncorrectMessageLengthOrInvalidFormat
        *resp_len = 3;
        return 0;
    }

    // 6. parse memory address
    uint32_t address = 0;
    for (int i = 0; i < addr_len; i++) {
        address = (address << 8) | req[2 + i];
    }

    printf("[LOG] parsed address: 0x%08X\n", address);

    // 7. parse read size
    uint32_t size = 0;
    for (int i = 0; i < size_len; i++) {
        size = (size << 8) | req[2 + addr_len + i];
    }

    printf("[LOG] read parameters: address=0x%08X, size=%d bytes\n", address, size);

    // 8. address range check
    if (address < 0x40000000 || address > 0x4FFFFFFF) {
        printf("[LOG] error: address outside safe range (0x40000000-0x4FFFFFFF)\n");
        resp[0] = 0x7F;
        resp[1] = 0x23;
        resp[2] = 0x22; // ConditionsNotCorrect
        *resp_len = 3;
        return 0;
    }

    // 9. size limit check
    if (size > MAX_MEMORY_READ_SIZE) { // maximum 4KB
        printf("[LOG] error: read size exceeds limit (%d > 4096)\n", size);
        resp[0] = 0x7F;
        resp[1] = 0x23;
        resp[2] = 0x22; // ConditionsNotCorrect
        *resp_len = 3;
        return 0;
    }

    // 10. program memory range check
    if (address < 0x40000000 || address > 0x7FFFFFFF) {
        printf("[LOG] error: address outside program memory range\n");
        resp[0] = 0x7F;
        resp[1] = 0x23;
        resp[2] = 0x22; // ConditionsNotCorrect
        *resp_len = 3;
        return 0;
    }

    printf("[LOG] all checks passed; reading memory...\n");

    // 12. build response header
    resp[0] = 0x63; // ReadMemoryByAddress response
    resp[1] = format_identifier; // return the same format identifier

    // 13. copy memory data
    uint8_t *data_ptr = &resp[2];
    // return exactly the requested size, capped at 4KB
    uint32_t copy_size = size; // return requested size
    if (copy_size > MAX_MEMORY_READ_SIZE) copy_size = MAX_MEMORY_READ_SIZE; // redundant guard; already checked above

    // copy memory data safely with stricter checks
    if (copy_size > 0) {
        // check whether address is in the valid program memory range
        if (address < 0x40000000 || address > 0x7FFFFFFF) {
            printf("[LOG] error: address outside valid range; refusing access\n");
            resp[0] = 0x7F;
            resp[1] = 0x23;
            resp[2] = 0x22; // ConditionsNotCorrect
            *resp_len = 3;
            return 0;
        }

        // check address alignment; optional but useful for avoiding issues
        if (address % 4 != 0) {
            printf("[LOG] warning: address is unaligned (0x%08X %% 4 = %d)\n", address, address % 4);
        }

        // try to read memory safely
        printf("[LOG] trying to read memory address: 0x%08X\n", address);

        // special case: when accessing 0x40000000, return ELF file data instead of reading process memory directly
        if (address >= 0x40000000 && g_elf_data != NULL) {
            uint32_t elf_offset = address - 0x40000000;
            if (elf_offset < g_elf_size) {
                // copy from ELF data
                uint32_t available_size = g_elf_size - elf_offset;
                uint32_t actual_copy_size = (copy_size < available_size) ? copy_size : available_size;

                printf("[LOG] returning ELF file data: offset=0x%08X, size=%d bytes\n", elf_offset, actual_copy_size);
                memcpy(data_ptr, g_elf_data + elf_offset, actual_copy_size);

                // if requested size exceeds ELF file size, zero-fill the remaining bytes
                if (copy_size > actual_copy_size) {
                    printf("[LOG] zero-filling remaining %d bytes\n", copy_size - actual_copy_size);
                    memset(data_ptr + actual_copy_size, 0, copy_size - actual_copy_size);
                }
            } else {
                // outside ELF file range; returning zero bytes
                printf("[LOG] addressoutside ELF file range; returning zero bytes\n");
                memset(data_ptr, 0, copy_size);
            }
        } else {
            printf("[LOG] runtime ELF view unavailable; returning zero bytes\n");
            memset(data_ptr, 0, copy_size);
        }
    }

    // 14. print debug information
    printf("[LOG] memory read succeeded: copied %d bytes\n", copy_size);
    printf("[LOG] memory data (first 16 bytes): ");
    for (int i = 0; i < 16 && i < copy_size; i++) {
        printf("%02X ", data_ptr[i]);
    }
    printf("\n");

    // 16. set response length
    *resp_len = 2 + copy_size;

    printf("[LOG] ===== 0x23 ReadMemoryByAddress service complete =====\n");
    return 0;
}

int handle_tester_present(uint8_t *req, int req_len, uint8_t *resp, int *resp_len) {
    last_tester_present_time = time(NULL);
    printf("[LOG] received TesterPresent; updating timestamp\n");
    resp[0] = 0x7E;
    resp[1] = 0x00;
    *resp_len = 2;
    return 0;
}

int main() {
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;
    srand(time(NULL));

    // set signal handler
    signal(SIGSEGV, segfault_handler);
    signal(SIGBUS, segfault_handler);
    init_fallback_flags();
    load_runtime_flags("/opt/udsctf/flags.txt");
    load_boot_marker();

    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("socket");
        return 1;
    }
    strcpy(ifr.ifr_name, "vcan0");
    ioctl(s, SIOCGIFINDEX, &ifr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    printf("UDS server started on vcan0...\n");

    // read own ELF file content into global buffer
    FILE *elf_file = fopen("uds_server", "rb");
    if (elf_file) {
        fseek(elf_file, 0, SEEK_END);
        g_elf_size = ftell(elf_file);
        fseek(elf_file, 0, SEEK_SET);
        g_elf_data = (uint8_t *)malloc(g_elf_size);
        if (g_elf_data) {
            fread(g_elf_data, 1, g_elf_size, elf_file);
            fclose(elf_file);
            patch_runtime_elf_view();
            printf("[LOG] successfully read ELF file 'uds_server' (size: %zu bytes)\n", g_elf_size);
        } else {
            perror("malloc");
            fclose(elf_file);
            return 1;
        }
    } else {
        perror("fopen");
        return 1;
    }

    while (1) {
        // session timeout check
        if (current_session != 0x01 && last_tester_present_time > 0) {
            time_t now = time(NULL);
            if (now - last_tester_present_time > 10) {
                printf("[LOG] session timed out; falling back to default session\n");
                current_session = 0x01;
                // Note: security access state remains valid in default session
                // security_level and security_unlocked are unchanged
                last_tester_present_time = 0;
            }
        }
        int nbytes = read(s, &frame, sizeof(struct can_frame));
        if (nbytes < 0) continue;
        printf("[LOG] received CAN frame: can_id=0x%03X, dlc=%d, data=", frame.can_id, frame.can_dlc);
        for (int i = 0; i < frame.can_dlc; ++i) printf("%02X ", frame.data[i]);
        printf("\n");
        if (frame.can_id != UDS_PHYS_ID) {
            printf("[LOG] non-UDS physical-addressing frame; ignoring\n");
            continue;
        }

        // parse ISO-TP length field
        uint8_t frame_type = (frame.data[0] >> 4) & 0x0F;
        uint8_t data_length = frame.data[0] & 0x0F;
        uint8_t *uds_data = NULL;
        int uds_data_len = 0;

        printf("[LOG] ISO-TP frame type: 0x%X, data length: %d\n", frame_type, data_length);

        if (frame_type == 0x0) { // single frame
            if (data_length > 0 && data_length <= 7) {
                uds_data = &frame.data[1];
                uds_data_len = data_length;
                printf("[LOG] single-frame UDS data: ");
                for (int i = 0; i < uds_data_len; ++i) printf("%02X ", uds_data[i]);
                printf("\n");
            } else {
                printf("[LOG] invalid single-frame data length: %d\n", data_length);
                continue;
            }
        } else if (frame_type == 0x1) { // first frame
            printf("[LOG] received first frame; starting multi-frame handling\n");

            // parse first-frame length
            uint16_t total_length = ((frame.data[0] & 0x0F) << 8) | frame.data[1];
            printf("[LOG] multi-frame total length: %d bytes\n", total_length);

            // allocate buffer
            uint8_t *multi_frame_data = malloc(total_length);
            if (!multi_frame_data) {
                printf("[LOG] memory allocation failed\n");
                continue;
            }

            // copy first-frame data
            int first_frame_data_len = 6; // first-frame data length
            memcpy(multi_frame_data, &frame.data[2], first_frame_data_len);

            // send flow-control frame
            struct can_frame fc_frame;
            fc_frame.can_id = UDS_PHYS_ID;
            fc_frame.data[0] = 0x30; // flow-control frame
            fc_frame.data[1] = 0x00; // block size
            fc_frame.data[2] = 0x00; // STmin
            fc_frame.can_dlc = 3;
            write(s, &fc_frame, sizeof(struct can_frame));
            printf("[LOG] send flow-control frame\n");

            // receive consecutive frames
            int received_len = first_frame_data_len;
            uint8_t sn = 1;

            while (received_len < total_length) {
                fd_set cf_readfds;
                struct timeval cf_tv;
                FD_ZERO(&cf_readfds);
                FD_SET(s, &cf_readfds);
                cf_tv.tv_sec = 1;
                cf_tv.tv_usec = 0;

                int ready = select(s + 1, &cf_readfds, NULL, NULL, &cf_tv);
                if (ready <= 0) {
                    printf("[LOG] timed out waiting for consecutive frame\n");
                    break;
                }

                int nbytes = read(s, &frame, sizeof(struct can_frame));
                if (nbytes < 0) break;

                if (frame.can_id == UDS_PHYS_ID) {
                    uint8_t cf_frame_type = (frame.data[0] >> 4) & 0x0F;
                    if (cf_frame_type == 0x2) { // consecutive frame
                        uint8_t received_sn = frame.data[0] & 0x0F;
                        if (received_sn == sn) {
                            int cf_data_len = frame.can_dlc - 1;
                            int copy_len = (total_length - received_len < cf_data_len) ?
                                          (total_length - received_len) : cf_data_len;

                            memcpy(multi_frame_data + received_len, &frame.data[1], copy_len);
                            received_len += copy_len;
                            sn = (sn + 1) & 0x0F;

                            printf("[LOG] received consecutive frame SN=%d, received %d/%d bytes\n",
                                   received_sn, received_len, total_length);
                        }
                    }
                }
            }

            if (received_len == total_length) {
                uds_data = multi_frame_data;
                uds_data_len = total_length;
                printf("[LOG] multi-frame receive complete, UDS data: ");
                for (int i = 0; i < uds_data_len; ++i) printf("%02X ", uds_data[i]);
                printf("\n");
            } else {
                printf("[LOG] multi-frame receive failed\n");
                free(multi_frame_data);
                continue;
            }
        } else if (frame_type == 0x2) { // consecutive frame received alone
            printf("[LOG] received consecutive frame outside first-frame handling\n");
            continue;
        } else if (frame_type == 0x3) { // flow-control frame
            printf("[LOG] received flow-control frame; ignoring\n");
            continue;
        } else {
            printf("[LOG] unknown frame type: 0x%X\n", frame_type);
            continue;
        }

        if (uds_data == NULL || uds_data_len == 0) {
            printf("[LOG] no valid UDS data\n");
            continue;
        }

        uint8_t resp[MAX_MEMORY_READ_SIZE + 8];
        int resp_len = 0;
        int handled = 0;

        if (uds_data[0] == 0x10) {
            handled = handle_diagnostic_session_control(uds_data, uds_data_len, resp, &resp_len);
        } else if (uds_data[0] == 0x11) {
            handled = handle_ecu_reset(uds_data, uds_data_len, resp, &resp_len);
        } else if (uds_data[0] == 0x22) {
            handled = handle_read_data_by_identifier(uds_data, uds_data_len, resp, &resp_len);
            if (handled == 2) {
                uint8_t did[2] = {uds_data[1], uds_data[2]};
                const char *flag = get_flag(FLAG_F190);
                send_isotp_response(s, 0x62, did, flag, strlen(flag));
                continue;
            } else if (handled == 3) {
                uint8_t did[2] = {uds_data[1], uds_data[2]};
                const char *flag = get_flag(FLAG_C1C2);
                send_isotp_response(s, 0x62, did, flag, strlen(flag));
                continue;
            } else if (handled == 4) {
                uint8_t did[2] = {uds_data[1], uds_data[2]};
                const char *flag = get_flag(FLAG_D1D2);
                send_isotp_response(s, 0x62, did, flag, strlen(flag));
                continue;
            } else if (handled == 6) {
                uint8_t did[2] = {uds_data[1], uds_data[2]};
                const char *flag = get_flag(FLAG_BOOT);
                send_isotp_response(s, 0x62, did, flag, strlen(flag));
                continue;
            }
        } else if (uds_data[0] == 0x23) {
            handled = handle_read_memory_by_address(uds_data, uds_data_len, resp, &resp_len);
        } else if (uds_data[0] == 0x27) {
            handled = handle_security_access(uds_data, uds_data_len, resp, &resp_len);
        } else if (uds_data[0] == 0x3E) {
            handled = handle_tester_present(uds_data, uds_data_len, resp, &resp_len);
        } else {
            printf("[LOG] unimplemented service ID: 0x%02X\n", uds_data[0]);
        }

        if (handled == 0 || handled == 5) {
            // check response length; use multi-frame send if it exceeds the single-frame limit
            if (resp_len == 0) {
                printf("[LOG] positive response suppressed; no direct response frame sent\n");
            } else if (resp_len <= 7) {
                // single-frame send
                struct can_frame txf;
                txf.can_id = UDS_RESP_ID;
                txf.data[0] = resp_len; // single frame, length field
                memcpy(&txf.data[1], resp, resp_len);
                txf.can_dlc = 1 + resp_len;
                printf("[LOG] sending single-frame response: can_id=0x%03X, dlc=%d, data=", txf.can_id, txf.can_dlc);
                for (int i = 0; i < txf.can_dlc; ++i) printf("%02X ", txf.data[i]);
                printf("\n");
                write(s, &txf, sizeof(struct can_frame));
            } else {
                // multi-frame send
                printf("[LOG] response length exceeds single-frame limit (%d bytes); using multi-frame send\n", resp_len);
                send_isotp_response_raw(s, resp, resp_len);
            }

            if (handled == 5) {
                printf("[LOG] restarting UDS server...\n");
                exit(0); // exit process; supervisor restarts the service
            }
        } else if (handled != 0) {
            printf("[LOG] unhandled or invalid request\n");
        }

        // free multi-frame data buffer
        if (frame_type == 0x1 && uds_data != NULL) {
            free(uds_data);
        }
    }
    close(s);
    return 0;
}
