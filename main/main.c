/* (c) 2022 HomeAccessoryKid
 * LCM demo for ESP32, based on ota-demo for ESP8266
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
#include "lcm_api.h"

// You must set VERSION=x.y.z of the lcm-demo code to match github version tag x.y.z via e.g. version.txt file

#define CMD_BUF_SIZE 5000
uint8_t count=0;
int  timeleft=60; //60 seconds timeout if left unattended
nvs_handle_t lcm_handle;
nvs_handle_t wifi_handle;

void usage(void) {
    printf(
        "Available commands:\n"
        "  otareboot       -- Reboot to the OTA partition\n"
        "  otazero         -- Reset the ota_version to 0.0.0\n"
        "  <key>?          -- Query the value of <key>\n"
        "  <key>=<value>   -- Set <key> to text <value>\n"
        "  <key>#<value>   -- Set <key> to uint8_t <value>\n"
        "  dump            -- Show all currently set keys/values\n"
        "  stats           -- Show current statistics about NVS\n"
        "  reformat        -- Reinitialize (clear) the sysparam area\n"
        "  echo_off        -- Disable input echo\n"
        "  echo_on         -- Enable input echo\n"
        "  help            -- Show this help screen\n"
        );
//      "  led<+/-><pin#>  -- Set gpio with a LED connected, >15 to remove\n"
//      "                      +- defines if LED activates with a 0 or a 1\n"
//      "  <key>:<hexdata> -- Set <key> to binary blob represented as hex\n"
}

size_t tty_readline(char *buffer, size_t buf_size, bool echo) {
    size_t i = 0;
    int c;

    while (true) {
        c = getchar();
        if (c == '\r' || c == '\n') {
            if (echo) putchar('\n');
            break;
        } else if (c == '\b' || c == 0x7f) {
            if (i) {
                if (echo) {
                    printf("\b \b");
                    fflush(stdout);
                }
                i--;
            }
        } else if (c < 0x20) {
            /* Ignore other control characters */
        } else if (i >= buf_size - 1) {
            if (echo) {
                putchar('\a');
                fflush(stdout);
            }
        } else {
            buffer[i++] = c;
            if (echo) {
                putchar(c);
                fflush(stdout);
            }
        }
        vTaskDelay(1);
    }

    buffer[i] = 0;
    return i;
}

uint8_t *parse_hexdata(char *string, size_t *result_length) {
    size_t string_len = strlen(string);
    uint8_t *buf = malloc(string_len / 2);
    uint8_t c;
    int i, j;
    bool digit = false;

    j = 0;
    for (i = 0; string[i]; i++) {
        c = string[i];
        if (c >= 0x30 && c <= 0x39) {
            c &= 0x0f;
        } else if (c >= 0x41 && c <= 0x46) {
            c -= 0x37;
        } else if (c >= 0x61 && c <= 0x66) {
            c -= 0x57;
        } else if (c == ' ') {
            continue;
        } else {
            free(buf);
            return NULL;
        }
        if (!digit) {
            buf[j] = c << 4;
        } else {
            buf[j++] |= c;
        }
        digit = !digit;
    }
    if (digit) {
        free(buf);
        return NULL;
    }
    *result_length = j;
    return buf;
}

