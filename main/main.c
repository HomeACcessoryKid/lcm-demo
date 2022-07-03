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


// You must set VERSION=x.y.z of the lcm-demo code to match github version tag x.y.z via e.g. version.txt file
void app_main(void) {
    printf("\n\n\nLifeCycleManager-Demo ESP32-version %s\n",esp_ota_get_app_description()->version);
    
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    printf("Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
           nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
    uint8_t number;
    char    string[64];
    size_t  size=64;
    nvs_handle_t lcm_handle;
    err = nvs_open("LCM", NVS_READONLY, &lcm_handle);
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
        printf("\n");
    };
    // Note: no need to release iterator obtained from nvs_entry_find function when
    //       nvs_entry_find or nvs_entry_next function return NULL, indicating no other
    //       element for specified criteria was found.


//     // Open
//     printf("\n");
//     printf("Opening Non-Volatile Storage (NVS) handle... ");
//     nvs_handle_t my_handle;
//     err = nvs_open("LCM", NVS_READWRITE, &my_handle);
//     if (err != ESP_OK) {
//         printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
//     } else {
//         printf("Done\n");
// 
//         // Read
//         printf("Reading restart counter from NVS ... ");
//         int32_t restart_counter = 0; // value will default to 0, if not set yet in NVS
//         err = nvs_get_i32(my_handle, "restart_counter", &restart_counter);
//         switch (err) {
//             case ESP_OK:
//                 printf("Done\n");
//                 printf("Restart counter = %d\n", restart_counter);
//                 break;
//             case ESP_ERR_NVS_NOT_FOUND:
//                 printf("The value is not initialized yet!\n");
//                 break;
//             default :
//                 printf("Error (%s) reading!\n", esp_err_to_name(err));
//         }

//         // Write
//         printf("Updating restart counter in NVS ... ");
//         restart_counter++;
//         err = nvs_set_i32(my_handle, "restart_counter", restart_counter);
//         printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
// 
//         // Commit written value.
//         // After setting any values, nvs_commit() must be called to ensure changes are written
//         // to flash storage. Implementations may write to storage at other times,
//         // but this is not guaranteed.
//         printf("Committing updates in NVS ... ");
//         err = nvs_commit(my_handle);
//         printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
// 
//         // Close
//         nvs_close(my_handle);
//     }
// 
//     printf("\n");
// 
//     // Restart module
//     for (int i = 30; i >= 0; i--) {
//         printf("Restarting in %d seconds...\n", i);
//         vTaskDelay(1000 / portTICK_PERIOD_MS);
//     }
//     printf("Restarting now.\n");
//     fflush(stdout);
//     esp_restart();
}
