# Integração do OTA Manager ao Smart Farm Water Tank

## Contexto

O `ota_manager` já está registrado como `EXTRA_COMPONENT_DIRS` no `CMakeLists.txt` do projeto. A
`partitions.csv` atual já é compatível com OTA dual-bank. O objetivo é:

1. Criar **`IOtaTrigger`** e **`BootButtonOtaTrigger`** no repo `smart-farm-common` (reutilizável por todos os apps).
2. Criar **`OtaController`** em `main/` do water-tank (política de rollback específica do app).
3. Integrar tudo no `app_main` com alterações mínimas no fluxo existente.
4. Ajustar `sdkconfig.defaults.esp32c3`, `secrets.hpp`, e `CMakeLists.txt` do `main`.

---

## Open Questions

> [!IMPORTANT]
> **Pergunta 1 — Verificação pós-reboot:**
> O `handle_pending_verify()` é chamado no início do `app_main`, antes do `WaterTankApp::run()`.
> Se `check_pending_verify()` = true, confirma o firmware imediatamente (assume que o boot foi
> bem-sucedido). Se a confirmação falhar, faz rollback e reinicia.
> **Aprovado?**

> [!IMPORTANT]
> **Pergunta 2 — Timeout de conexão WiFi antes do OTA:**
> Para conectar antes do OTA, `wifi.connect(timeout_ms)` é bloqueante. Proposta: **30 segundos**.

> [!NOTE]
> **Pergunta 3 — `allow_same_version = true`** para bancada de testes. Confirmado.

> [!NOTE]
> **Pergunta 4 — `device_type`:** Usar `"water_tank"` para validação no manifest JSON.

> [!WARNING]
> **Pergunta 5 — Reflash necessário:**
> A `partitions.csv` atual já é correta, mas se o chip foi flashed antes com outra tabela de
> partições, é preciso `idf.py erase-flash && idf.py flash` para inicializar o `otadata` corretamente.

---

## Arquitetura de Componentes

```
smart-farm-common/              ← Repo standalone / submódulo
└── common/
    ├── include/
    │   ├── interfaces/
    │   │   └── i_ota_trigger.hpp           [NEW] Interface genérica
    │   └── boot_button_ota_trigger.hpp     [NEW] Implementação concreta (GPIO)
    └── src/
        └── boot_button_ota_trigger.cpp     [NEW]

smart-farm-water-tank/
├── sdkconfig.defaults.esp32c3             [MODIFY] HTTP OTA + rollback
└── main/
    ├── include/
    │   ├── secrets.hpp                    [MODIFY] adicionar #pragma once
    │   └── ota_controller.hpp             [NEW] Orquestrador específico do app
    ├── src/
    │   └── ota_controller.cpp             [NEW]
    ├── CMakeLists.txt                     [MODIFY] adicionar ota_manager
    └── main.cpp                           [MODIFY] integrar OtaController
```

---

## Proposed Changes

---

### Componente 1: `smart-farm-common` (repo standalone)

> **Workflow:** Editar em `/home/german/dev/workspaces/smart-farm-common`, commitar e fazer push.
> Depois atualizar o ponteiro do submodule no water-tank (ver seção "Workflow de Publicação").

#### [MODIFY] [CMakeLists.txt (common)](file:///home/german/dev/workspaces/smart-farm-common/common/CMakeLists.txt)

Adicionar o novo `.cpp` à lista de `SRCS` e `driver` aos `REQUIRES`:

```cmake
idf_component_register(
    SRCS
        "src/boot_button_ota_trigger.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
)
```

#### [NEW] `common/include/interfaces/i_ota_trigger.hpp`

Interface pura — sem dependências de hardware. Reutilizável por qualquer app da farm:

```cpp
// common/include/interfaces/i_ota_trigger.hpp
#pragma once

#include "esp_err.h"

/**
 * @brief Abstract interface for OTA trigger sources.
 *
 * Implementations can be: boot button, ESP-NOW command, MQTT message, etc.
 * The OtaController polls this interface to decide when to start an OTA update.
 */
class IOtaTrigger
{
public:
    virtual ~IOtaTrigger() = default;

    /**
     * @brief Initialize the trigger hardware or source.
     * @return ESP_OK on success.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Returns true if an OTA trigger event has been detected.
     *
     * This method is polled periodically. Implementations are responsible
     * for debouncing and edge detection as appropriate for their source.
     */
    virtual bool is_triggered() = 0;
};
```

#### [NEW] `common/include/boot_button_ota_trigger.hpp`

