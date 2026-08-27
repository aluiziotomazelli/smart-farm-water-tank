// main/include/tank_command_handler.hpp
#pragma once

#include "interfaces/i_tank_command_handler.hpp"
#include "i_espnow_manager.hpp"
#include "interfaces/i_time_manager.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_hal_freertos.hpp"

/**
 * @class TankCommandHandler
 * @brief Drains incoming ESP-NOW message queue, routes commands to subsystems, and handles ACK confirmations.
 */
class TankCommandHandler : public ITankCommandHandler
{
public:
    TankCommandHandler(
        QueueHandle_t rx_queue,
        espnow::IEspNowManager& espnow,
        time_manager::ITimeManager& time_manager,
        idf_hals::ITimerHAL& sys_timer,
        idf_hals::IHalFreertos& rtos);

    /** @copydoc ITankCommandHandler::process */
    TankCommandProcessResult process(uint32_t timeout_ms = 0) override;

private:
    QueueHandle_t rx_queue_;
    espnow::IEspNowManager& espnow_;
    time_manager::ITimeManager& time_manager_;
    idf_hals::ITimerHAL& sys_timer_;
    idf_hals::IHalFreertos& rtos_;

    void process_command_message(const espnow::AppMessage& msg, TankCommandProcessResult& result);
    void send_cmd_ack(const espnow::AppMessage& msg, espnow::AckStatus status);
};