uint8_t number;
char    string[65];
size_t  size=65;
uint8_t blob_data[65];
size_t  blob_size=65;
void dump_params(void) {
    printf("\n");
    nvs_iterator_t it = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY); //listing all the key-value pairs
    while (it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        it = nvs_entry_next(it);
        if (strcmp(info.namespace_name,"nvs.net80211")) { //suppress wifi at this level
            printf("namespace:%-15s key:%-15s type:%2d", info.namespace_name, info.key, info.type);
        }
        if (!strcmp(info.namespace_name,"LCM")) { //LCM only uses U8 and string
            if (info.type==0x21) { //string
                string[0]=0;size=65;
                nvs_get_str(lcm_handle,info.key,string,&size);
                printf("  value: '%s'",string);
            } else { //number
                nvs_get_u8(lcm_handle,info.key,&number);
                printf("  value: %d",number);
            }
        }
        if (!strcmp(info.namespace_name,"nvs.net80211")) { //wifi only uses blob for ssid and password
            if (!strcmp(info.key,"sta.ssid")) {
                printf("namespace:%-15s key:%-15s type:%2d  value: ", info.namespace_name, info.key, info.type);
                blob_size=65;
                nvs_get_blob(wifi_handle,info.key,blob_data,&blob_size);
                printf("'%s'",blob_data+4);
            }
            if (!strcmp(info.key,"sta.pswd")) {
                printf("namespace:%-15s key:%-15s type:%2d  value: ", info.namespace_name, info.key, info.type);
                blob_size=65;
                nvs_get_blob(wifi_handle,info.key,blob_data,&blob_size);
                printf("'%s'",blob_data);
            }
        }
        if (strcmp(info.namespace_name,"nvs.net80211")) printf("\n");
    };
    // Note: no need to release iterator obtained from nvs_entry_find function when
    //       nvs_entry_find or nvs_entry_next function return NULL, indicating no other
    //       element for specified criteria was found.
}