```cpp
// common/include/boot_button_ota_trigger.hpp
#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "interfaces/i_ota_trigger.hpp"

/**
 * @brief OTA trigger implementation using a physical GPIO button.
 *
 * Polls the configured GPIO pin. Returns true when the button is detected
 * as pressed (active LOW with internal pull-up enabled).
 */
class BootButtonOtaTrigger : public IOtaTrigger
{
public:
    /** @brief Constructs the trigger for the given GPIO pin. */
    explicit BootButtonOtaTrigger(gpio_num_t gpio);

    /** @copydoc IOtaTrigger::init() */
    esp_err_t init() override;

    /** @copydoc IOtaTrigger::is_triggered() */
    bool is_triggered() override;

private:
    gpio_num_t gpio_;
};
```

#### [NEW] `common/src/boot_button_ota_trigger.cpp`

```cpp
// common/src/boot_button_ota_trigger.cpp
#include "boot_button_ota_trigger.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

BootButtonOtaTrigger::BootButtonOtaTrigger(gpio_num_t gpio) : gpio_(gpio) {}

esp_err_t BootButtonOtaTrigger::init()
{
    gpio_set_direction(gpio_, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_, GPIO_PULLUP_ONLY);
    return ESP_OK;
}

bool BootButtonOtaTrigger::is_triggered()
{
    if (gpio_get_level(gpio_) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50)); // debounce
        if (gpio_get_level(gpio_) == 0) {
            return true;
        }
    }
    return false;
}
```

---

### Componente 2: `smart-farm-water-tank/main/`

#### [MODIFY] [sdkconfig.defaults.esp32c3](file:///home/german/dev/workspaces/smart-farm-water-tank/sdkconfig.defaults.esp32c3)

Adicionar as configurações obrigatórias para OTA via HTTP com rollback:

```
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y
CONFIG_PARTITION_TABLE_CUSTOM=y
```

#### [NO CHANGE] [partitions.csv](file:///home/german/dev/workspaces/smart-farm-water-tank/partitions.csv)

A tabela atual já é idêntica ao exemplo do `ota_manager`. **Nenhuma alteração necessária.**

#### [MODIFY] [secrets.hpp](file:///home/german/dev/workspaces/smart-farm-water-tank/main/include/secrets.hpp)

Adicionar `#pragma once` (ausente no arquivo atual):

```cpp
// main/include/secrets.hpp
#pragma once

#define WIFI_SSID "TP-LINK_73D2B8"
#define WIFI_PASS "64596969"
#define SERVER_URL "http://<your-ip>:8070/manifests/water_tank.json"
```

#### [MODIFY] [CMakeLists.txt (main)](file:///home/german/dev/workspaces/smart-farm-water-tank/main/CMakeLists.txt)

Adicionar `ota_manager` e o novo `.cpp`:

```cmake
idf_component_register(
    SRCS
        "main.cpp"
        "src/water_tank_app.cpp"
        "src/water_tank_nvs.cpp"
        "src/water_tank_logic.cpp"
        "src/tank_geometry.cpp"
        "src/ota_controller.cpp"    # NEW
    INCLUDE_DIRS
        "include"
        "."
    REQUIRES
        nvs_core
        common
        ultrasonic_sensor
        floatswitch
        espnow_manager
        power_control
        wifi_manager
        ota_manager                 # NEW
)
```

#### [NEW] [ota_controller.hpp](file:///home/german/dev/workspaces/smart-farm-water-tank/main/include/ota_controller.hpp)

Orquestrador com a política de rollback específica do `water_tank`:

```cpp
// main/include/ota_controller.hpp
#pragma once

#include "esp_err.h"
#include "ota_manager.hpp"
#include "interfaces/i_ota_trigger.hpp"
#include "interfaces/i_wifi_manager.hpp"

/**
 * @brief Configuration for the OtaController.
 */
struct OtaControllerConfig {
    uint32_t wifi_connect_timeout_ms;  ///< Max wait for WiFi IP acquisition
    uint32_t trigger_poll_interval_ms; ///< Polling interval for the trigger
    uint32_t trigger_timeout_ms;       ///< 0 = wait forever; >0 = give up after N ms
};

/**
 * @brief Orchestrates the OTA update flow for the Water Tank application.
 *
 * Responsibilities:
 * - On boot: check if the new firmware needs confirmation (pending verify).
 * - When running: poll the trigger, connect WiFi, invoke OtaManager::start_ota().
 *
 * The rollback policy (what counts as a valid boot) is app-specific and
 * defined here, keeping it decoupled from the generic OtaManager.
 */
class OtaController
{
public:
    OtaController(
        OtaManager& ota_manager,
        wifi_manager::IWiFiManager& wifi,
        IOtaTrigger& trigger,
        const OtaControllerConfig& config);

    /**
     * @brief Check and handle a firmware pending-verification state.
     *
     * If the current firmware is in PENDING_VERIFY state (just rebooted after OTA),
     * confirms it as valid. If confirmation fails, triggers rollback and reboot.
     *
     * Call this ONCE at startup, before the main application loop.
     */
    void handle_pending_verify();

    /**
     * @brief Poll the trigger and, if fired, connect WiFi and run OTA.
     *
     * Blocks until the trigger fires or trigger_timeout_ms elapses.
     *
     * @return ESP_OK        OTA started successfully (device will reboot).
     * @return ESP_ERR_TIMEOUT trigger window expired, no OTA performed.
     * @return ESP_FAIL      WiFi connection or OTA start failed.
     */
    esp_err_t wait_and_run();

private:
    OtaManager& ota_manager_;
    wifi_manager::IWiFiManager& wifi_;
    IOtaTrigger& trigger_;
    OtaControllerConfig config_;

    esp_err_t connect_wifi();
};
```

