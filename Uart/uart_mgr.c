#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
// #include "driver/gpio.h"
#include "uart_protocol.h"
#include "low_power.h"
#include "wja_lock.h"
#include "alarm_capture_pic.h"

static const char *TAG = "UART";

static QueueHandle_t u2nQueue = NULL;
static QueueHandle_t n2uQueue = NULL;

static StaticQueue_t u2nQueueBuffer;
static StaticQueue_t n2uQueueBuffer;

static unsigned char *u2nQueueStorage = NULL;
static unsigned char *n2uQueueStorage = NULL;


static lock_uart_cmd_ret_check_t g_uart_cmd_ack;
static SemaphoreHandle_t uart_ack_mutex;

static EventGroupHandle_t uart_ack_wait_evt = NULL;

static bool uart1_start = false;
#define UART_ACK_WAIT_ALL_BIT			(BIT0|BIT1)
#define UART_ACK_SUCCESS_BIT		BIT0
#define UART_ACK_FAIL_BIT		    BIT1


uint8_t get_uart_last_cmd_ack(uint32_t block_time_ms)
{
	uint8_t ack = 0;

	if(uart_ack_mutex != NULL)
	{
		if( xSemaphoreTake( uart_ack_mutex, block_time_ms / portTICK_PERIOD_MS) == pdTRUE )
		{
			ack = g_uart_cmd_ack.ret_cmd;			
		}
	}

	return ack;
}

int set_uart_last_cmd_ack(lock_uart_cmd_ret_check_t tem)
{
	if(uart_ack_mutex != NULL)
	{
		g_uart_cmd_ack.ret_cmd = tem.ret_cmd;
		g_uart_cmd_ack.ret_value = tem.ret_value;
		xSemaphoreGive( uart_ack_mutex);
	}
	return 0;
}



QueueHandle_t get_u2n_queue(void)
{
	return u2nQueue;
}

void reset_u2n_queue(void)
{
	if (u2nQueue)
		xQueueReset(u2nQueue);
}

bool send_u2n_queue(struct un_report_t *report)
{
	if (u2nQueue)
		return xQueueSend(u2nQueue, report, 20 / portTICK_PERIOD_MS);
	else
		return false;
}

QueueHandle_t get_n2u_queue(void)
{
	return n2uQueue;
}

bool send_n2u_queue(struct un_report_t *report)
{
	bool ret = false;
	if (n2uQueue)
	{
		wja_clear_mqttmsg_uart_send_ack_bits();

		if( xQueueSend(n2uQueue, report, 20 / portTICK_PERIOD_MS) == pdPASS){
			ret = true;
		}
	}
	ESP_LOGW(TX_TASK_TAG, "txcmd:%02x,ret:%d.", report->cmd, ret);
	
	return ret;
}

int uart_send_data(const char* data, int len)
{
	//static const char *TAG = "UART_TX";
	int txBytes = uart_write_bytes(MY_UART_NUM, data, len);
	ESP_LOGI(TX_TASK_TAG, "Wrote %d bytes", txBytes);
	ESP_LOG_BUFFER_HEXDUMP(TX_TASK_TAG, data, txBytes, ESP_LOG_INFO);

	return txBytes;
}

static bool uart_wait_for(uint8_t cmd, int nMs, int repeat)
{
	bool ret = false;

	if (0 == nMs || 0 == repeat)
		return ret;

	do {
		if (get_uart_last_cmd_ack(nMs) == cmd)
		{
			ret = true;
			break;
		}		
	} while(repeat--);

	return ret;
}

