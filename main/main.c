/* Non-Volatile Storage (NVS) Read and Write a Value - Example

   For other examples please check:
   https://github.com/espressif/esp-idf/tree/master/examples

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "bootloader_common.h"


// You must set VERSION=x.y.z of the lcm-demo code to match github version tag x.y.z via e.g. version.txt file

uint8_t lcm_bootloader_count=0,rtc_read_busy=1;
void rtc_task(void *arg) {
    rtc_retain_mem_t* rtcmem=bootloader_common_get_rtc_retain_mem(); //access to the memory struct
    if (bootloader_common_get_rtc_retain_mem_reboot_counter()) { //if zero, RTC CRC not valid
        lcm_bootloader_count=rtcmem->custom[0]; //byte zero for count
    } else {
        lcm_bootloader_count=0; //valid count values are > 0
    }
    bootloader_common_reset_rtc_retain_mem(); //this will clear RTC    
    rtcmem->reboot_counter=1;
    rtcmem->custom[1]=lcm_bootloader_count>2?1:0; //byte one for temp_boot signal (from app to bootloader)
    bootloader_common_update_rtc_retain_mem(NULL,false); //this will update the CRC only
    printf("core0 count=%d temp_boot=%d\n",lcm_bootloader_count,rtcmem->custom[1]);
    rtc_read_busy=0;
    vTaskDelete(NULL);
}

void app_main(void) {
    xTaskCreatePinnedToCore(rtc_task,"rtc",4096,NULL,1,NULL,0); //CPU_0 PRO_CPU needed for rtc operations
    
    printf("\n\n\nLifeCycleManager-Demo ESP32-version %s\n",esp_ota_get_app_description()->version);
    while (rtc_read_busy) vTaskDelay(1);
    printf("app_main count=%d\n",lcm_bootloader_count);
    
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    //experimental block that gets you WIFI with the lowest amount of effort, otherwise based on FLASH
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    esp_netif_config.route_prio = 128;
    esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    esp_wifi_set_default_wifi_sta_handlers();
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());


    uint8_t number;
    char    string[64];
    size_t  size=64;
    uint8_t blob_data[1024];
    size_t blob_size = 1024;
    nvs_handle_t lcm_handle;
    nvs_handle_t wifi_handle;
    err = nvs_open("LCM", NVS_READONLY, &lcm_handle);
    err = nvs_open("nvs.net80211", NVS_READWRITE, &wifi_handle);
    // Example of listing all the key-value pairs of any type under specified partition and namespace
    nvs_iterator_t it = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY);
    while (it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        it = nvs_entry_next(it);
        printf("namespace:%-15s key:%-15s type:%2d", info.namespace_name, info.key, info.type);
        if (!strcmp(info.namespace_name,"LCM")) { //LCM only uses U8 and string
            if (info.type==0x21) { //string
                string[0]=0;
                nvs_get_str(lcm_handle,info.key,string,&size);
                printf("  value: '%s'",string);
            } else { //number
                nvs_get_u8(lcm_handle,info.key,&number);
                printf("  value: %d",number);
            }
        }
        if (!strcmp(info.namespace_name,"nvs.net80211")) { //wifi only uses blob for ssid and password
            if (info.type==0x42) { //blob
                printf("  value:");
                blob_size=1024;
                nvs_get_blob(wifi_handle,info.key,blob_data,&blob_size);
                for (int i=0;i<blob_size;i++) printf(" %02x",blob_data[i]);
            }
            if (info.type==0x01) { //uint8_t
                nvs_get_u8(wifi_handle,info.key,&number);
                printf("  value: %d",number);
            }
        }
        printf("\n");
    };
    // Note: no need to release iterator obtained from nvs_entry_find function when
    //       nvs_entry_find or nvs_entry_next function return NULL, indicating no other
    //       element for specified criteria was found.

    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    printf("\nCount: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n\n",
           nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);

    // Restart module
    for (int i = 30; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    esp_ota_set_boot_partition(esp_partition_find_first(ESP_PARTITION_TYPE_ANY,ESP_PARTITION_SUBTYPE_ANY,"ota_1"));
    fflush(stdout);
    esp_restart();
}
