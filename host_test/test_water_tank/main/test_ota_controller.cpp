// host_test/test_water_tank/main/test_ota_controller.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ota_controller.hpp"
#include "mock_i_wifi_manager.hpp"
#include "mock_i_ota_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_i_ota_trigger.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;

class OtaControllerTest : public ::testing::Test
{
protected:
    NiceMock<wifi_manager::MockWiFiManager> mock_wifi;
    NiceMock<MockOtaManager> mock_ota;
    NiceMock<idf_hals::MockHalFreertos> mock_rtos;

    OtaControllerConfig config;
    OtaController controller{mock_wifi, mock_ota, mock_rtos, config};

    // Helper methods to access private members of OtaController
    void set_state(OtaController::State state) {
        controller.state_ = state;
    }
    OtaController::State get_state() const {
        return controller.state_;
    }
    void set_connected_by_us(bool val) {
        controller.connected_by_us_ = val;
    }
    bool get_connected_by_us() const {
        return controller.connected_by_us_;
    }
    void set_triggers(std::vector<IOtaTrigger*> triggers) {
        controller.triggers_ = triggers;
    }
    const std::vector<IOtaTrigger*>& get_triggers() const {
        return controller.triggers_;
    }
    void set_task(TaskHandle_t task) {
        controller.task_ = task;
    }
    TaskHandle_t get_task() const {
        return controller.task_;
    }
    void run_fsm() {
        controller.run_fsm();
    }
};

TEST_F(OtaControllerTest, WifiAlreadyConnectedSkipsConnect)
{
    set_state(OtaController::State::WIFI_CONNECTING);

    EXPECT_CALL(mock_wifi, get_state()).WillOnce(Return(wifi_manager::State::CONNECTED_GOT_IP));
    EXPECT_CALL(mock_wifi, connect(_)).Times(0);

    run_fsm();

    EXPECT_EQ(get_state(), OtaController::State::OTA_RUNNING);
    EXPECT_FALSE(get_connected_by_us());
}

TEST_F(OtaControllerTest, WifiConnectsSuccessfully)
{
    set_state(OtaController::State::WIFI_CONNECTING);

    EXPECT_CALL(mock_wifi, get_state()).WillOnce(Return(wifi_manager::State::STARTED));
    EXPECT_CALL(mock_wifi, connect(config.wifi_connect_timeout_ms)).WillOnce(Return(ESP_OK));

    run_fsm();

    EXPECT_EQ(get_state(), OtaController::State::OTA_RUNNING);
    EXPECT_TRUE(get_connected_by_us());
}

TEST_F(OtaControllerTest, WifiConnectionFails)
{
    set_state(OtaController::State::WIFI_CONNECTING);

    EXPECT_CALL(mock_wifi, get_state()).WillOnce(Return(wifi_manager::State::STARTED));
    EXPECT_CALL(mock_wifi, connect(config.wifi_connect_timeout_ms)).WillOnce(Return(ESP_FAIL));

    run_fsm();

    EXPECT_EQ(get_state(), OtaController::State::OTA_FAILED);
}

TEST_F(OtaControllerTest, OtaStartsAndCompletesSuccessfully)
{
    set_state(OtaController::State::OTA_RUNNING);

    EXPECT_CALL(mock_ota, start_ota()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota, get_status())
        .WillOnce(Return(OtaStatus::DOWNLOADING))
        .WillOnce(Return(OtaStatus::READY_TO_RESTART));

    run_fsm();

    EXPECT_EQ(get_state(), OtaController::State::IDLE);
}

TEST_F(OtaControllerTest, OtaStartsAndFails)
{
    set_state(OtaController::State::OTA_RUNNING);

    EXPECT_CALL(mock_ota, start_ota()).WillOnce(Return(true));
    EXPECT_CALL(mock_ota, get_status())
        .WillOnce(Return(OtaStatus::DOWNLOADING))
        .WillOnce(Return(OtaStatus::FAILED));

    run_fsm();

    EXPECT_EQ(get_state(), OtaController::State::OTA_FAILED);
}

TEST_F(OtaControllerTest, OtaStartRejected)
{
    set_state(OtaController::State::OTA_RUNNING);

    EXPECT_CALL(mock_ota, start_ota()).WillOnce(Return(false));

    run_fsm();

    EXPECT_EQ(get_state(), OtaController::State::OTA_FAILED);
}

TEST_F(OtaControllerTest, CleanupDisconnectsWifiIfConnectedByUs)
{
    set_state(OtaController::State::OTA_FAILED);
    set_connected_by_us(true);

    EXPECT_CALL(mock_wifi, disconnect()).Times(1);

    run_fsm();

    EXPECT_EQ(get_state(), OtaController::State::IDLE);
    EXPECT_FALSE(get_connected_by_us());
}

TEST_F(OtaControllerTest, CleanupDoesNotDisconnectWifiIfNotConnectedByUs)
{
    set_state(OtaController::State::OTA_FAILED);
    set_connected_by_us(false);

    EXPECT_CALL(mock_wifi, disconnect()).Times(0);

    run_fsm();

    EXPECT_EQ(get_state(), OtaController::State::IDLE);
}

TEST_F(OtaControllerTest, IsBusyReturnsCorrectly)
{
    set_state(OtaController::State::IDLE);
    EXPECT_FALSE(controller.is_busy());

    set_state(OtaController::State::WIFI_CONNECTING);
    EXPECT_TRUE(controller.is_busy());

    set_state(OtaController::State::OTA_RUNNING);
    EXPECT_TRUE(controller.is_busy());

    set_state(OtaController::State::OTA_FAILED);
    EXPECT_TRUE(controller.is_busy());
}

TEST_F(OtaControllerTest, StartArmsTriggersAndCreatesTask)
{
    MockOtaTrigger mock_trigger1;
    MockOtaTrigger mock_trigger2;
    std::vector<IOtaTrigger*> triggers = {&mock_trigger1, &mock_trigger2};

    EXPECT_CALL(mock_trigger1, arm(testing::Ref(controller))).Times(1);
    EXPECT_CALL(mock_trigger2, arm(testing::Ref(controller))).Times(1);
    EXPECT_CALL(mock_rtos, task_create(_, _, _, _, _, _)).WillOnce(Return(pdPASS));

    esp_err_t err = controller.start(triggers);
    EXPECT_EQ(err, ESP_OK);

    // Stop to clear triggers before they go out of scope
    controller.stop();
}

TEST_F(OtaControllerTest, StopDisarmsTriggersAndDeleteTask)
{
    MockOtaTrigger mock_trigger1;
    std::vector<IOtaTrigger*> triggers = {&mock_trigger1};
    set_triggers(triggers);
    set_task(reinterpret_cast<TaskHandle_t>(0x1234));

    EXPECT_CALL(mock_trigger1, disarm()).Times(1);
    EXPECT_CALL(mock_rtos, task_delete(reinterpret_cast<TaskHandle_t>(0x1234))).Times(1);

    controller.stop();
    EXPECT_EQ(get_task(), nullptr);
    EXPECT_TRUE(get_triggers().empty());
}