void nvs_stats() {
    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    printf("NVS-Count: UsedEntries = (%d), FreeEntries = (%d), AllEntries = (%d)\n",
           nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
}

void ota_task(void *arg) {
    char *cmd_buffer = malloc(CMD_BUF_SIZE);
    esp_err_t status;
    char *value;
//     uint8_t *bin_value;
    size_t len;
//     uint8_t *data;
    bool echo = true;

    vTaskDelay(1000); //10 seconds to allow connecting a console after flashing and flush wifi info messages
    if (!cmd_buffer) {
        printf("ERROR: Cannot allocate command buffer!\n");
        vTaskDelete(NULL);
    }

    count=lcm_read_count();
    printf("\n\n\nLifeCycleManager-Demo ESP32-version %s\n",esp_ota_get_app_description()->version);
    printf("LCM power-cycle count=%d\n",count);
    printf(
        "\nIn 1 minute will reset.\n"
        "Press <enter> for 5 new minutes\n"
        "If powercylce count=3 reboot to OTA and\n"
        "if powercylce count=4 also set the version to 0.0.0\n"
        "Enter 'help' for more information.\n\n"
    );
    nvs_stats();
    status = nvs_open("LCM", NVS_READWRITE, &lcm_handle);
    status = nvs_open("nvs.net80211", NVS_READWRITE, &wifi_handle);
    while (true) {
        printf("==> ");
        fflush(stdout);
        len = tty_readline(cmd_buffer, CMD_BUF_SIZE, echo);
        timeleft=300; //5 minutes after input
        status = ESP_OK;
        if (!len) continue;
        if (cmd_buffer[len - 1] == '?') {
            cmd_buffer[len - 1] = 0;
            printf("Querying '%s'...\n", cmd_buffer);
//             status = nvs_get_str(lcm_handle,cmd_buffer,string,&size);
//             if (status == SYSPARAM_OK) {
//                 print_text_value(cmd_buffer, value);
//                 free(value);
//             } else if (status == SYSPARAM_PARSEFAILED) {
//                 // This means it's actually a binary value
//                 status = sysparam_get_data(cmd_buffer, &bin_value, &len, NULL);
//                 if (status == SYSPARAM_OK) {
//                     print_binary_value(cmd_buffer, bin_value, len);
//                     free(value);
//                 }
//             }
        } else if ((value = strchr(cmd_buffer, '='))) {
            *value++ = 0;
            printf("Setting '%s' to '%s'...\n", cmd_buffer, value);
            if (strlen(value)) {
                status = nvs_set_str(lcm_handle,cmd_buffer, value);
            } else {
                status = nvs_erase_key(lcm_handle,cmd_buffer);
            }
            nvs_commit( lcm_handle);
        } else if ((value = strchr(cmd_buffer, '#'))) {
            *value++ = 0;
            if (atoi(value)<256) {
                printf("Setting '%s' to '%s'...\n", cmd_buffer, value);
                if (strlen(value)) {
                    status = nvs_set_u8(lcm_handle,cmd_buffer, atoi(value));
                } else {
                    status = nvs_erase_key(lcm_handle,cmd_buffer);
                }
                nvs_commit( lcm_handle);
            } else {
                printf("Valid uint8_t values are between 0 and 255\n");
            }
//         } else if ((value = strchr(cmd_buffer, ':'))) {
//             *value++ = 0;
//             data = parse_hexdata(value, &len);
//             if (value) {
//                 printf("Setting '%s' to binary data...\n", cmd_buffer);
// //                 status = sysparam_set_data(cmd_buffer, data, len, true);
//                 free(data);
//             } else {
//                 printf("Error: Unable to parse hex data\n");
//             }
//         } else if ((value = strchr(cmd_buffer, '+'))) {
//             *value++ = 0;
//             ledset(atoi(value),0);
//         } else if ((value = strchr(cmd_buffer, '-'))) {
//             *value++ = 0;
//             ledset(atoi(value),1);
        } else if (!strcmp(cmd_buffer, "dump")) {
            printf("Dumping all params:\n");
            dump_params();
        } else if (!strcmp(cmd_buffer, "stats")) {
            nvs_stats();
        } else if (!strcmp(cmd_buffer, "reformat")) {
            printf("Re-initializing region...\n");
            nvs_flash_erase();
            nvs_flash_init();
        } else if (!strcmp(cmd_buffer, "echo_on")) {
            echo = true;
            printf("Echo on\n");
        } else if (!strcmp(cmd_buffer, "echo_off")) {
            echo = false;
            printf("Echo off\n");
        } else if (!strcmp(cmd_buffer, "otareboot")) {
            lcm_temp_boot(); //select the OTA main routine and restart
        } else if (!strcmp(cmd_buffer, "otazero")) {
            nvs_set_str(lcm_handle,"ota_version", "0.0.0");  //only needed for the demo to be efficient
            nvs_commit( lcm_handle);
        } else if (!strcmp(cmd_buffer, "help")) {
            usage();
        } else {
            printf("Unrecognized command.\n\n");
            usage();
        }

        if (status != ESP_OK) ESP_ERROR_CHECK(status);
    }
}    

void timeout_task(void *arg) {
    while(1) {
        if (timeleft==10) printf("In 10 seconds will reset the version to 0.0.0 and reboot to OTA\nPress <enter> for 5 new minutes\n==> ");
        if (timeleft <10) printf("In %d seconds\n==> ",timeleft);
        if (timeleft--==0) { //timed out
            if (count==4) nvs_set_str(lcm_handle,"ota_version", "0.0.0");  //only needed for the demo to be efficient
            nvs_commit( lcm_handle);
            dump_params();
            // in ota-boot the user gets to set the wifi and the repository details and it then installs the ota-main binary
            // the below line is the ONLY thing needed for a repo to support ota after having started with ota-boot
            printf("Restarting now.\n");
            fflush(stdout);
            if (count>2) lcm_temp_boot(); // for count==3 and count==4 we trigger ota_main
            else esp_restart(); //regular reboot

            vTaskDelete(NULL); //should never get here
        }
        vTaskDelay(100); //1 second
    }
}

void app_main(void) {
    printf("app_main-start\n");

    //The code in this function would be the setup for any app that uses wifi which is set by LCM
    //It is all boilerplate code that is also used in common_example code
    esp_err_t err = nvs_flash_init(); // Initialize NVS
    if (err==ESP_ERR_NVS_NO_FREE_PAGES || err==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); //NVS partition truncated and must be erased
        err = nvs_flash_init(); //Retry nvs_flash_init
    } ESP_ERROR_CHECK( err );

    //block that gets you WIFI with the lowest amount of effort, and based on FLASH
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
    //end of boilerplate code

    xTaskCreate(ota_task,"ota",4096,NULL,1,NULL);
    xTaskCreate(timeout_task,"t-o",2048,NULL,1,NULL);

    printf("app_main-done\n");
}
