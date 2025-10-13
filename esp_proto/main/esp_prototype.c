/*
 * Addaption of the linux_bluez_prototype to work with the ESP32 Bluedroid stack
 * Some AI was used to help develop this port alongside https://www.circuitstate.com/tutorials/getting-started-with-espressif-esp32-wifi-bluetooth-soc-using-doit-esp32-devkit-v1-development-board/#ESP32_Programming
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"

#define PROGRAM_TAG "ESP_PROTO"
#define DEVICE_NAME "ESP32_PROTOTYPE_DEVICE"
#define MESSAGE_TO_SEND "Hello from ESP32"
#define SPP_SERVER_NAME "SPP_SERVER"
#define FIXED_SCN 3  // Use channel 3 communicate with Linux Version

static uint32_t spp_handle = 0;
static bool is_connected = false;
static bool is_server_started = false;
static volatile int num_devices_found = 0;
static int current_device_index = 0;
static SemaphoreHandle_t connection_sem = NULL;
static volatile bool connecting_in_progress = false;

typedef struct {
    esp_bd_addr_t bda;
    char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
} discovered_device_t;

#define MAX_DISCOVERED_DEVICES 10
static discovered_device_t discovered_devices[MAX_DISCOVERED_DEVICES];
static discovered_device_t devices_to_connect[MAX_DISCOVERED_DEVICES];
static int devices_to_connect_count = 0;

static void esp_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
    case ESP_SPP_INIT_EVT:
        ESP_LOGI(PROGRAM_TAG, "SPP initialized");
        // Start SPP server on fixed channel 3
        esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_SLAVE, FIXED_SCN, SPP_SERVER_NAME);
        break;

    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(PROGRAM_TAG, "SPP server started on SCN %d, listening...", param->start.scn);
            is_server_started = true;
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        } else {
            ESP_LOGE(PROGRAM_TAG, "SPP server start failed, status: %d", param->start.status);
        }
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(PROGRAM_TAG, "SPP server connection opened from: " ESP_BD_ADDR_STR, 
                 ESP_BD_ADDR_HEX(param->srv_open.rem_bda));
        spp_handle = param->srv_open.handle;
        is_connected = true;
        break;

    case ESP_SPP_DATA_IND_EVT:
        ESP_LOGI(PROGRAM_TAG, "\n\nRECEIVED MESSAGE:");
        ESP_LOGW(PROGRAM_TAG, "%.*s\n\n", param->data_ind.len, param->data_ind.data);
        
        // Send response back
        if (param->data_ind.handle) {
            const char *response = MESSAGE_TO_SEND;
            ESP_LOGI(PROGRAM_TAG, "Sending response: %s", response);
            esp_spp_write(param->data_ind.handle, strlen(response), (uint8_t *)response);
        }
        break;

    case ESP_SPP_WRITE_EVT:
        if (param->write.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(PROGRAM_TAG, "Message sent successfully");
        } else {
            ESP_LOGE(PROGRAM_TAG, "Message send failed");
        }
        break;

    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(PROGRAM_TAG, "SPP connection closed");
        is_connected = false;
        spp_handle = 0;
        break;

    case ESP_SPP_CONG_EVT:
        ESP_LOGW(PROGRAM_TAG, "SPP congestion: %d", param->cong.cong);
        break;

    case ESP_SPP_DISCOVERY_COMP_EVT:
        if (param && param->disc_comp.scn_num > 0) {
            ESP_LOGI(PROGRAM_TAG, "SPP discovery complete, status: %d, scn_num: %d", 
                     param->disc_comp.status, param->disc_comp.scn_num);
            if (param->disc_comp.status == ESP_SPP_SUCCESS) {
                ESP_LOGI(PROGRAM_TAG, "Found SPP service on channel %d, connecting...", param->disc_comp.scn[0]);
                esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER, 
                               param->disc_comp.scn[0], discovered_devices[current_device_index].bda);
            } else {
                ESP_LOGW(PROGRAM_TAG, "No SPP service found, trying fixed channel %d", FIXED_SCN);
                // Try connecting to fixed channel (for Linux compatibility)
                esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER, 
                               FIXED_SCN, discovered_devices[current_device_index].bda);
            }
        } else {
            ESP_LOGW(PROGRAM_TAG, "SPP discovery complete with no services, trying fixed channel %d", FIXED_SCN);
            // Try connecting to fixed channel (for Linux compatibility)
            if (current_device_index < MAX_DISCOVERED_DEVICES) {
                esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER, 
                               FIXED_SCN, discovered_devices[current_device_index].bda);
            }
        }
        break;

    case ESP_SPP_OPEN_EVT:
        if (param->open.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(PROGRAM_TAG, "SPP client connection opened successfully");
            spp_handle = param->open.handle;
            is_connected = true;
            
            ESP_LOGI(PROGRAM_TAG, "Sending message: %s", MESSAGE_TO_SEND);
            esp_spp_write(spp_handle, strlen(MESSAGE_TO_SEND), (uint8_t *)MESSAGE_TO_SEND);
            
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_spp_disconnect(spp_handle);
        } else {
            ESP_LOGE(PROGRAM_TAG, "SPP client connection failed, status: %d", param->open.status);
        }
        break;

    default:
        break;
    }
}

static void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        ESP_LOGI(PROGRAM_TAG, "Device discovered: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(param->disc_res.bda));
        
        if (num_devices_found < MAX_DISCOVERED_DEVICES) {
            memcpy(discovered_devices[num_devices_found].bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
            strcpy(discovered_devices[num_devices_found].name, "[unknown]");
            esp_bt_gap_read_remote_name(param->disc_res.bda);
            num_devices_found++;
        }
        break;
    
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(PROGRAM_TAG, "Remote name: %s", param->read_rmt_name.rmt_name);
            if (num_devices_found > 0 && num_devices_found <= MAX_DISCOVERED_DEVICES) {
                strncpy(discovered_devices[num_devices_found - 1].name, 
                       (char *)param->read_rmt_name.rmt_name, 
                       ESP_BT_GAP_MAX_BDNAME_LEN);
                discovered_devices[num_devices_found - 1].name[ESP_BT_GAP_MAX_BDNAME_LEN] = '\0';
            }
        }
        break;

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            ESP_LOGI(PROGRAM_TAG, "Discovery stopped. Found %d device(s)", num_devices_found);
            
            if (num_devices_found > 0 && num_devices_found <= MAX_DISCOVERED_DEVICES && !connecting_in_progress) {
                // Copy devices to a separate buffer for connection attempts
                devices_to_connect_count = num_devices_found;
                memcpy(devices_to_connect, discovered_devices, sizeof(discovered_device_t) * num_devices_found);
                // Signal the connection task
                if (connection_sem) {
                    xSemaphoreGive(connection_sem);
                }
            } else {
                if (num_devices_found == 0) {
                    ESP_LOGI(PROGRAM_TAG, "No devices found");
                }
            }
            
            // Reset for next scan
            num_devices_found = 0;
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            ESP_LOGI(PROGRAM_TAG, "Discovery started...");
        }
        break;

    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(PROGRAM_TAG, "Authentication success: %s", param->auth_cmpl.device_name);
        } else {
            ESP_LOGE(PROGRAM_TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
        }
        break;
        
    case ESP_BT_GAP_PIN_REQ_EVT:
        ESP_LOGI(PROGRAM_TAG, "PIN request, using default PIN");
        esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        break;

    default:
        break;
    }
}

void connection_task(void *pvParameters)
{
    while (true) {
        // Wait for discovery to complete
        if (xSemaphoreTake(connection_sem, portMAX_DELAY) == pdTRUE) {
            if (!connecting_in_progress && devices_to_connect_count > 0) {
                connecting_in_progress = true;
                
                ESP_LOGI(PROGRAM_TAG, "Attempting to connect to discovered devices...");
                
                for (int i = 0; i < devices_to_connect_count && i < MAX_DISCOVERED_DEVICES; i++) {
                    current_device_index = i;
                    if (devices_to_connect[i].name[0] != '\0') {
                        ESP_LOGI(PROGRAM_TAG, "Trying device %d: %s", i, devices_to_connect[i].name);
                    } else {
                        ESP_LOGI(PROGRAM_TAG, "Trying device %d: [unknown]", i);
                    }
                    
                    // Performs service discovery for the services provided by the given peer device 
                    esp_spp_start_discovery(devices_to_connect[i].bda);
                    vTaskDelay(3000 / portTICK_PERIOD_MS);
                }
                
                connecting_in_progress = false;
                devices_to_connect_count = 0;
            }
        }
    }
}

void scan_task(void *pvParameters)
{
    while (!is_server_started) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(PROGRAM_TAG, "Starting continuous scan loop...");
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    
    while (true) {
        if (!connecting_in_progress) {
            ESP_LOGI(PROGRAM_TAG, "Scanning for devices...");
            // Starts Inquiry and Name Discovery
            esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
        }
        
        // Wait for discovery to complete and connections to be attempted
        vTaskDelay(20000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(PROGRAM_TAG, "ESP32 Bluetooth Prototype");
    ESP_LOGI(PROGRAM_TAG, "Message: %s", MESSAGE_TO_SEND);
    ESP_LOGI(PROGRAM_TAG, "Device: %s", DEVICE_NAME);

    // Initialize the default NVS partition
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Setup ESP BT
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(esp_bt_gap_cb));
    ESP_ERROR_CHECK(esp_spp_register_callback(esp_spp_cb));

    // Setup Serial Port Protocol (SPP)
    esp_spp_cfg_t bt_spp_cfg = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size = 0,
    };
    ESP_ERROR_CHECK(esp_spp_enhanced_init(&bt_spp_cfg));

    // Set PINs for pairing
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
    esp_bt_gap_set_pin(pin_type, 4, pin_code);

    esp_bt_dev_set_device_name(DEVICE_NAME);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    // Create semaphore for connection task
    connection_sem = xSemaphoreCreateBinary();
    if (connection_sem == NULL) {
        ESP_LOGE(PROGRAM_TAG, "Failed to create semaphore");
        return;
    }

    // Create two "tasks" (threads) to handle scanning and connection attempts
    xTaskCreate(scan_task, "scan_task", 8192, NULL, 5, NULL);
    xTaskCreate(connection_task, "connection_task", 8192, NULL, 5, NULL);

    ESP_LOGI(PROGRAM_TAG, "Ready - listening and scanning");
}