#### [NEW] [ota_controller.cpp](file:///home/german/dev/workspaces/smart-farm-water-tank/main/src/ota_controller.cpp)

```cpp
// main/src/ota_controller.cpp
#include "ota_controller.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "wifi_types.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "OtaController";

OtaController::OtaController(
    OtaManager& ota_manager,
    wifi_manager::IWiFiManager& wifi,
    IOtaTrigger& trigger,
    const OtaControllerConfig& config)
    : ota_manager_(ota_manager)
    , wifi_(wifi)
    , trigger_(trigger)
    , config_(config)
{
}

void OtaController::handle_pending_verify()
{
    if (!ota_manager_.check_pending_verify()) {
        return;
    }

    ESP_LOGI(TAG, "New firmware pending verification. Confirming as valid.");
    if (ota_manager_.confirm_app_valid()) {
        ESP_LOGI(TAG, "Firmware confirmed successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to confirm firmware. Triggering rollback.");
        ota_manager_.rollback_and_reboot();
    }
}

esp_err_t OtaController::wait_and_run()
{
    ESP_LOGI(TAG, "OTA window open. Waiting for trigger (%lu ms)...",
             config_.trigger_timeout_ms);

    uint32_t elapsed_ms = 0;
    while (true) {
        if (trigger_.is_triggered()) {
            ESP_LOGI(TAG, "OTA trigger received.");
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(config_.trigger_poll_interval_ms));
        elapsed_ms += config_.trigger_poll_interval_ms;

        if (config_.trigger_timeout_ms > 0 && elapsed_ms >= config_.trigger_timeout_ms) {
            ESP_LOGI(TAG, "OTA trigger window expired. Proceeding with normal boot.");
            return ESP_ERR_TIMEOUT;
        }
    }

    esp_err_t err = connect_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed before OTA: %s", esp_err_to_name(err));
        return err;
    }

    if (!ota_manager_.start_ota()) {
        ESP_LOGE(TAG, "Failed to start OTA process.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA in progress. Waiting for completion...");
    while (true) {
        OtaStatus status = ota_manager_.get_status();
        if (status == OtaStatus::READY_TO_RESTART || status == OtaStatus::FAILED) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (ota_manager_.get_status() == OtaStatus::FAILED) {
        ESP_LOGE(TAG, "OTA failed.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA complete. Device will reboot.");
    return ESP_OK;
}

esp_err_t OtaController::connect_wifi()
{
    wifi_manager::State state = wifi_.get_state();

    if (state == wifi_manager::State::CONNECTED_GOT_IP) {
        ESP_LOGI(TAG, "WiFi already connected.");
        return ESP_OK;
    }

    if (state == wifi_manager::State::STARTED) {
        ESP_LOGI(TAG, "WiFi started, connecting...");
        return wifi_.connect(config_.wifi_connect_timeout_ms);
    }

    ESP_LOGE(TAG, "WiFi not in a connectable state: %d", static_cast<int>(state));
    return ESP_ERR_INVALID_STATE;
}
```

#### [MODIFY] [main.cpp](file:///home/german/dev/workspaces/smart-farm-water-tank/main/main.cpp)

Alterações mínimas e bem delimitadas:

**1. Includes novos** (adicionar após os includes existentes):
```cpp
#include "secrets.hpp"
#include "ota_manager.hpp"
#include "boot_button_ota_trigger.hpp"
#include "ota_controller.hpp"
```

