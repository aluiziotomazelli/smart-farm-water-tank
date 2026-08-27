// main/include/interfaces/i_tank_command_handler.hpp
#pragma once

#include <cstdint>

/**
 * @struct TankCommandProcessResult
 * @brief Result flags returned after listening and processing incoming ESP-NOW commands for the Water Tank node.
 */
struct TankCommandProcessResult
{
    bool time_synced{false};        ///< True if clock was synchronized from incoming SYNC_TIME packet
    bool ota_requested{false};      ///< True if START_OTA command was received
    bool reboot_requested{false};   ///< True if REBOOT command was received
    uint64_t override_sleep_us{0};  ///< Sleep duration override in microseconds (0 = no override)
};

/**
 * @class ITankCommandHandler
 * @brief Interface for draining and processing incoming ESP-NOW commands for the Water Tank node.
 */
class ITankCommandHandler
{
public:
    virtual ~ITankCommandHandler() = default;

    /**
     * @brief Waits up to timeout_ms for incoming commands, processes them and returns execution flags.
     * @param timeout_ms Timeout window in milliseconds (0 = non-blocking drain).
     * @return TankCommandProcessResult struct with status flags.
     */
    virtual TankCommandProcessResult process(uint32_t timeout_ms = 0) = 0;
};