int make_uart_param_quick(struct un_report_t *un_msg, uint8_t *param)
{
	uint8_t i = 0;
	int len = -1;

	if (NULL == un_msg || NULL == param)
	{
		UART_INFO("un_msg or param NULL,%p,%p", &un_msg, &param);
		return len;
	}

	UART_INFO("un_msg cmd:%02x", un_msg->cmd);
	switch(un_msg->cmd)
	{
		case CMD_VISUAL_MODULE_STATUS_SYNC:
		{
			param[0] = un_msg->payload.sync_status;
			len = 1;
			break;
		}
		case CMD_VISUAL_MODULE_OR_LOCK_SLEEP_NOTICE:
		{
			param[0] = 0x00;
			len = 1;
			break;
		}
		case CMD_VISUAL_MODULE_OR_LOCK_BOARD_WAKEUP:
		{
			uint8_t type = un_msg->cmd_type;
			if(type == CMD_TYPE_REQUEST)
			{
				len = 0;
			}
			else if(type == CMD_TYPE_RESPONSE)
			{
				param[0] = 0x00;
				len = 1;
			}
			break;
		}
		case CMD_SET_VISUAL_MODULE_WIFI_PROVISION: 
		{
			param[0] = un_msg->payload.provision_status;
			len = 1;
			break;
		}
		case CMD_REQUIRE_WIFI_PROVISION_STAT: 
		{
			param[0] = 0;
			param[1] = un_msg->payload.char_buff;
			len = 2;
			break;
		}
		case CMD_SET_VISUAL_MODULE_RESET_APP_BIND: 
		{
			param[0] = 0x00;
			len = 1;
			break;
		}
		case CMD_SET_VISUAL_MODULE_DEFAULT_STATUS: 
		{
			param[0] = un_msg->payload.exe_status;
			len = 1;
			break;
		}
#if 0		
		case CMD_SET_LOCK_BOARD_DEFAULT_STATUS:
		{
			param[0] = 0x00;
			len = 1;
			break;
		}
#endif
		case CMD_GET_VISUAL_MODULE_VERSION_AND_UUID:
		{
			len = 0;

			#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
			param[0] = 0x00;
			len += 1;
			#endif

			char *uuid = (char*)malloc_priv(20);
			bzero(uuid, 20);
#if INRAW_RW_FLASH_TASK
			inRamRwFlash_Stroge_Read(STROGE_KEY_UUID, uuid, 20);
#else
			stroge_partion_read(STROGE_KEY_UUID, uuid, 20);
#endif
			uint8_t len_tem = strlen(uuid);
			if(len_tem > 19)
				len_tem = 19;
			strncpy((char*)param+len, uuid, len_tem);
			len += 20;
			free_priv(uuid);

			#if (LCD_I80_3INCH || LCD_I80_3INCH5)
			len_tem = strlen(VISION_MODULE_VERSION_3INCH5_PART);
			snprintf((char*)param+len, len_tem+1,"%s", VISION_MODULE_VERSION_3INCH5_PART);
			#elif LCD_RGB_4INCH
			len_tem = strlen(VISION_MODULE_VERSION_4INCH_PART);
			snprintf((char*)param+len, len_tem+1,"%s", VISION_MODULE_VERSION_4INCH_PART);
			#endif
			len += len_tem;

			len_tem = strlen(LOCK_FACTORT_ID)+1;
			snprintf((char*)param+len, len_tem+1,"%s%s", LOCK_FACTORT_ID, "_");
			len += len_tem;

			len_tem = strlen(gDevInfo.baseCfg.soft_version);
			strncpy((char*)param+len, gDevInfo.baseCfg.soft_version, len_tem);
			len += len_tem;
			
			break;
		}
		case CMD_GET_VISUAL_MODULE_VERSION:
		{
			uint8_t len_tem = strlen(gDevInfo.baseCfg.soft_version);
			if(len_tem > 20)
				len_tem = 20;
			strncpy((char*)param, gDevInfo.baseCfg.soft_version, len_tem);

			len = 20;
			break;
		}
		case CMD_GET_VISUAL_MODULE_UUID:
		{
			char *uuid = (char*)malloc_priv(32);
			bzero(uuid, 32);
#if INRAW_RW_FLASH_TASK
			inRamRwFlash_Stroge_Read(STROGE_KEY_UUID, uuid, 32);
#else
			stroge_partion_read(STROGE_KEY_UUID, uuid, 32);
#endif
			uint8_t len_tem = strlen(uuid);
			if(len_tem > 20)
				len_tem = 20;
			strncpy((char*)param, uuid, len_tem);
			free_priv(uuid);
			len = 20;
			break;
		}
		case CMD_SET_VISUAL_MODULE_UUID:
		{
			param[0] = un_msg->payload.exe_status;
			len = 1;
			break;
		}
		case CMD_SET_VISUAL_MODULE_AUTHKEY:
		{
			param[0] = un_msg->payload.exe_status;
			len = 1;
			break;
		}
		case CMD_GET_VISUAL_MODULE_AUTHKEY:
		{
			len = strlen(gDevInfo.dev_key);
			
			memcpy(param, gDevInfo.dev_key, len);
			break;
		}
		case CMD_GET_CMEI:
		{
			len = strlen(gDevInfo.cmei);
			
			memcpy(param, gDevInfo.cmei, len);
			break;
		}
		case CMD_GET_VISUAL_MODULE_MAC:
		{
			len = 0;
			uint8_t *mac = (uint8_t*)malloc_priv(6);
			char *str_mac = (char*)malloc_priv(32);
			memset(mac, 0, 6);	
			memset(str_mac, 0, 32);	
			esp_read_mac(mac, ESP_MAC_BT);
			snprintf(str_mac, 32, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
			
			#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
				param[0] = 0x00;
				len += 1;
			#endif
			uint8_t len_tem = strlen(str_mac);
			if(len_tem > 19)
				len_tem = 19;
			strncpy((char*)param+len, str_mac, len_tem);
			len += len_tem;

			free_priv(mac);
			free_priv(str_mac);
			break;
		}
		case CMD_GET_VISUAL_MODULE_WIFI_STRENGTH:
		{
			wifi_ap_record_t wifi_cfg;
			len = 0;

			memset(&wifi_cfg, 0, sizeof(wifi_cfg));
			esp_wifi_sta_get_ap_info(&wifi_cfg);

			#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
				param[0] = 0x00;
				len += 1;
			#endif
			param[len] = wifi_cfg.rssi + 100;
			len += 1;
			break;
		}
		case CMD_GET_VISUAL_MODULE_BATTERY_LEVEL:
		{
			len = 0;

			#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
				param[0] = 0x00;
				len += 1;
			#endif
			param[len] = un_msg->payload.char_buff;
			len += 1;
			break;
		}
		case CMD_GET_VISUAL_MODULE_NET_TIME:
		{
#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
			len = 0;
			param[0] = 0x00;
			len += 1;
			break;
#endif			
		}
		case CMD_SET_LOCK_BOARD_NET_TIME:
		{
			struct datetime_t *datetime = &(un_msg->payload.datetime);

			param[10] = (uint8_t)datetime->s32TimeStamp;
			param[9] = (uint8_t)(datetime->s32TimeStamp >> 8);
			param[8] = (uint8_t)(datetime->s32TimeStamp >> 16);
			param[7] = (uint8_t)(datetime->s32TimeStamp >> 24);
			param[6] = datetime->s8TimeZone;

#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL			
			param[5] = convert_int_to_BSD(datetime->sec);
			param[4] = convert_int_to_BSD(datetime->min);
			param[3] = convert_int_to_BSD(datetime->hour);
			param[2] = convert_int_to_BSD(datetime->day);
			param[1] = convert_int_to_BSD(datetime->mon);
			param[0] = convert_int_to_BSD(datetime->year - 2000);
#else
			param[5] = convert_int_to_BSD(datetime->year - 2000);
			param[4] = convert_int_to_BSD(datetime->mon);
			param[3] = convert_int_to_BSD(datetime->day);
			param[2] = convert_int_to_BSD(datetime->hour);
			param[1] = convert_int_to_BSD(datetime->min);
			param[0] = convert_int_to_BSD(datetime->sec);
#endif
			len = 11;
			break;
		}
		case CMD_UPLOAD_LOCK_BOARD_MBAT_LEVEL:
		{
			param[0] = un_msg->payload.char_buff;
			len = 1;
			break;
		}
		case CMD_GET_LOCK_BOARD_CURRENT_TIME:
		{
			len = 0;
			break;
		}
		case CMD_GET_LOCK_BOARD_VERSION:
		{
			len = 0;
			break;
		}
		case CMD_GET_LOCK_BOARD_LOCK_STATUS:
		{
			len = 0;
			break;
		}
		case CMD_GET_LOCK_BOARD_USER_LIST:
		{
			len = 0;
			break;
		}
		case CMD_GET_LOCK_BOARD_MAIN_BAT_LEVEL:
		{
			len = 0;
			break;
		}
		case CMD_SET_VISUAL_MODULE_CAPTURE_EVENT:
		{
			param[0] = un_msg->payload.exe_status;
			len = 1;
			break;
		}
		case CMD_GET_VISUAL_MODULE_CURRENT_STATUS:
		{
			param[0] = un_msg->payload.char_buff;
			len = 1;
			break;
		}
		case CMD_REQUESET_VISUAL_MODULE_UNLOCK_LOCK:
		{
			param[0] = un_msg->payload.exe_status;
			len = 1;
			break;
		}
		case CMD_REQUESET_VISUAL_MODULE_LOCK_LOCK:
		{
			param[0] = un_msg->payload.exe_status;
			len = 1;
			break;
		}
		case CMD_REQUESET_VISUAL_MODULE_VERIFY_PASSW:
		{
			param[0] = un_msg->payload.lock_verifyPassw_ret.ret;
			param[1] = un_msg->payload.lock_verifyPassw_ret.buff;
			len = 2;
			break;
		}
		case CMD_VISUAL_MODULE_POWER_SAVE_MODE_NOTICE:
		{
			param[0] = un_msg->payload.power_save_mode.enable;
			param[1] = (uint8_t)(un_msg->payload.power_save_mode.start_time >> 8);
			param[2] = (uint8_t)(un_msg->payload.power_save_mode.start_time);
			param[3] = (uint8_t)(un_msg->payload.power_save_mode.end_time >> 8);
			param[4] = (uint8_t)(un_msg->payload.power_save_mode.end_time);
			len = 5;
			break;
		}
		case CMD_SET_LOCK_PIR_MODE:
		{
			param[0] = un_msg->payload.char_buff;
			len = 1;
			break;
		}
		case CMD_SET_LOCK_APP_OK_UNLOCK:
		{
			len = 0;
			break;
		}
		case CMD_VISUAL_MODULE_FACTORY_TEST:
		{
			param[0] = un_msg->payload.char_buff;
			len = 1;
			break;
		}
		case CMD_VISUAL_MODULE_GPIO_DETECT:
		{
			param[0] = un_msg->payload.io_detect.io_num;
			param[1] = un_msg->payload.io_detect.value;
			len = 2;
			break;
		}
		case CMD_GET_LOCK_FULL_RANGE_ATTRIBUTE:
		{
			len = 0;
			break;
		}
		case CMD_SET_LOCK_FULL_RANGE_ATTRIBUTE:
		{
			write_lock_dev_fullRange_attribute_t temp = {0};
			len = 0;
			memcpy(&temp, un_msg->payload.set_fullRange_attribute, sizeof(write_lock_dev_fullRange_attribute_t));
			for(int i = 0; i < 5; i++)
			{
				if(temp.attribute[i].write_enable)
				{
					param[i] = 2;
					param[i+1] = temp.attribute[i].attri_id;
					param[i+2] = temp.attribute[i].value;
					len += 3;
				}
			}
 			memset(un_msg->payload.set_fullRange_attribute, 0, sizeof(write_lock_dev_fullRange_attribute_t));
			break;
		}
		case CMD_SET_LOCK_OTA_REQUEST:
		{
			break;
		}
		default:
				UART_INFO("make uart param cmd dont matching");
				len = -1;
			break;
	}

	return len;
}

int wja_clear_mqttmsg_uart_send_ack_bits(void)
{
	xEventGroupClearBits(uart_ack_wait_evt, UART_ACK_WAIT_ALL_BIT);
	return 0;
}
int wja_set_mqttmsg_uart_send_ack_success_bit(void)
{
	xEventGroupSetBits(uart_ack_wait_evt, UART_ACK_SUCCESS_BIT);
	return 0;
}
int wja_set_mqttmsg_uart_send_ack_fail_bit(void)
{
	xEventGroupSetBits(uart_ack_wait_evt, UART_ACK_FAIL_BIT);
	return 0;
}
mqttmsg_lock_uart_ack_t wja_wait_mqttmsg_uart_send_ack(void)
{
	mqttmsg_lock_uart_ack_t ret = {0};
	if(uart_ack_wait_evt == NULL)
	{
		UART_INFO("uart wait ack evt hdl NULL!");
		ret.ret_status = NTPK_CODE_FAIL;
		return ret;
	}
	EventBits_t uxBits = xEventGroupWaitBits(uart_ack_wait_evt, UART_ACK_WAIT_ALL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
	if(uxBits & UART_ACK_SUCCESS_BIT)
	{		
		UART_INFO("uart wait ack success!!!");
		ret.ret_status = NTPK_CODE_SUCCESS;
		ret.uart_cmd_ret.ret_cmd = g_uart_cmd_ack.ret_cmd;
		ret.uart_cmd_ret.ret_value = g_uart_cmd_ack.ret_value;
		UART_INFO("uart_cmd_ret,cmd:%02x,value:%02x.", ret.uart_cmd_ret.ret_cmd, ret.uart_cmd_ret.ret_value);
	}
	else /* if(uxBits & UART_ACK_FAIL_BIT) */
	{
		UART_INFO("uart wait ack fail!!!");
		ret.ret_status = NTPK_CODE_FAIL;
	}
	return ret;
}

static void tx_task(void *arg)
{
	uint8_t i = 0;
	uint8_t repeat_times = 3;
	signed char send_ret = 0;
	signed int param_len = 0;
	struct un_report_t un_msg;
	uint8_t *param = (uint8_t*)malloc_priv(256);


	while (1)
	{
		repeat_times = 3;
		memset(param, 0, 256);

		//从队列读取
		xQueueReceive(n2uQueue, (void *)&un_msg, portMAX_DELAY);

		if(un_msg.cmd != CMD_VISUAL_MODULE_OR_LOCK_SLEEP_NOTICE)
			system_active_set(ACTIVE_LEVEL_HIGH);
		
		lock_uart_cmd_ret_check_t temp = {0};
		set_uart_last_cmd_ack(temp); //clear
		
		param_len = make_uart_param_quick(&un_msg, param);
		if(param_len < 0)
		{
			UART_INFO("make_uart_param quick fail!!!");
			goto ack_fail_ret;
		}

		bool uart_send_wait_enable = false;

#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
		uint16_t cmd_combine = un_msg.cmd;

		if (0 != un_msg.send_times)
		{
			repeat_times = un_msg.send_times;
		}
		
		if ((CMD_TYPE_REPORT == un_msg.cmd_type) || (CMD_TYPE_RESPONSE == un_msg.cmd_type))
		{
			uart_send_wait_enable = false;
		}
		else
		{
			uart_send_wait_enable = true;
		}
#else
		uint16_t cmd_combine = un_msg.cmd_type;
		
		cmd_combine = (cmd_combine << 8) | (un_msg.cmd);
		UART_INFO("cmd_combine:0x%x.", cmd_combine);

		if(un_msg.cmd_type == CMD_TYPE_REQUEST)
		{
			uart_send_wait_enable = true;
		}
#endif

		if(uart_send_wait_enable)
		{
			for (i = 0; i < repeat_times; i++)
			{
				uart_send_packet(cmd_combine, param, param_len);

				send_ret = 0;
				if (true == uart_wait_for(un_msg.cmd, 50, 8))
				{
					send_ret = 0;
					break;
				}
				else
				{
					send_ret = -1;
				}
			}
			
			ack_fail_ret:
			if((param_len < 0) || (send_ret < 0))
			{				
				UART_WARM("send err:%d, %d.", send_ret, param_len);
				
				wja_set_mqttmsg_uart_send_ack_fail_bit();
			}
			else
			{
				if (CMD_SET_LOCK_BOARD_NET_TIME != un_msg.cmd)
				{
					wja_set_mqttmsg_uart_send_ack_success_bit();
				}
				else
				{
					UART_WARM("cmd = %02x.", un_msg.cmd);
				}
			}
			
			param_len = 0;
		}
		else
		{
			uart_send_packet(cmd_combine, param, param_len);
		}
    }
	
	if(param != NULL)
	{
		free_priv(param);
	}
}

static TaskHandle_t uart_rx_hdl, uart_tx_hdl;
// static bool rx_task_suspend_enable = false;
static uint8_t* rx_task_data = NULL;
static QueueHandle_t           uart_evt_que;
static bool uart_wakeup_flag = 0;
static esp_timer_handle_t uart_sleep_check_timer;

int set_uart_wakeup_flag(bool value)
{
	uart_wakeup_flag = value;
	return 0;
}

static int clear_uart_dirty_data_wakeup_status(void)
{
	if(uart_wakeup_flag)
	{
		system_pm_lock_access(0);
		uart_wakeup_flag = 0;
	}
	return 0;
}

#define SLEEP_CHECK_TIMER_TIMEOUT 8e6
static void uart_wakeup_check_timer_callback(void* arg)
{
	clear_uart_dirty_data_wakeup_status();
	int ret = inRamRwFlash_exec_func((EXEC_CB_FUNC)uart_wakeup_check_timer_delete, NULL, true);
}


int uart_wakeup_check_timer_creat(void)
{
	int ret = 0;
	if(uart_sleep_check_timer == NULL)
	{
		const esp_timer_create_args_t sleep_check_timer_args = {
				.callback = &uart_wakeup_check_timer_callback,
				/* argument specified here will be passed to timer callback function */
				.name = "uart_sleep_check_timer"
		};
		ESP_ERROR_CHECK(esp_timer_create(&sleep_check_timer_args, &uart_sleep_check_timer));
		ESP_ERROR_CHECK(esp_timer_start_once(uart_sleep_check_timer, SLEEP_CHECK_TIMER_TIMEOUT));
	}
	else
	{
        ESP_LOGI(TAG, "uart_sleep_check_timer created");
	}
	return ret;
}

int uart_wakeup_check_timer_delete(void)
{
	int ret = 0;
	if(uart_sleep_check_timer != NULL)
	{
		if(esp_timer_is_active(uart_sleep_check_timer))
		{
			ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_stop(uart_sleep_check_timer));
			ESP_LOGI(TAG,"uart_sleep_check_timer is active");
		}
		else
		{
			ESP_LOGI(TAG,"uart_sleep_check_timer isnot active");
		}
		ESP_ERROR_CHECK(esp_timer_delete(uart_sleep_check_timer));
		uart_sleep_check_timer = NULL;
	}
	else
	{
        ESP_LOGI(TAG, "uart_sleep_check_timer deleted");
	}
	return ret;
}

static void rx_task(void *arg)
{
    rx_task_data = (uint8_t*) malloc_priv(UART_RX_BUF_SIZE+1);
	uart_event_t event;
    if (uart_evt_que == NULL) {
        ESP_LOGE(TAG, "Uart_evt_que is NULL");
        abort();
    }

	while(1) {
        // Waiting for UART event.
        if (xQueueReceive(uart_evt_que, (void * )&event, (TickType_t)portMAX_DELAY)) {
			
            if (wait_wakeup_event_group_sleep_bit() == 0) 
			{
				if(!uart_wakeup_flag)
				{
					system_pm_lock_access(1);
					ESP_LOGW(TAG, "Uart Acquire pm lock");
					uart_wakeup_check_timer_creat();
				}
				uart_wakeup_flag = true;
            }	

            ESP_LOGI(TAG, "Uart%d recved event:%d", MY_UART_NUM, event.type);
            switch(event.type) {
                case UART_DATA:
                    ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                    uart_read_bytes(MY_UART_NUM, rx_task_data, event.size, portMAX_DELAY);
                    rx_task_data[event.size] = '\0';
                    // ESP_LOGI(TAG, "[DATA EVT]: %s", rx_task_data);

					ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, rx_task_data, event.size, ESP_LOG_INFO);
					proto_parse_data(rx_task_data, event.size);
                    break;
                // Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "Hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(MY_UART_NUM);
                    xQueueReset(uart_evt_que);
                    break;
                // Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "Ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(MY_UART_NUM);
                    xQueueReset(uart_evt_que);
                    break;
                // Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGI(TAG, "Uart rx break");
                    break;
                // Event of UART parity check error
                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "Uart parity error");
                    break;
                // Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "Uart frame error");
                    break;
                default:
                    ESP_LOGI(TAG, "Uart event type: %d", event.type);
                    break;
            }
        }
    }
    free_priv(rx_task_data);
	vTaskDelete(NULL);
}
void create_uart_tasks(void)
{
	if(!uart1_start)
	{
		ESP_LOGI("UART", "esp uart1_init not started");
		return;
	}

	if(uart_ack_wait_evt == NULL)
	{
		uart_ack_wait_evt = xEventGroupCreate();
		if(uart_ack_wait_evt != NULL)
		{
			ESP_LOGI(RX_TASK_TAG, "uart_ack_wait_evt created");
		}
	}

#if USE_EXTERN_RAM
	if(uart_rx_hdl == NULL)
	{
		audio_thread_create( &uart_rx_hdl, "uart_rx_task", rx_task, NULL, 5*1024,
				6, true, 1);
		if(uart_rx_hdl == NULL)
		{
			ESP_LOGI(RX_TASK_TAG, "uart_rx_task creat fail");
		}
	}
	else
	{
		ESP_LOGI("UART", "uart_rx_hdl resume");
		vTaskResume(uart_rx_hdl);
	}

	if(uart_tx_hdl == NULL)
	{
		audio_thread_create( &uart_tx_hdl, "uart_tx_task", tx_task, NULL, 5*1024,
            7, true, 1);
		if(uart_tx_hdl == NULL)
		{
			ESP_LOGI(TX_TASK_TAG, "uart_tx_task creat fail");
		}
	}
	else
	{
		ESP_LOGI("UART", "uart_tx_hdl resume");
		vTaskResume(uart_tx_hdl);
	}
#else
	int ret;
    ret = xTaskCreate(rx_task, "uart_rx_task", 1024*5, NULL, 6, &uart_rx_hdl);
	WJA_ASSERT_CHECK_EXT(ret == pdPASS, "uart", "%s. rx_task:fail. \n", __func__);
	
    ret = xTaskCreate(tx_task, "uart_tx_task", 1024*4, NULL, 7, &uart_tx_hdl);	
	WJA_ASSERT_CHECK_EXT(ret == pdPASS, "uart", "%s. tx_task:fail. \n", __func__);
#endif
}

