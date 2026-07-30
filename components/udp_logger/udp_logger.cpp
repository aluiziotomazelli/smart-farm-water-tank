#include "udp_logger.hpp"
#include "esp_log.h"

namespace udp_logger {

static const char* TAG = "UdpLogger";

#if !defined(__linux__) && !defined(PROJ_HOST_TEST)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "lwip/sockets.h"

static RingbufHandle_t log_ringbuf = nullptr;
static vprintf_like_t original_vprintf = nullptr;
static int udp_sock = -1;
static struct sockaddr_in dest_addr;

static int udp_log_vprintf(const char* fmt, va_list args)
{
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);

    if (len > 0) {
        if (log_ringbuf != nullptr) {
            xRingbufferSend(log_ringbuf, buf, len, 0);
        }
    }

    if (original_vprintf != nullptr) {
        return original_vprintf(fmt, args);
    }
    return len;
}

static void udp_log_sender_task(void* pvParameters)
{
    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    
    while (true) {
        size_t item_size = 0;
        char* item = (char*)xRingbufferReceive(log_ringbuf, &item_size, portMAX_DELAY);
        if (item != nullptr) {
            if (udp_sock >= 0) {
                sendto(udp_sock, item, item_size, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
            }
            vRingbufferReturnItem(log_ringbuf, (void*)item);
        }
    }
}

void init(const std::string& dest_ip, uint16_t port)
{
    if (log_ringbuf != nullptr) {
        ESP_LOGW(TAG, "UDP Logger already initialized");
        return;
    }

    dest_addr.sin_addr.s_addr = inet_addr(dest_ip.c_str());
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    log_ringbuf = xRingbufferCreate(8192, RINGBUF_TYPE_NOSPLIT);
    if (log_ringbuf != nullptr) {
        xTaskCreate(udp_log_sender_task, "udp_log_tx", 3072, nullptr, 3, nullptr);
        original_vprintf = esp_log_set_vprintf(&udp_log_vprintf);
        ESP_LOGI(TAG, "UDP Remote Logging initialized to %s:%u", dest_ip.c_str(), port);
    } else {
        ESP_LOGE(TAG, "Failed to create ring buffer for UDP Logger");
    }
}

#else

void init(const std::string& dest_ip, uint16_t port)
{
    (void)dest_ip;
    (void)port;
    ESP_LOGI(TAG, "UDP Logger disabled on host test");
}

#endif

} // namespace udp_logger