**2. Instâncias estáticas** (adicionar após as demais instâncias globais):
```cpp
// OtaManager — HAL implementations
static HttpClient      http_client;
static ManifestParser  manifest_parser;
static OtaSession      ota_session;
static System          ota_system;
static TaskScheduler   task_scheduler;
static RollbackManager rollback_manager;

static OtaDependencies ota_deps = {
    .http_client      = http_client,
    .manifest_parser  = manifest_parser,
    .ota_session      = ota_session,
    .system           = ota_system,
    .task_scheduler   = task_scheduler,
    .rollback_manager = rollback_manager,
};

static OtaConfig ota_config{
    .device_type        = "water_tank",
    .manifest_url       = SERVER_URL,
    .task_stack_size    = 8192,
    .task_priority      = 5,
    .transport          = {.manifest_timeout_ms = 30000, .firmware_timeout_ms = 30000},
    .security           = {.allow_http_during_development = true},
    .allow_same_version = true,  // permite mesma versao em bancada de testes
    .restart_on_success = true,
};

static OtaManager ota_manager(ota_deps);

// OTA trigger: botão BOOT (GPIO 9 no XIAO-ESP32-C3)
static BootButtonOtaTrigger ota_trigger(BOOT_BUTTON_GPIO);

static OtaControllerConfig ota_ctrl_config{
    .wifi_connect_timeout_ms  = 30000,
    .trigger_poll_interval_ms = 100,
    .trigger_timeout_ms       = 10000, // janela de 10s para pressionar o botão
};
```

**3. Em `setup_hardware()`** — adicionar credenciais WiFi antes do `wifi.start()`:
```cpp
// ANTES de wifi.start() — define as credenciais para OTA
if ((err = wifi.add_credentials(WIFI_SSID, WIFI_PASS)) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set WiFi credentials: %s", esp_err_to_name(err));
    // nao e fatal: credenciais podem ja estar no NVS
}
```

**4. Em `app_main()`** — inserir entre `setup_hardware()` e `WaterTankApp app(...)`:
```cpp
// Inicializar OtaManager e gatilho
ota_manager.init(ota_config);
ota_trigger.init();

// Referência ao WiFi (já obtida pelo singleton)
auto& wifi = wifi_manager::WiFiManager::get_instance();
OtaController ota_controller(ota_manager, wifi, ota_trigger, ota_ctrl_config);

// Verificar rollback state logo no boot
ota_controller.handle_pending_verify();

// Janela de OTA: pressionar BOOT para disparar update
ota_controller.wait_and_run();
```

---

## Fluxo Completo

```
app_main()
├── setup_hardware()
│   ├── wifi.init()
│   ├── wifi.add_credentials(WIFI_SSID, WIFI_PASS)
│   ├── wifi.start()          ← driver ativo, NÃO conectado ainda
│   ├── float_switch.init()
│   ├── nvs.init_partition()
│   ├── espnow.init()
│   ├── power.init() + turn_on()
│   └── sensor_us.init()
│
├── ota_manager.init(ota_config)
├── ota_trigger.init()
│
├── ota_controller.handle_pending_verify()
│   └── se PENDING_VERIFY → confirm_app_valid() ou rollback_and_reboot()
│
├── ota_controller.wait_and_run()      ← janela de 10s
│   ├── [sem botão] → ESP_ERR_TIMEOUT → segue normal
│   └── [botão pressionado]
│       ├── wifi.connect(30s) → aguarda IP
│       ├── ota_manager.start_ota()
│       ├── aguarda READY_TO_RESTART ou FAILED
│       └── restart_on_success=true → reboot automático
│
└── WaterTankApp::run()
    └── leitura → lógica → envio ESP-NOW → deep sleep
```

---

## Mudança Futura: De Botão para ESP-NOW

Para migrar o gatilho, basta criar `EspNowOtaTrigger : public IOtaTrigger` que observa a
`app_rx_queue` do EspNow e substituir a instância em `main.cpp`:

```cpp
// Antes:
static BootButtonOtaTrigger ota_trigger(BOOT_BUTTON_GPIO);

// Depois:
static EspNowOtaTrigger ota_trigger(espnow, FarmPayloadType::OTA_COMMAND);
```

**Zero mudanças** em `OtaController`, `OtaManager`, ou no restante do `main.cpp`.

---

## Workflow de Publicação do Submodule

```bash
# 1. Commitar as mudanças no repo standalone do common
cd /home/german/dev/workspaces/smart-farm-common
git add .
git commit -m "feat(common): add IOtaTrigger interface and BootButtonOtaTrigger"
git push

# 2. Atualizar o ponteiro do submodule no water-tank
cd /home/german/dev/workspaces/smart-farm-water-tank
git submodule update --remote components/smart-farm-common
git add components/smart-farm-common
git commit -m "chore: bump smart-farm-common (add IOtaTrigger)"
```

---

## Verification Plan

### Build Test
```bash
idf.py build
```

### Manual Verification
1. `idf.py erase-flash && idf.py flash monitor`
2. Observar: `OtaController: OTA window open. Waiting for trigger (10000 ms)...`
3. Sem botão → `OtaController: OTA trigger window expired. Proceeding with normal boot.`
4. Fluxo normal do WaterTankApp continua (medição + sleep)
5. Pressionar BOOT durante a janela de 10s:
   - `OTA trigger received.` → `WiFi started, connecting...` → `OTA in progress...`
   - Servidor deve estar rodando: `python3 -m http.server 8070`
