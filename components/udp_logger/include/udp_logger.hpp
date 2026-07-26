#pragma once

#include <cstdint>
#include <string>

namespace udp_logger {

/**
 * @brief Initializes the UDP remote logger.
 * 
 * Hijacks the ESP-IDF log vprintf function and sends logs via UDP in a background task.
 * MUST be called AFTER WiFi is connected and has an IP address.
 * 
 * @param dest_ip The destination IPv4 address (e.g. "192.168.1.23")
 * @param port The destination UDP port
 */
void init(const std::string& dest_ip, uint16_t port);

} // namespace udp_logger