void uart_task_cancel(void)
{
	// WJA_ASSERT_CHECK(uart_tx_hdl != NULL && uart_rx_hdl != NULL);
	if((uart_tx_hdl == NULL )||(uart_rx_hdl == NULL ))
	{
		ESP_LOGW("UART", "uart hdl null");
		return;
	}
#if 1
	vTaskSuspend(uart_tx_hdl);
	vTaskSuspend(uart_rx_hdl);
	// rx_task_suspend_enable = true;//delete
	taskDelay(20);
	vTaskDelete(uart_rx_hdl);
	if(rx_task_data != NULL)
	{
    	free_priv(rx_task_data);
		rx_task_data = NULL;
	}
	uart_rx_hdl = NULL;

	ESP_LOGW("UART", "uart_task_suspend");
#else
	vTaskDelete(uart_tx_hdl);
	vTaskDelete(uart_rx_hdl);
	uart_tx_hdl = uart_rx_hdl = NULL;

	if(u2nQueueStorage != NULL)
	{
		free_priv(u2nQueueStorage);
		u2nQueueStorage = NULL;
	}
	
	if(n2uQueueStorage != NULL)
	{
		free_priv(n2uQueueStorage);
		n2uQueueStorage = NULL;
	}
	ESP_LOGW("UART", "uart_task cancel");
#endif
}

