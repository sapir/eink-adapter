#include "espressif/esp_common.h"
#include "espressif/esp_wifi.h"
#include "espressif/esp_sta.h"
#include "espressif/user_interface.h"
#include <stdarg.h>
#include <string.h>
#include <lwip/sockets.h>
#include "esp/uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "eink.h"
#include "missing_api.h"
#include "skall.h"
#include "private_ssid_config.h"
#include "waveform.h"


#define LISTEN_PORT 3124


#define MY_UART 0


// a full frame buffer is 60KB, but we don't have that much available RAM on
// the ESP8266, so we get it in chunks
#define SCREEN_BITMAP_WIDTH 800
#define SCREEN_BITMAP_HEIGHT 600
#define SCREEN_BITMAP_X_OFS 0
#define SCREEN_BITMAP_Y_OFS 0
#define CHUNK_WIDTH 100
#define CHUNK_HEIGHT 100
#define CHUNK_OVERLAP 0

static uint8_t old_chunk[CHUNK_HEIGHT][CHUNK_WIDTH / PIXELS_PER_BYTE];
static uint8_t new_chunk[CHUNK_HEIGHT][CHUNK_WIDTH / PIXELS_PER_BYTE];


struct chunk_params {
    int x;
    int y;
    int byte_w;
};

bool get_rows_from_chunks(void *arg, int y, int x0, int x1, uint8_t *old_row,
    uint8_t *new_row)
{
    struct chunk_params *cp = arg;
    memcpy(old_row, old_chunk[y - cp->y], cp->byte_w);
    memcpy(new_row, new_chunk[y - cp->y], cp->byte_w);
    return true;
}


void handle_conn(int client_sock)
{
    printf("powering on...\n");
    eink_power_on();
    printf("here we go!\n");

    for (int y = 0; y < SCREEN_BITMAP_HEIGHT; y += CHUNK_HEIGHT - CHUNK_OVERLAP) {
        for (int x = 0; x < SCREEN_BITMAP_WIDTH; x += CHUNK_WIDTH - CHUNK_OVERLAP) {
            int w = SCREEN_BITMAP_WIDTH - x;
            if (w > CHUNK_WIDTH) w = CHUNK_WIDTH;

            int h = SCREEN_BITMAP_HEIGHT - y;
            if (h > CHUNK_HEIGHT) h = CHUNK_HEIGHT;

            int byte_w = (w+PIXELS_PER_BYTE-1)/PIXELS_PER_BYTE;

            if (!(sendall(client_sock, (void*)&x, sizeof(x))
                && sendall(client_sock, (void*)&y, sizeof(y))
                && sendall(client_sock, (void*)&w, sizeof(w))
                && sendall(client_sock, (void*)&h, sizeof(h))))
                continue;

            bool err = false;

            for (int row_y = 0; row_y < h; ++row_y) {
                if (!recvall(client_sock, old_chunk[row_y], byte_w)) {
                    err = true;
                    break;
                }
            }
            if (err)
                continue;

            for (int row_y = 0; row_y < h; ++row_y) {
                if (!recvall(client_sock, new_chunk[row_y], byte_w)) {
                    err = true;
                    break;
                }
            }
            if (err)
                continue;

            struct chunk_params cp = {
                .x = SCREEN_BITMAP_X_OFS + x,
                .y = SCREEN_BITMAP_Y_OFS + y,
                .byte_w = byte_w,
            };

            eink_update(get_rows_from_chunks, &cp,
                SCREEN_BITMAP_X_OFS + x,
                SCREEN_BITMAP_Y_OFS + y,
                SCREEN_BITMAP_X_OFS + x + w,
                SCREEN_BITMAP_Y_OFS + y + h);
        }
    }

    printf("powering off\n");
    eink_power_off();

    lwip_close(client_sock);
}

