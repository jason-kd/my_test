#include "wja_def.h"
#include <sys/select.h>
#include "wja_media.h"
#include "uart_mgr.h"

static char *TAG="console";
fsafsdfs

static TaskHandle_t console_hdl;

void file_write_test_thread(void *p)
{
        file_write("/spiffs/52747237.jpeg", "1234567890", 10);
        taskDelay(1000);
        remove("/spiffs/52747237.jpeg");
        vTaskDelete(NULL);
}
int file_write_test(void *p)
{	
	int as32Test[10] = {0};

	as32Test[10] = 12;
	
	as32Test[11] = 12;
	
	as32Test[11] = 14;
	
	as32Test[13] = 14;
	as32Test[14] = 14;

}

void uart_console_task(void *arg)
{
	int as32Test[10] = {0};

	as32Test[10] = 12;

	
	char buffer[100];
	ESP_LOGI(TAG, "%s, %d", __func__,__LINE__);
	while(1){
		memset(buffer, 0, sizeof(buffer));
		//taskDelay(50);
		fgets(buffer, 100, stdin);
		int len = strlen(buffer);
		if(len <= 1){
			continue;
		}
		buffer[len - 1] = '\0';
		if(!strcmp(buffer, "wifi cfg"))
		{
			#if AP_WIFI_CFG_SUPPORT
			wja_wifi_prov_init();
			ESP_LOGI(TAG, "wja_wifi_prov_init\n");
			#elif  BLE_OR_QR_WIFI_CFG_SUPPORT
			wja_blufi_init();
			ESP_LOGI(TAG, "wja_blufi_init\n");
			#endif

		}
		else if (strstr(buffer, "uart"))
		{
			wja_uart_test(buffer, strlen(buffer));
		}
		else if(!strcmp(buffer, "reboot"))
		{
			esp_restart();
		}
		else if(NULL != strstr(buffer, "ota"))//ota:http://xxxxxx
		{
			app_ota_task_init(buffer+4);//参数格式为:http://xxxxxxx
		}
		else if(!strncmp(buffer, "ssid", 4))
		{
			memset(gDevInfo.sta_ssid, 0, sizeof(gDevInfo.sta_ssid));
			strcpy(gDevInfo.sta_ssid, buffer+5);
			
			ESP_LOGI(TAG, "ssid: %s.\n",gDevInfo.sta_ssid);
#if INRAW_RW_FLASH_TASK
			inRamRwFlash_NVS_Write(NAME_SPACE_DEFAULT,NVS_KEY_WIFI_SSID,NAV_PARAM_TYPE_STR,gDevInfo.sta_ssid,sizeof(gDevInfo.sta_ssid), false);
#else
			nvs_partion_write(NAME_SPACE_DEFAULT,NVS_KEY_WIFI_SSID,NAV_PARAM_TYPE_STR,gDevInfo.sta_ssid,sizeof(gDevInfo.sta_ssid));
#endif
		}
		else if(!strncmp(buffer, "pwd", 3))
		{
			memset(gDevInfo.sta_pwd, 0, sizeof(gDevInfo.sta_pwd));
			strcpy(gDevInfo.sta_pwd, buffer+4);
			ESP_LOGI(TAG, "pwd: %s.\n",gDevInfo.sta_pwd);
#if INRAW_RW_FLASH_TASK
			inRamRwFlash_NVS_Write(NAME_SPACE_DEFAULT,NVS_KEY_WIFI_PWD,NAV_PARAM_TYPE_STR,gDevInfo.sta_pwd,sizeof(gDevInfo.sta_pwd), false);
#else
			nvs_partion_write(NAME_SPACE_DEFAULT,NVS_KEY_WIFI_PWD,NAV_PARAM_TYPE_STR,gDevInfo.sta_pwd,sizeof(gDevInfo.sta_pwd));
#endif
		}
		else if(!strncmp(buffer, "ATPN=", 5))
		{
			memset(gDevInfo.sta_ssid, 0, sizeof(gDevInfo.sta_ssid));
			char *pstr, *pssid, *pwd;
			pstr = buffer+5;
			
			int i;
			for(i=0;i<strlen(pstr);i++){
				if(*pstr == '\r' || *pstr == '\n')
					*pstr = '\0';
				pstr++;
			}
			pstr = buffer+5;
			
			pssid = strtok(pstr, ",");
			if(pssid != NULL){
				pwd = strtok(NULL, ",");
			}
			else
				continue;
			strcpy(gDevInfo.sta_ssid, pssid);
			
			memset(gDevInfo.sta_pwd, 0, sizeof(gDevInfo.sta_pwd));
			if(pwd != NULL)
			{
				strcpy(gDevInfo.sta_pwd, pwd);
			}
			ESP_LOGI(TAG, "ssid: %s, pwd:%s\n",gDevInfo.sta_ssid, gDevInfo.sta_pwd);
#if INRAW_RW_FLASH_TASK
			inRamRwFlash_NVS_Write(NAME_SPACE_DEFAULT, NVS_KEY_WIFI_SSID,NAV_PARAM_TYPE_STR,gDevInfo.sta_ssid,sizeof(gDevInfo.sta_ssid), false);
			inRamRwFlash_NVS_Write(NAME_SPACE_DEFAULT, NVS_KEY_WIFI_PWD,NAV_PARAM_TYPE_STR,gDevInfo.sta_pwd,sizeof(gDevInfo.sta_pwd), false);
#else
			nvs_partion_write(NAME_SPACE_DEFAULT, NVS_KEY_WIFI_SSID,NAV_PARAM_TYPE_STR,gDevInfo.sta_ssid,sizeof(gDevInfo.sta_ssid));
			nvs_partion_write(NAME_SPACE_DEFAULT, NVS_KEY_WIFI_PWD,NAV_PARAM_TYPE_STR,gDevInfo.sta_pwd,sizeof(gDevInfo.sta_pwd));
#endif
			taskDelay(10);
			
			esp_restart();
		}
		else if(!strncmp(buffer, "ATID=", 5))
		{
			char *pstr = buffer+5;
			int i;
			for(i=0;i<strlen(pstr);i++){
				if(*pstr == '\r' || *pstr == '\n')
					*pstr = '\0';
				pstr++;
			}
			pstr = buffer+5;

			if(strlen(pstr) < 10){
				printf("[ATID] write UUID failed\r\n");
				continue;
			}
			
			//ESP_LOGI(TAG, "%s, len:%d\n",buffer, strlen(pstr));
#if INRAW_RW_FLASH_TASK
			if(0 == inRamRwFlash_Stroge_Write(STROGE_KEY_UUID,pstr, 1+strlen(pstr), true)){
#else
			if(0 == stroge_partion_write(STROGE_KEY_UUID,pstr, 1+strlen(pstr))){
#endif
				printf("[ATID] write UUID:%s successed\r\n",pstr);
			}
			else
				printf("[ATID] write UUID failed\r\n");
		}
		else if(!strncmp(buffer, "ATAK=", 5))
		{
			char *pstr = buffer+5;
			int i;
			for(i=0;i<strlen(pstr);i++){
				if(*pstr == '\r' || *pstr == '\n')
					*pstr = '\0';
				pstr++;
			}
			pstr = buffer+5;
			
			if(strlen(pstr) < 10){
				printf("[ATAK] write AUTHKEY failed\r\n");
				continue;
			}
			//ESP_LOGI(TAG, "%s, len:%d\n",buffer, strlen(pstr));
#if INRAW_RW_FLASH_TASK
			if(0 == inRamRwFlash_Stroge_Write(STROGE_DEV_KEY,pstr, 1+strlen(pstr), true)){
#else
			if(0 == stroge_partion_write(STROGE_DEV_KEY,pstr, 1+strlen(pstr))){
#endif
				
				printf("[ATAK] write AUTHKEY:%s successed\r\n",pstr);
			}
			else
				printf("[ATAK] write AUTHKEY failed\r\n");
		}
		else if(!strncmp(buffer, "ATID", 4))
		{
			bzero(gDevInfo.uuid, sizeof(gDevInfo.uuid));
			
#if INRAW_RW_FLASH_TASK
			inRamRwFlash_Stroge_Read(STROGE_KEY_UUID, gDevInfo.uuid, sizeof(gDevInfo.uuid));
#else
			stroge_partion_read(STROGE_KEY_UUID, gDevInfo.uuid, sizeof(gDevInfo.uuid));
#endif
			gDevInfo.uuid[sizeof(gDevInfo.uuid) - 1] = '\0';
			printf("[ATID] read UUID:%s\r\n",gDevInfo.uuid);
		}
		else if(!strncmp(buffer, "ATAK", 4))
		{
			bzero(gDevInfo.dev_key, sizeof(gDevInfo.dev_key));
			
#if INRAW_RW_FLASH_TASK
			inRamRwFlash_Stroge_Read(STROGE_DEV_KEY, gDevInfo.dev_key, sizeof(gDevInfo.dev_key));
#else
			stroge_partion_read(STROGE_DEV_KEY, gDevInfo.dev_key, sizeof(gDevInfo.dev_key));
#endif
			gDevInfo.dev_key[sizeof(gDevInfo.dev_key) - 1] = '\0';
			printf("[ATAK] read AUTHKEY:%s\r\n",gDevInfo.dev_key);
		}
		else if(!strncmp(buffer, "erase", 5))
		{
#if INRAW_RW_FLASH_TASK
			inRamRwFlash_exec_func((EXEC_CB_FUNC)nas_partion_erase_all, NULL, true);
#else
			nas_partion_erase_all();
#endif
			printf("nas_partion_erase_all\n");
		}
		else if(!strncmp(buffer, "gpior", 5))
		{
			printf("gpio 21:%d\n", gpio_get_level(GPIO_NUM_21));
		}
		else if(!strncmp(buffer, "gpiow", 5))
		{
			char *pstr = buffer+5;
			printf("gpio 21:%d\n", gpio_set_level(GPIO_NUM_21, 0));
		}
		else if(!strncmp(buffer, "gpioa", 5))
		{
			printf("gpio 21:%d\n", gpio_set_level(GPIO_NUM_21, 1));
		}
		else if(!strncmp(buffer, "free", 4))
		{
			ESP_LOGI(TAG,"esp_get_free_heap_size:%d, esp_get_free_internal_heap_size:%d", esp_get_free_heap_size(), esp_get_free_internal_heap_size());
		}
		else if(!strncmp(buffer, "ffclear", 7))
		{
			ESP_LOGI(TAG,"spiffs_clear_all_data\n ");
#if INRAW_RW_FLASH_TASK
			inRamRwFlash_exec_func((EXEC_CB_FUNC)spiffs_clear_all_data, NULL, true);
#else
			spiffs_clear_all_data();
#endif
			ESP_LOGI(TAG,"spiffs_clear_all_data done\n ");
		}
		else if(!strncmp(buffer, "ufake", 5))
		{
			char *pstr = buffer+5;
			bool enable = false;
			if(!strncmp(pstr, "0", 1))
			{
				enable = false;
			}
			else
			{
				enable = true;
			}
			ESP_LOGI(TAG,"uartfake:%d", enable);
			LOCK_VALUE_FAKE = enable;
		}
		else if(!strncmp(buffer, "rssi", 4))
		{
			wifi_ap_record_t wifi_cfg;

			memset(&wifi_cfg, 0, sizeof(wifi_cfg));
			esp_wifi_sta_get_ap_info(&wifi_cfg);
			ESP_LOGI(TAG, "rssi,rssi %d.", wifi_cfg.rssi + 100);
		}
		else if(!strncmp(buffer, "qwer", 4))
		{
			inRamRwFlash_exec_func((EXEC_CB_FUNC)file_write_test, NULL, false);

		}
		else if(!strncmp(buffer, "blufi", 5))
        {
			system_active_set(ACTIVE_LEVEL_FAV);
			set_wakeup_event_group_gpio_bit();
			wja_blufi_init();
        }
		else if(!strncmp(buffer, "test1", 5))
        {
			if(get_mp3_decoder_handle() == 0)
			{
				wja_mp3_decoder_init();
				taskDelay(100);
			}
			// 检测cam是否在工作
			if(ucamera_is_running() == 0)
			{
				// 当前cam未工作
				board_cam_power_enable(true);
				taskDelay(50);
				ucamera_start(LCD_VIDEO_PLAY);
				taskDelay(200);
			}
			set_whether_close_cam_pipe_flag(1);
			inRamRwFlash_exec_func((EXEC_CB_FUNC)decoder_writer_init, (void *)AUDIO_FILE_LINK_SUCCESS, false);
			inRamRwFlash_exec_func((EXEC_CB_FUNC)decoder_data_reader_init, NULL, false);

        }
		else if(!strncmp(buffer, "test2", 5))
        {
			if(get_mp3_decoder_handle() == 0)
			{
				wja_mp3_decoder_init();
				taskDelay(100);
			}
			// 检测cam是否在工作
			if(ucamera_is_running() == 0)
			{
				// 当前cam未工作
				board_cam_power_enable(true);
				taskDelay(50);
				ucamera_start(LCD_VIDEO_PLAY);
				// taskDelay(300);
			}
			set_whether_close_cam_pipe_flag(1);

			inRamRwFlash_exec_func((EXEC_CB_FUNC)decoder_writer_init, (void *)AUDIO_FILE_LINK_FAIL, false);
			inRamRwFlash_exec_func((EXEC_CB_FUNC)decoder_data_reader_init, NULL, false);

        }
		else if(!strncmp(buffer, "test3", 5))
        {
			if(get_mp3_decoder_handle() == 0)
			{
				wja_mp3_decoder_init();
				taskDelay(100);
			}
			// 检测cam是否在工作
			if(ucamera_is_running() == 0)
			{
				// 当前cam未工作
				board_cam_power_enable(true);
				taskDelay(50);
				ucamera_start(LCD_VIDEO_PLAY);
				// taskDelay(300);
			}
			set_whether_close_cam_pipe_flag(1);

			inRamRwFlash_exec_func((EXEC_CB_FUNC)decoder_writer_init, (void *)AUDIO_OTA_SUCCESS, false);
			inRamRwFlash_exec_func((EXEC_CB_FUNC)decoder_data_reader_init, NULL, false);

        }
#if 0
		else if(!strncmp(buffer, "play", 4))
		{
			system_active_set(ACTIVE_LEVEL_FAV);
			ESP_LOGI(TAG,"cmd:%s", buffer);
			char sect= buffer[4];
			
			ESP_LOGI(TAG,"sect:%c", sect);
			switch(sect){
				case '0':
					audio_player(AUDIO_FILE_WIFI_CONFIG_START);
				break;
				case '1':
					audio_player(AUDIO_FILE_RCV_SUCCESS);
				break;
				case '2':
					audio_player(AUDIO_FILE_LINK_SUCCESS);
				break;
				case '3':
					audio_player(AUDIO_FILE_LINK_FAIL);
				break;
				default:
					break;
			}
		}
		else if(!strncmp(buffer, "deplay", 6))
		{
			//audio_player_task_deinit();
		}
#endif
		else if(!strncmp(buffer, "stat", 4))
		{
			audio_sys_get_real_time_stats();
		}
		else if(!strncmp(buffer, "alarm", 5))
		{
			static int eventType = 12;
			inRamRwFlash_exec_func((EXEC_CB_FUNC)capture_pic_thread_init, &eventType, true);
		}
		else if(!strncmp(buffer, "video", 5))
		{
			static int eventType = 0;
			inRamRwFlash_exec_func((EXEC_CB_FUNC)capture_video_thread_init, &eventType, true);
		}
		else if(!strncmp(buffer, "lcd", 5))
		{
			lcd_display(NULL);
			//inRamRwFlash_exec_func((EXEC_CB_FUNC)lcd_display, NULL, true);
		}
		else if(!strncmp(buffer, "qrde", 4))
		{
			qrCode_decode_appBind_init();
		}
		else if(!strncmp(buffer, "ustop", 5))
		{
			wja_ucamera_stop();
		}
#ifdef CONFIG_WJA_HTTP_SERVER
		else if(!strncmp(buffer, "server", 6)){
			system_active_set(ACTIVE_LEVEL_DISABLE_SLEEP);
			inRamRwFlash_exec_func((EXEC_CB_FUNC)example_start_file_server, "/spiffs", false);
		}
#endif
		else if(!strncmp(buffer, "stop", 4))
		{
			capture_pic_enable(false);
		}
		else if(!strncmp(buffer, "enable", 5))
		{
			capture_pic_enable(true);
		}
		
#if  DEBUG_RING_CACHE
		else if(!strncmp(buffer, "rd", 2))
		{
			printf("dump_ring_cache_entry\n");
			dump_ring_cache_entry();
		}
		else if(!strncmp(buffer, "rp1", 3))
		{
			printf("debug_ring_cache_psuh_data1\n");
			debug_ring_cache_psuh_data1();
		}
		else if(!strncmp(buffer, "rp2", 3))
		{
			printf("debug_ring_cache_psuh_data2\n");
			debug_ring_cache_psuh_data2();
		}
#endif

	}
}
void uart_console_task_init(void)
{
	ESP_LOGI(TAG, "%s, %d", __func__,__LINE__);
	example_configure_stdin_stdout();
	ESP_LOGI(TAG, "%s, %d", __func__,__LINE__);
#if USE_EXTERN_RAM
	int ret = audio_thread_create( &console_hdl, "console", uart_console_task, NULL, 5*1024,
            3, true, 1);
	if(ret != ESP_OK)
    {
		ESP_LOGI(TAG,"uart_console_task creat fail");
    }
#else
	xTaskCreate(uart_console_task, "console", 5*1024, NULL, 3, &console_hdl);
#endif
}

void uart_console_task_suspend(void)
{
	int as32Test[11] = {0};
	
	as32Test[11] = 15;
	
	if(console_hdl == NULL)
	{
		ESP_LOGI(TAG, "%s,console_hdl NULL", __func__);
		return;
	}
	ESP_LOGI(TAG, "console_hdl suspend");
	vTaskSuspend(console_hdl);
}

void uart_console_task_resume(void)
{
	if(console_hdl == NULL)
	{
		ESP_LOGI(TAG, "%s,console_hdl NULL", __func__);
		return;
	}
	
	trest;
	ESP_LOGI(TAG, "console_hdl resume");
	vTaskResume(console_hdl);
	
	
	
	
	
	
}