static esp_err_t uart_initialization(void)
{
    uart_config_t uart_cfg = {
#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL_baud_9600
        .baud_rate  = 9600,
#else
        .baud_rate  = 115200,
#endif
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_XTAL,
    };
    //Install UART driver, and get the queue.
    ESP_RETURN_ON_ERROR(uart_driver_install(MY_UART_NUM, UART_RX_BUF_SIZE * 2, UART_RX_BUF_SIZE * 2, 20, &uart_evt_que, 0),
                        TAG, "Install uart failed");
    if (MY_UART_NUM == CONFIG_ESP_CONSOLE_UART_NUM) {
        /* temp fix for uart garbled output, can be removed when IDF-5683 done */
        ESP_RETURN_ON_ERROR(uart_wait_tx_idle_polling(MY_UART_NUM), TAG, "Wait uart tx done failed");
    }
    ESP_RETURN_ON_ERROR(uart_param_config(MY_UART_NUM, &uart_cfg), TAG, "Configure uart param failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(MY_UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        TAG, "Configure uart gpio pins failed");
    return ESP_OK;
}

static esp_err_t uart_wakeup_config(void)
{
    /* UART will wakeup the chip up from light sleep if the edges that RX pin received has reached the threshold
     * Besides, the Rx pin need extra configuration to enable it can work during light sleep */
    ESP_RETURN_ON_ERROR(gpio_sleep_set_direction(RXD_PIN, GPIO_MODE_INPUT), TAG, "Set uart sleep gpio failed");
    ESP_RETURN_ON_ERROR(gpio_sleep_set_pull_mode(RXD_PIN, GPIO_PULLUP_ONLY), TAG, "Set uart sleep gpio failed");
    ESP_RETURN_ON_ERROR(uart_set_wakeup_threshold(MY_UART_NUM, 3), // UART_WAKEUP_THRESHOLD
                        TAG, "Set uart wakeup threshold failed");
    /* Only uart0 and uart1 (if has) support to be configured as wakeup source */
    ESP_RETURN_ON_ERROR(esp_sleep_enable_uart_wakeup(MY_UART_NUM),
                        TAG, "Configure uart as wakeup source failed");
    return ESP_OK;
}


void wja_uart_init(void)
{
	if(uart1_start)
	{
		ESP_LOGI("UART", "esp uart1_init started");
		return;
	}
	uart_initialization();
	uart_wakeup_config();

	/* Avoid gpio dropping and causing interruption when automatically switching from active mode to sleep mode */
    /* See menuconfig: Disable all GPIO when chip at sleep */
    gpio_sleep_sel_dis(RXD_PIN);

#if USE_EXTERN_RAM
	int len = sizeof(struct un_report_t);
	// u2nQueueStorage = (unsigned char*)malloc_priv(len*5);
	// u2nQueue = xQueueCreateStatic(5, len, u2nQueueStorage, &u2nQueueBuffer);
	// if (u2nQueue == NULL)
	// {
	// 	ESP_LOGE("UART", "Create queue failed1");
	// }
	n2uQueueStorage = (unsigned char*)malloc_priv(len*8);
	n2uQueue = xQueueCreateStatic(8, len, n2uQueueStorage, &n2uQueueBuffer);
	if (n2uQueue == NULL)
	{
		ESP_LOGE("UART", "Create queue failed2");
	}
#else
	// create queue
	u2nQueue = xQueueCreate(3, sizeof(struct un_report_t));
	if (u2nQueue == 0)
	{
		ESP_LOGE("UART", "Create queue failed");
	}

	n2uQueue = xQueueCreate(3, sizeof(struct un_report_t));
	if (n2uQueue == 0)
	{
		ESP_LOGE("UART", "Create queue failed");
	}
#endif
	//uart_ack_mutex = xSemaphoreCreateMutex();
	uart_ack_mutex = xSemaphoreCreateCounting(1, 0);
	if(uart_ack_mutex == NULL)
	{
		ESP_LOGE("UART", "uart_ack_mutex Create failed\n");
	}
	// wja_proto_register_init();
	
	uart1_start = true;
	create_uart_tasks();
	lock_sleep_status_store_access_mutex_init();
}

#include "esp_vfs_dev.h"
esp_err_t example_configure_stdin_stdout(void)
{
    // Initialize VFS & UART so we can use std::cout/cin
    setvbuf(stdin, NULL, _IONBF, 0);
    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK( uart_driver_install( (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0) );
    /* Tell VFS to use UART driver */
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
    return ESP_OK;
}

extern int set_module_capture_event(uint8_t *data, int len);
extern void wja_bsp_lcd_reset_init_test(uint8_t u8Pclk, uint8_t u8Vbp);
extern void p2p_sample_exit(void);
extern int p2p_sample_init(void);
extern int set_alarm_event_to_module(uint8_t *data, int len);
extern int capture_pic_thread_deinit(void);

void wja_uart_test(void *pParam, int s32Len)
{
	static uint8_t u8Tmp = 0;
	char *pszParam = (char *)pParam;
	uint8_t au8Data[100] = {0};
	struct un_report_t report = {0};
	uint8_t u8Pclk = 0;
	uint8_t u8Vbp = 0;
	char *pcData = NULL;
	char *pTmp = NULL;
	
	if (strstr(pszParam, "get version"))
	{
		get_module_version_and_uuid(au8Data, 1);	
	}
	else if (strstr(pszParam, "cap event"))
	{
		alarmCaptureEventQueueData_t tMsg = {0};
		
		tMsg.event_type = atoi(&pTmp[strlen("cap event") + 1]);
		tMsg.alarm_pic_type = PIC_JPEG;
		getTimeStamp(&(tMsg.event_time), NULL);
		
		capture_pic_msg_send(&tMsg);
	}
	else if (strstr(pszParam, "cap deinit"))
	{
		capture_pic_thread_deinit();
	}
	else if (strstr(pszParam, "lcd start"))
	{
		pcData = strstr(pszParam, "p=");
		
		u8Pclk = pcData[2] - '0';
		u8Vbp = atoi(&pcData[4]);

		ESP_LOGE("UART", "u8Pclk=%d, u8Vbp=%d\n", u8Pclk, u8Vbp);

		lcd_stop(1);
		
		int ret = lcd_start();
		if(ret == 0)
		{
			// wja_bsp_lcd_reset_init_test(u8Pclk, u8Vbp);
			ucamera_start(LCD_VIDEO_PLAY);
			board_cam_power_enable(true);
		}
	}
	else if (strstr(pszParam, "p2p exit"))
	{
		p2p_sample_exit();
	}
	else if (strstr(pszParam, "p2p start"))
	{
		p2p_sample_init();
	}
	else if (NULL != (pTmp = strstr(pszParam, "alarm event")))
	{	
		au8Data[1] = atoi(&pTmp[strlen("alarm event") + 1]);
		
		set_module_capture_event(au8Data, 2);
	}
	else if (strstr(pszParam, "lock reset"))
	{
		au8Data[1] = 0x06;
		set_alarm_event_to_module(au8Data, 2);
	}
	else if (strstr(pszParam, "provision wifi"))
	{
		au8Data[1] = 1;
		set_module_wifi_provision(au8Data, 2);
	}
	else if (strstr(pszParam, "get mac"))
	{
		get_module_mac(au8Data, 1);
	}
	else if (strstr(pszParam, "get wifi"))
	{
		get_module_wifi_strength(au8Data, 1);
	}
	else if (strstr(pszParam, "get bat"))
	{
		get_module_board_battery_level(au8Data, 1);
	}
	else if (strstr(pszParam, "sync time"))
	{
		get_module_net_time(au8Data, 1);
	}
	else if (strstr(pszParam, "set time"))
	{
		get_lock_board_current_time(au8Data, 1);
	}
	else if (strstr(pszParam, "get lock ver"))
	{
		report.cmd = CMD_GET_LOCK_BOARD_VERSION;
		report.send_times = 3;
		send_n2u_queue(&report);
	}
	else if (strstr(pszParam, "get lock stat"))
	{
		report.cmd = CMD_GET_LOCK_BOARD_LOCK_STATUS;
		report.send_times = 3;
		send_n2u_queue(&report);
	}
	else if (strstr(pszParam, "get main bat"))
	{
		report.cmd = CMD_GET_LOCK_BOARD_MAIN_BAT_LEVEL;
		report.send_times = 3;
		send_n2u_queue(&report);
	}
	else if (strstr(pszParam, "get back bat"))
	{
		report.cmd = CMD_GET_LOCK_BOARD_BACK_BAT_LEVEL;
		report.send_times = 3;
		send_n2u_queue(&report);
	}
	else if (strstr(pszParam, "set pir switch"))
	{
		report.cmd = CMD_SET_LOCK_PIR_MODE;
		report.cmd_type = CMD_TYPE_REQUEST;
		report.payload.char_buff = !u8Tmp;
		report.send_times = 3;
		send_n2u_queue(&report);
	}
	else if (strstr(pszParam, "app unlock"))
	{
		report.cmd = CMD_SET_LOCK_APP_OK_UNLOCK;
		report.cmd_type = CMD_TYPE_REQUEST;
		report.payload.char_buff = 0;
		report.send_times = 3;
		send_n2u_queue(&report);
	}
	else if (strstr(pszParam, "audio test 1"))
	{
		uint8_t u8Open = 1;
		
		mic_speaker_test(&u8Open, 1);
	}
	else if (strstr(pszParam, "audio test 0"))
	{
		uint8_t u8Open = 0;
		
		mic_speaker_test(&u8Open, 1);
	}
	else if (strstr(pszParam, "factory test 1"))
	{
		uint8_t u8Flag = 1;
		
		set_module_factory_test(&u8Flag, 1);
	}
	else if (strstr(pszParam, "factory test 0"))
	{
		uint8_t u8Flag = 0;
		
		set_module_factory_test(&u8Flag, 1);
	}
	else if (strstr(pszParam, "set wifi"))
	{		
		strcpy(gDevInfo.sta_ssid, "wjatest");
		strcpy(gDevInfo.sta_pwd, "test1234");

		set_module_set_wifi(NULL, 0);
	}
	else if (strstr(pszParam, "get strenght"))
	{		
		ESP_LOGE("UART", "strenght");
		get_wifi_strenght(NULL, 0);
	}
	else
	{
		ESP_LOGE("UART", "not support the cmd:%s", pszParam);
	}
}