static bool connect_to_wifi(struct ip_info *info_ptr)
{
    printf("connecting to %s...\n", WIFI_SSID);

    struct sdk_station_config sta_config;
    bzero(&sta_config, sizeof(struct sdk_station_config));
    strcpy((char*)sta_config.ssid, WIFI_SSID);
    strcpy((char*)sta_config.password, WIFI_PASS);
    if (!sdk_wifi_station_set_config_current(&sta_config)) {
        printf("failed setting config\n");
        return false;
    }

    if (!sdk_wifi_station_connect()) {
        printf("failed connecting\n");
        return false;
    }

    printf("connected, starting DHCP\n");
    if (!sdk_wifi_station_dhcpc_start()) {
        printf("failed starting DHCP\n");
        return false;
    }

    printf("waiting for IP...\n");

    sdk_wifi_get_ip_info(STATION_IF, info_ptr);
    while (info_ptr->ip.addr == 0) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        sdk_wifi_get_ip_info(STATION_IF, info_ptr);
    }

    printf("got %08x\n", info_ptr->ip.addr);
    return true;
}

void print_waveform_table(void)
{
    printf("HIGH  LOW   STGE  W-0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15-B\n");
    for (int wf_stage = 0; ; ++wf_stage) {
        uint32_t ckv_high_delay_ns;
        uint32_t ckv_low_delay_ns;
        uint32_t stage_delay_us;
        get_update_waveform_timings(wf_stage,
            &ckv_high_delay_ns, &ckv_low_delay_ns, &stage_delay_us);
        if (!ckv_high_delay_ns)
            break;
        printf("%5u %5u %5u ", ckv_high_delay_ns, ckv_low_delay_ns, stage_delay_us);
        for (int i = 0; i < 16; ++i) {
            enum PIXEL_VALUE pv = get_update_waveform_value(wf_stage, WHITE, i);
            printf("  %c", (pv == PV_WHITE ? 'W' : (pv == PV_BLACK ? 'B' : '-')));
        }
        printf("\n");
    }
}

void main_thread(void *arg)
{
    print_waveform_table();

    printf("free heap size: %u\n", sdk_system_get_free_heap_size());

    struct ip_info ip_config;
    while (!connect_to_wifi(&ip_config)) {
        printf("couldn't connect!\n");
    }

    int listen_sock = lwip_socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in listen_addr = {
        .sin_family = AF_INET,
        .sin_len = sizeof(struct sockaddr_in),
        .sin_addr = { .s_addr = htonl(INADDR_ANY) },
        .sin_port = htons(LISTEN_PORT)
    };

    lwip_bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr));
    lwip_listen(listen_sock, 5);

    printf("clearing screen...\n");
    eink_power_on();
    eink_refresh(WHITE);
    eink_power_off();

    printf("listening...\n");

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int client_sock = lwip_accept(listen_sock,
            (struct sockaddr*)&client_addr, &client_addrlen);
        if (client_sock < 0) {
            printf("accept error %d\n", client_sock);
            break;
        }

        ip_addr_t addr = (ip_addr_t){ client_addr.sin_addr.s_addr };
        u16_t port = client_addr.sin_port;

        printf("got connection from " IPSTR ":%u\n",
             ip4_addr1(&addr), ip4_addr2(&addr),
             ip4_addr3(&addr), ip4_addr4(&addr),
             ntohs(port));

        handle_conn(client_sock);
    }

    lwip_close(listen_sock);

    vTaskDelete(NULL);
}


void user_init(void)
{
    sdk_wifi_set_opmode_current(STATION_MODE);

    uart_set_baud(MY_UART, 115200);
    printf("serial OK\n");

    if (!eink_setup()) {
        printf("eink setup fail\n");
        return;
    }

    printf("hw setup ok\n");

    int err = xTaskCreate(main_thread, "listen", 512, NULL, 1, NULL);
    if (pdPASS != err) {
        printf("error creating main thread: %d\n", err);
    }
}
