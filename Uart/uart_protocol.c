
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "uart_protocol.h"
#include "uart_mgr.h"
#include "wja_lock.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "wja_media.h"

uint8_t g_u8FactoryTest = 0;

static SemaphoreHandle_t lock_sleep_status_store_access_mutex = NULL;
static read_lock_dev_fullRange_attribute_t lock_dev_attribute = {0};
extern void wja_factory_test_wifi_set(char *ssid, char *password);

uint8_t convert_BSD_to_int(uint8_t h)
{
	uint8_t high = 0;
	uint8_t low = 0;
	int dec_num = 0;

	high = (h & 0xF0) >> 4;
	low = (h & 0x0F);
	dec_num = high * 10 + low;

	return dec_num;
}

uint8_t convert_int_to_BSD(uint8_t i)
{
	return ((i % 100 / 10) << 4) | (i % 10);
}



/*
Brief: 获取累加和
*/
uint16_t get_lrc_checksum(uint8_t *data, int len)
{
	uint16_t sum = 0;
	int i = 0;

	if (data == NULL || len == 0)
	{
		return 0;
	}


	for (i = 0; i < len; i++)
	{
		sum += data[i];
	}

	return sum;
}

#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
/*
Brief：生成串口命令
cmd[in],		命令
in_data[in],	参数
in_len[in],		参数的长度(不包括CMD)
out_data[out],	输出数组
out_len[out],	输出数组的长度
*/
int uart_make_entry(int cmd, uint8_t *in_data, int in_len, uint8_t *out_data, int out_len)
{
	uint16_t checksum = 0;

	if (NULL == out_data)
		return 1;
	if(out_len < in_len + UART_HEADER_LEN)
	{
		UART_ERROR("%s failed!! out_len is too short!!\n", __func__);
		return 1;
	}

	out_data[OFFSET_HEADER]	= STX;
	out_data[OFFSET_LEN] = (in_len + 1) >> 8;
	out_data[OFFSET_LEN+1] = (in_len + 1) & 0xFF;
	out_data[OFFSET_CMD] = cmd;

	if (in_data != NULL && in_len > 0)
		memcpy(out_data + OFFSET_PARAM, in_data, in_len);

	//计算checksum
	checksum = get_lrc_checksum(out_data + OFFSET_LEN, in_len+3);
	out_data[OFFSET_LRC] = (checksum & 0xFF00) >> 8;
	out_data[OFFSET_LRC + 1] = (checksum & 0x00FF);

	return 0;
}
#else
/*
Brief：生成串口命令
cmd[in],		命令
in_data[in],	参数
in_len[in],		参数的长度(不包括CMD)
out_data[out],	输出数组
out_len[out],	输出数组的长度
*/
int uart_make_entry(int cmd, uint8_t cmd_type, uint8_t *in_data, int in_len, uint8_t *out_data, int out_len)
{
	uint16_t checksum = 0;

	if (NULL == out_data)
		return 1;
	if(out_len < in_len + UART_HEADER_LEN)
	{
		UART_ERROR("%s failed!! out_len is too short!!\n", __func__);
		return 1;
	}

	out_data[OFFSET_HEADER1]	= STX1;
	out_data[OFFSET_HEADER2]	= STX2;
	out_data[OFFSET_CMD_TYPE]	= cmd_type;

	out_data[OFFSET_LEN1] = (in_len + 3) >> 8;
	out_data[OFFSET_LEN2] = (in_len + 3) & 0x00FF;
	out_data[OFFSET_CMD] = cmd;

	if (in_data != NULL && in_len > 0)
		memcpy(out_data + OFFSET_PARAM, in_data, in_len);

	//计算checksum
	checksum = get_lrc_checksum(out_data + OFFSET_CMD_TYPE, in_len+4);
	out_data[OFFSET_PARAM + in_len] = (checksum & 0x00FF);
	out_data[OFFSET_PARAM + in_len + 1] = (checksum & 0xFF00) >> 8;

	out_data[OFFSET_PARAM + in_len + 2] = END1;
	out_data[OFFSET_PARAM + in_len + 3] = END2;

	return 0;
}
#endif

/*
Brief：生成串口命令, 并且发送
cmd[in],		命令
in_data[in],	参数
in_len[in],		参数的长度(不包括CMD)
*/
int uart_send_packet(int cmd, uint8_t *in_data, int in_len)
{
	char *pdata = NULL;
	int out_len = in_len + UART_HEADER_LEN;
	int ret = 0;

	// UART_INFO("cmd:0x%x.", cmd);

	pdata = (char *)malloc_priv(out_len);
	if (NULL == pdata)
	{
		UART_ERROR("%s malloc failed", __func__);
		return -1;
	}

	#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
		memset(pdata, 0, out_len);
		ret = uart_make_entry(cmd, in_data, in_len, (uint8_t *)pdata, out_len);
	#else
		uint8_t cmd_type_tem = (uint8_t)(cmd >> 8);
		uint8_t cmd_tem = (uint8_t)cmd;
		UART_INFO("cmd_tem:0x%x,cmd_type_tem:0x%x.", cmd_tem, cmd_type_tem);
		memset(pdata, 0, out_len);
		ret = uart_make_entry(cmd_tem, cmd_type_tem, in_data, in_len, (uint8_t *)pdata, out_len);
	#endif
	if (0 == ret)
	{
		// UART_INFO("%s uart send cmd:[0x%x] \n", __func__, cmd);		
		uart_send_data(pdata, out_len);
	}

	free_priv(pdata);
	pdata = NULL;

	return 0;
}

// #if !LOCK_BOARD_UART_WJA_OWN_PROTOCOL
/*---------------------------------uart protocol logic for custom start----------------------------------*/
int lock_report_sleep_status(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);

	return 0;
}
int set_lock_board_wakeup_response(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);


	if (1 == len)
	{
		// wja_lock_visual_module_wakeup_response(0);
		uint8_t dataTmp = 0x00;
		uart_send_packet(CMD_VISUAL_MODULE_OR_LOCK_BOARD_WAKEUP, &dataTmp, 1); //唤醒ack
		taskDelay(20);
	}
	else
	{
		return 0;
	}

	if(sys_state_login_get() != LOGIN_SUCCESS)
	{
		// 同步网络状态 0xA0（设备离线）
		#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL_t31_cmcc_xhj
		#else
		// wja_lock_visual_module_status_sync(DEV_MQTT_OFFLINE_WIFI_CONNECT);
		uint8_t dataTmp = DEV_MQTT_OFFLINE_WIFI_CONNECT;
		uart_send_packet(CMD_VISUAL_MODULE_STATUS_SYNC, &dataTmp, 1); //网络状态report
		#endif
	}
	else
	{
		// 同步网络状态 0xA0（设备在线）
		#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL_t31_cmcc_xhj
		#else
		// wja_lock_visual_module_status_sync(DEV_MQTT_ONLINE);
		uint8_t dataTmp = DEV_MQTT_ONLINE;
		uart_send_packet(CMD_VISUAL_MODULE_STATUS_SYNC, &dataTmp, 1); //网络状态report
		#endif
	}
	return 0;
}

int set_module_wifi_provision(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);

	if(!wja_get_wifi_sta_start_enable())
	{
		wja_lock_visual_module_wifi_provision_status_sync(DEV_WIFI_PROVISION_FAIL);
		return 0;
	}

	if(temp == 0x01)
	{
		//config start
		//WIFI_SET功能相关功能函数	
		#if BLE_WIFI_CFG_SUPPORT
		wja_blufi_init();
		#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
			#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL_t31_cmcc_xhj
			wja_lock_visual_module_wifi_provision_status_sync(0);
			set_module_wifi_provision_status(DEV_WIFI_PROVISIONING);
		    #else
			wja_lock_visual_module_wifi_provision_status_sync(DEV_WIFI_PROVISIONING_CMD_EXE_OK);
			wja_lock_visual_module_status_sync(DEV_WIFI_PROVISIONING);
            #endif
		#else
			wja_lock_visual_module_wifi_provision_status_sync(DEV_WIFI_PROVISIONING);
		#endif
		#endif
	}
	else
	{
		//exit
		#if BLE_WIFI_CFG_SUPPORT
			wifi_ble_prove_exit_timer_start_once(1);
			#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL_t31_cmcc_xhj
				//鑫泓佳那边锁板没主动发退出配网
			#else
				wja_lock_visual_module_wifi_provision_status_sync(DEV_WIFI_PROVISIONING_CMD_EXE_OK);
			#endif
		#endif
	}
	return 0;
}

extern int set_wja_reconnect_flag(uint8_t u8Flag);

int set_module_set_wifi(uint8_t *data, int len)
{
	uint8_t au8RespData[1] = {0};
	wifi_ap_record_t wifi_cfg = {0};

	set_wja_reconnect_flag(0);
	
	ESP_ERROR_CHECK(esp_wifi_disconnect());

	taskDelay(200);
	
	wja_wifi_stop();
	
//	wja_factory_test_wifi_set(gDevInfo.sta_ssid, gDevInfo.sta_pwd);
	
	wja_factory_test_wifi_set((char *)&data[1], (char *)&data[21]);

	ESP_ERROR_CHECK(esp_wifi_start());
	
	// taskDelay(200);
	set_wja_reconnect_flag(1);
	
	ESP_ERROR_CHECK(esp_wifi_connect());

	uart_send_packet(CMD_PROVISE_WIFI, au8RespData, 1);		// 返回上报数据给锁端

	return 0;
}

int get_wifi_strenght(uint8_t *data, int len)
{
	int s32Ret = 0;
	uint8_t au8RespData[2] = {0};
	wifi_ap_record_t wifi_cfg = {0};
		
	s32Ret = esp_wifi_sta_get_ap_info(&wifi_cfg);
	if (ESP_OK == s32Ret)
	{
		au8RespData[0] = 0;
		au8RespData[1] = wifi_cfg.rssi + 100;
	}
	else
	{
		au8RespData[0] = 1;
	}

	uart_send_packet(CMD_GET_WIFI_STRENGHT, au8RespData, 2);		// 返回上报数据给锁端

	BLUFI_INFO("bssid:%d, %d\n", (int)wifi_cfg.rssi + 100, s32Ret);

	return 0;
}

int set_module_wifi_provision_status(uint8_t value)
{
	if(value > 7)
	{
		UART_INFO("wifi_provision_status value illegal");
		return 1;
	}
	#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL_t31_cmcc_xhj
	gDevInfo.current_module_status = value;
	#endif
	return 0;

}

int require_module_provision_wifi_state(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);

	#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL_t31_cmcc_xhj
	wja_lock_visual_module_wifi_provision_status_response(gDevInfo.current_module_status);
	#endif
	return 0;
}

int set_module_default_status(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	#if !LOCK_BOARD_UART_WJA_OWN_PROTOCOL
	if(temp == 0x00)
	{
		//set factory
	}
	else if(temp == 0x01)
	{

	}
	else if(temp == 0x02)
	{
		UART_INFO("esp restart");
		wja_lock_visual_module_default_status_sync(DEV_CMD_EXE_OK);
		vTaskDelay(pdMS_TO_TICKS(500));
		esp_restart();
	}
	#else
	wja_lock_visual_module_default_status_sync(DEV_CMD_EXE_OK);
	#endif
	return 0;
}

int set_module_reset_app_bind(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	return 0;
}

int set_lock_board_default_status_response(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("status ack:%x,len:%d.", temp, len);

	lock_system_reset_upload_server();
	wja_login_success_task_init();

	#if !LOCK_BOARD_UART_WJA_OWN_PROTOCOL
	lock_uart_cmd_ret_check_t check_value = {0};
	check_value.ret_cmd = CMD_SET_LOCK_BOARD_DEFAULT_STATUS;
	check_value.ret_value = temp;
	if(temp == 0x00)
	{

	}
	else if(temp == 0x01)
	{

	}
	else if(temp == 0xF0)
	{

	}
	set_uart_last_cmd_ack(check_value);
	#else
	wja_lock_lock_board_default_status_response(0);
	#endif
	return 0;
}

int get_module_version_and_uuid(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	wja_lock_visual_module_catsEye_version_and_uuid_sync(0);
	return 0;
}


int get_module_version(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	wja_lock_visual_module_catsEye_version_sync(0);
	return 0;
}

int get_module_uuid(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	wja_lock_visual_module_uuid_sync(0);
	return 0;
}

int set_module_uuid(uint8_t *data, int len)
{
	uint8_t temp = data[0];
	UART_INFO("cmd ack:%x,len:%d.", temp, len);

	char *pstr = (char *)data+1;
	int i;
	for(i=0;i<strlen(pstr);i++){
		if(*pstr == '\r' || *pstr == '\n')
			*pstr = '\0';
		pstr++;
	}
	pstr = (char *)data+1;

	if(strlen(pstr) < 10){
		UART_INFO("UUID len failed\r\n");
		goto set_uuid_fail;
	}
	
#if INRAW_RW_FLASH_TASK
	if(0 == inRamRwFlash_Stroge_Write(STROGE_KEY_UUID,pstr, 1+strlen(pstr), true)){
#else
	if(0 == stroge_partion_write(STROGE_KEY_UUID,pstr, 1+strlen(pstr))){
#endif
		UART_INFO("write UUID:%s successed\r\n",pstr);
		wja_lock_visual_module_uuid_set_response(0);
		return 0;
	}
	else
		UART_INFO("write UUID failed\r\n");
set_uuid_fail:
		wja_lock_visual_module_uuid_set_response(1);
	return 1;
}

int set_module_authkey(uint8_t *data, int len)
{
	uint8_t temp = data[0];
	UART_INFO("cmd ack:%x,len:%d.", temp, len);

	char *pstr = (char *)data+1;
	int i;
	for(i=0;i<strlen(pstr);i++){
		if(*pstr == '\r' || *pstr == '\n')
			*pstr = '\0';
		pstr++;
	}
	pstr = (char *)data+1;

	if(strlen(pstr) < 10){
		UART_INFO("AUTHKEY len err failed\r\n");
		goto set_uuid_fail; //set_authkey_fail
	}
	
#if INRAW_RW_FLASH_TASK
	if(0 == inRamRwFlash_Stroge_Write(STROGE_DEV_KEY,pstr, 1+strlen(pstr), true)){
#else
	if(0 == stroge_partion_write(STROGE_DEV_KEY,pstr, 1+strlen(pstr))){
#endif
		UART_INFO("write AUTHKEY:%s successed\r\n",pstr);
		wja_lock_visual_module_authkey_set_response(0);
		return 0;
	}
	else
		UART_INFO("write AUTHKEY failed\r\n");
set_uuid_fail:
		wja_lock_visual_module_authkey_set_response(1);
	return 1;
}

int get_module_mac(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	wja_lock_visual_module_mac_sync(0);
	return 0;
}

int get_module_wifi_strength(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	wja_lock_visual_module_wifi_strength_sync(0);
	return 0;
}

int get_module_board_battery_level(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	//todo
	//adc sample

	set_module_current_status_bit(MODULE_CURRENT_STATUS_BAT, true);

	wja_lock_visual_module_check_battery_sync(50);
	return 0;
}

int get_module_net_time(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	wja_lock_visual_module_net_time_sync(0);
	return 0;
}

int set_module_net_time_resp(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);

	lock_uart_cmd_ret_check_t check_value = {0};
	check_value.ret_cmd = CMD_SET_LOCK_BOARD_NET_TIME;
	check_value.ret_value = temp;
	set_uart_last_cmd_ack(check_value);
	return 0;
}

int get_lock_board_current_time(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	//todo
	return 0;
}

int get_lock_board_version(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);

	memset(lock_dev_attribute.lock_firmware_version, 0x0, sizeof(lock_dev_attribute.lock_firmware_version));
	memcpy(lock_dev_attribute.lock_firmware_version, &data[2], 20);
	UART_INFO("lock_firmware_version:%s.", lock_dev_attribute.lock_firmware_version);

	lock_uart_cmd_ret_check_t check_value = {0};
	check_value.ret_cmd = CMD_GET_LOCK_BOARD_VERSION;
	check_value.ret_value = temp;
	set_uart_last_cmd_ack(check_value);
	return 0;
}


int get_lock_board_lock_status(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);

	temp = data[2];
	UART_INFO("data2:%x.", temp);
	lock_dev_attribute.lock_status = temp;

	temp = data[3];
	UART_INFO("data3:%x.", temp);
	lock_dev_attribute.lock_anti_status = temp;

	lock_uart_cmd_ret_check_t check_value = {0};
	check_value.ret_cmd = CMD_GET_LOCK_BOARD_LOCK_STATUS;
	check_value.ret_value = temp;
	set_uart_last_cmd_ack(check_value);
	return 0;
}

int get_lock_board_user_lsit(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);

	if(temp == 1)
	{
		UART_ERROR("lock board user list execute fail!!");
		return -1;
	}
	else if(temp == 0xf0){
		gDevInfo.lockIsSupportUserList = 0;
		UART_ERROR("lock board user list dont support!!");
		return -1;
	}
	else if(temp == 0){ 
		gDevInfo.lockIsSupportUserList = 1;
	}


	lock_uart_cmd_ret_check_t check_value = {0};
	check_value.ret_cmd = CMD_GET_LOCK_BOARD_USER_LIST;


	uint8_t *ptr = data+2;
	int i = 0;
    int j = 0;
    int k = 0;
	int offset = 0;
	int userCnt = 0;
	int unlockTypeCnt = 0;
    int idCnt = 0;

	LockApiUserInfo *pUserInfo = (LockApiUserInfo*) malloc_priv(sizeof(LockApiUserInfo));
	zdk_memset(pUserInfo, 0x0, sizeof(LockApiUserInfo));

	userCnt = ptr[offset];
	offset++;

	if (userCnt > LOCKAPI_USER_NUM) {
        UART_INFO("userCnt(%d) more than LOCKAPI_USER_NUM(%d)\n", userCnt, LOCKAPI_USER_NUM);
		check_value.ret_value = temp;
        return -1;
    }
	
	pUserInfo->userCnt = userCnt;

	if (userCnt > 0)
	{
		pUserInfo->userAttr = (LockApiUserAttr *)malloc_priv(userCnt*sizeof(LockApiUserAttr));
		if(pUserInfo->userAttr == NULL){
			UART_INFO("LockApiUserAttr,malloc failed!!");
			check_value.ret_value = temp;
			return -1;
		}
	
		for (i = 0; i < userCnt; i++)
		{
			pUserInfo->userAttr[i].userId = ptr[offset];
			offset++;
	
			pUserInfo->userAttr[i].userPriType = ptr[offset];
			offset++;
#if 1
			pUserInfo->userAttr[i].userActive = ptr[offset];
			offset++;
			if(pUserInfo->userAttr[i].userActive != 0)
			{
				pUserInfo->userAttr[i].userActive = 1;
			}
#endif
			unlockTypeCnt = ptr[offset];
			pUserInfo->userAttr[i].unlockTypeCnt = unlockTypeCnt;
			offset++;
	
			for (j = 0; j < unlockTypeCnt; j++)
			{
				//printf("j = %d\n", j);
				pUserInfo->userAttr[i].unlockTypeAttr[j].type =  ptr[offset];
				offset++;
	
				idCnt = ptr[offset];
				pUserInfo->userAttr[i].unlockTypeAttr[j].idCnt = idCnt;
				offset++;
	
				// memcpy(pUserInfo->userAttr[i].unlockTypeAttr[j].idList, &ptr[offset], idCnt);
				// offset += idCnt;
	
				for (int s32i = 0; s32i < idCnt; s32i++)
				{				
					pUserInfo->userAttr[i].unlockTypeAttr[j].idList[s32i] = ptr[offset + s32i];
				}
				
				offset += idCnt;
			}
		}
	
#if 1 //debug
		userCnt = pUserInfo->userCnt;
		for (i = 0; i < userCnt; i++)
		{
			UART_INFO("{\n");
			UART_INFO("\tUserId 	   : %02d\n", pUserInfo->userAttr[i].userId);
			UART_INFO("\tUserPriType   : %d\n", pUserInfo->userAttr[i].userPriType);
			UART_INFO("\tUnLockTypeCnt : %d\n", pUserInfo->userAttr[i].unlockTypeCnt);
			unlockTypeCnt = pUserInfo->userAttr[i].unlockTypeCnt;
	
			for (j = 0; j < unlockTypeCnt; j++)
			{
				UART_INFO("\t{\n");
				UART_INFO("\t\tUnlockType  : %d\n", pUserInfo->userAttr[i].unlockTypeAttr[j].type);
				UART_INFO("\t\tIdCnt	   : %d\n", pUserInfo->userAttr[i].unlockTypeAttr[j].idCnt);
				idCnt = pUserInfo->userAttr[i].unlockTypeAttr[j].idCnt;
				UART_INFO("\t\tIdList	   : ");
				for (k = 0; k < idCnt; k++)
				{
					UART_INFO("%02x ", pUserInfo->userAttr[i].unlockTypeAttr[j].idList[k]);
				}
				UART_INFO("\n");
				UART_INFO("\t}\n");
			}
			UART_INFO("}\n");
		}
#endif
	}

	lock_user_list_info_upload_server(pUserInfo);

	check_value.ret_value = temp;
	set_uart_last_cmd_ack(check_value);
	return 0;
}

int get_lock_board_main_bat_level(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);

	temp = data[2];
	UART_INFO("data2:%x.", temp);
	gDevInfo.battery_value.mainBatteryLevelPer = temp;
	// lock_battery_power_upload_server( temp, VBAT_MAIN7V4);

	temp = data[3];
	UART_INFO("data3:%x.", temp);
	if(temp != 0)
	{
		low_power_t low_power = {0};
		low_power.elect = gDevInfo.battery_value.mainBatteryLevelPer;
		low_power.type = 2;
		lock_low_battery_power_upload_server(&low_power);
	}

	lock_uart_cmd_ret_check_t check_value = {0};
	check_value.ret_cmd = CMD_GET_LOCK_BOARD_MAIN_BAT_LEVEL;
	check_value.ret_value = temp;
	set_uart_last_cmd_ack(check_value);
	return 0;
}

int upload_lock_board_mbat_level_response(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	gDevInfo.battery_value.mainBatteryLevelPer = temp;
	lock_battery_power_upload_server( temp, VBAT_MAIN7V4);

	temp = data[2];
	UART_INFO("data2:%x.", temp);
	if(temp != 0)
	{
		low_power_t low_power = {0};
		low_power.elect = gDevInfo.battery_value.mainBatteryLevelPer;
		low_power.type = 2;
		lock_low_battery_power_upload_server(&low_power);
	}
	wja_lock_lock_board_main_bat_level_set_response(0);
	return 0;
}

int set_module_capture_event(uint8_t *data, int len)
{
	uint8_t ret = 0xf0;
//	bool returnCapVFailFlag = false;
	struct un_report_t tReport = {0};
	alarmCaptureEventQueueData_t tCapPicMsg = {0};

	if((wja_blufi_get_init_status() == 1) || is_ota_process() || getQrAppBindStatus())
	{
		UART_ERROR("blufi|ota|qr bind ing dont capture!");
		ret = 0xf3;
		uart_send_packet(CMD_SET_VISUAL_MODULE_CAPTURE_EVENT, &ret, 1);
		return 0;
	}

#if 0
	if(lcd_is_running())
	{
		returnCapVFailFlag = true;
		ret = 0xf2;
	}
#endif

	//todo capture pic
	static uint8_t eventType = 0;
	eventType = data[1];

	// 由于锁端上报类型与esp上报平台的类型不一致，需要重新赋值（ + 10 ）
	switch(eventType){
		case 0://门铃抓拍
		case 1://撬锁抓拍
		case 6://系统锁定抓拍
		{
			eventType += 10;
			tCapPicMsg.alarm_pic_type = PIC_JPEG;
			tCapPicMsg.event_type = eventType;
			getTimeStamp(&(tCapPicMsg.event_time), NULL);

			if (capture_pic_msg_send(&tCapPicMsg) < 0)
			{
				ret = DEV_CMD_EXE_FAIL;
			}
			else
			{
				ret = DEV_CMD_EXE_OK;
			}
			
			break;
		}

		case 8://PIR告警
		{
			eventType = 0;
			tCapPicMsg.alarm_pic_type = PIC_JPEG;
			tCapPicMsg.event_type = eventType;
			getTimeStamp(&(tCapPicMsg.event_time), NULL);

			if (capture_pic_msg_send(&tCapPicMsg) < 0)
			{
				ret = DEV_CMD_EXE_FAIL;
			}
			else
			{
				ret = DEV_CMD_EXE_OK;
			}
			
			break;
		}
		case 12://指纹开锁抓拍
		case 13://门卡开锁抓拍
		case 14://密码开锁抓拍
		case 15://人脸开锁抓拍
		case 18://胁迫指纹报警抓拍
		case 19://胁迫密码报警抓拍
		{
			tCapPicMsg.alarm_pic_type = PIC_JPEG;
			tCapPicMsg.event_type = eventType;
			getTimeStamp(&(tCapPicMsg.event_time), NULL);

			if (capture_pic_msg_send(&tCapPicMsg) < 0)
			{
				ret = DEV_CMD_EXE_FAIL;
			}
			else
			{
				ret = DEV_CMD_EXE_OK;
			}
			
			break;
		}

		default:
			break;

	}
	
#if 0
	tReport.payload.exe_status = ret;
	tReport.send_times = 1;
	tReport.cmd = CMD_SET_VISUAL_MODULE_CAPTURE_EVENT;
	tReport.u8NoRespone = 1;
	
	send_n2u_queue(&tReport);
#else
	uart_send_packet(CMD_SET_VISUAL_MODULE_CAPTURE_EVENT, &ret, 1);
#endif

	return 0;
}

int set_module_current_status_bit(uint8_t bit, bool bit_value)
{
	if(bit > 7)
	{
		UART_INFO("status bit map illegal");
		return 1;
	}
	if(bit_value)
	{
		gDevInfo.current_status_bits |= (0x01 << bit);
	}
	else
	{
		gDevInfo.current_status_bits &= (~(0x01 << bit));
	}
	return 0;

}

int get_module_current_status(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);

	wja_lock_visual_module_current_status_sync(gDevInfo.current_status_bits);
	return 0;
}

int requeset_module_unlock_lock(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);

	wja_lock_visual_module_unlock_lock_result_sync(1);
	return 0;
}

int requeset_module_lock_lock(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("data1:%x,len:%d.", temp, len);
	
	wja_lock_visual_module_lock_lock_result_sync(1);
	return 0;
}

int set_alarm_event_to_module(uint8_t *data, int len)
{
	uint8_t ret = abs(lock_alarm_event_handle(data+1));
	
	if((ret == 0)||(ret == 1)||(ret == 0xf0)||(ret == 0xfc))
	{
		uart_send_packet(CMD_SET_ALARM_EVNET_TO_VISUAL_MODULE, &ret, 1);		// 返回上报数据给锁端
	}
	if(ret == 2)
	{
		uart_send_packet(CMD_SET_ALARM_EVNET_TO_VISUAL_MODULE, 0, 1);		// 返回上报数据给锁端
		request_user_list_from_mcu(1);
	}

	return 0;
}

int set_open_record_to_module(uint8_t *data, int len)
{
	uint8_t retTmp = 0;
#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL		// 自有协议
	int ret = 0;
	int act_type = 0;
	unsigned int type_id = 0;
	unsigned int user_num_id = 0;
	unlock_info_t info = {0};			// 创建一个开门事件的结构体 info
	uint8_t u8TimeFmt = 0;
	signed char  s8TimeZone = 8;
	int u32TimeStamp = 0;

#if 1
	UART_INFO("len:%d.", len);
	if (len < 32){
		ret = 0xf0;
		goto retLock_goto;
	}
#endif

	memset(&info, 0x0, sizeof(unlock_info_t));
	memcpy(&info.user_name, &data[1], 12);			// 1、上报用户名字属性

	u8TimeFmt = data[13];
	act_type = data[14];		// 开门事件类型
	type_id = (data[29]<<8)+data[15];	// 获取开门方式id
	user_num_id = (data[30]<<8)+data[31];	// 获取用户id
	UART_INFO("act_type : %d, type_id:%04x,user_num_id:%04x,TimeFmt:%d\n", act_type, type_id, user_num_id, u8TimeFmt);

	if (0 == u8TimeFmt)
	{
		struct tm datetime = {0};
		datetime.tm_year = convert_BSD_to_int(data[16]);
		datetime.tm_mon = convert_BSD_to_int(data[17]);
		datetime.tm_mday = convert_BSD_to_int(data[18]);
		datetime.tm_hour = convert_BSD_to_int(data[19]);
		datetime.tm_min = convert_BSD_to_int(data[20]);
		datetime.tm_sec = convert_BSD_to_int(data[21]);
		UART_INFO("year:%d,mon:%d,day:%d,hour:%d,min:%d,sec:%d.", datetime.tm_year, datetime.tm_mon, datetime.tm_mday, datetime.tm_hour, datetime.tm_min, datetime.tm_sec);

		datetime.tm_year += 100;
		datetime.tm_mon -= 1;
		info.event_time = mktime(&datetime);
	}
	else
	{
		s8TimeZone = data[16];
		u32TimeStamp = (data[18] << 24) | (data[19] << 16) | (data[20] << 8) |data[21];
		// info.event_time = u32TimeStamp + 3600 * s8TimeZone;
		info.event_time = u32TimeStamp;
	}
	UART_INFO("event_time:%d.", info.event_time);


	int offect=0;
#if LOCK_OPEN_TYPE_ID_STEP
	if(act_type == 1)					// 密码开锁
	{
		offect = PWD_USER_NUM_OFFET;	
	}
	else if(act_type == 2)				// 指纹开锁
	{
		offect = FP_USER_NUM_OFFET;
	}
	else if(act_type == 3)				// 门卡开锁
	{
		offect = CARD_USER_NUM_OFFET;
	}
	else if(act_type == 7)				// 人脸开锁
	{
		offect = FACE_USER_NUM_OFFET;
	}
	else if(act_type == 9)				// 人脸开锁
	{
		offect = TMP_PWD_USER_NUM_OFFET;
	}
#endif

	info.user_num = user_num_id + offect;

	//发给net
	if(act_type == 4)//wrecked
	{
		UART_INFO("lock been wrecked");
		ret = 0xf0;
		goto retLock_goto;
	}
	if(act_type == 7) //face
	{	
		/* 人脸开锁 */
		act_type = 4;
	}
	if(act_type == 8)
	{
		/* app开锁 */
		act_type = 7;
	}
	if(act_type == 9)
	{
		/* 限时段开锁 */
		act_type = 8;
	}

	if(act_type >= 0x0a)
	{
		UART_INFO("open lock type illgeal");
		ret = 0xf0;
		goto retLock_goto;
	}
	
	bool temp_user_flag = false;
	//user_category, only normal user and tempor user in this cmd[0xe0]
	if(type_id & 0x8000)		// 临时用户
	{
		info.user_category = 2;	// 用户类别（ 普通 或者 临时 用户）
		info.type_id = type_id - 0x8000;	//
		temp_user_flag = true;
	}
	else						// 普通用户
	{
		info.user_category = 1;
		info.type_id = type_id;
	}

	if((act_type == 0) || (act_type == 5) || (act_type == 6) || (act_type == 7))
	{
		info.type_id = 0;
		info.user_num = 0;
	}

	if(temp_user_flag)
	{
		info.open_type = 8;
	}
	else
	{
		info.open_type = act_type;
	}

	ret = lock_user_unlock_record_upload_server(&info); // 上传app成功返回 0，失败： -1
retLock_goto:
	retTmp = abs(ret);
	uart_send_packet(CMD_SET_OPEN_DOOR_RECORD_TO_VISUAL_MODULE, &retTmp, 1);	// 下发给lock的ret值： 上传成功 0， 失败 1, 0xf0 没有定义未能识别的操作;

#else		// 白板协议
	uint8_t lock_open_type = data[1];
	UART_INFO("lock open type:%x,len:%d.", lock_open_type, len);
	uint16_t userid = data[3];
	userid = (userid<<8) | data[2];
	uint8_t lock_open_status = data[4];


	bool check = true;
	unlock_info_t info;
	memset(&info, 0x0, sizeof(unlock_info_t));
	UART_INFO("userid:%x,lock_open_status:%d.", userid, lock_open_status);//lock_open_status hijacked 平台处理劫持告警
	uint16_t userid_tmp = 0;
	switch(lock_open_type)
	{
		case KEY_OPEN: 
		{
			info.open_type 		= 0;
			info.type_id 		= 1;
			userid_tmp = 1;
			break;
		}
		case PWD_OPEN: 
		{
			info.open_type 		= 1;
			info.type_id 		= 1;
			userid_tmp = userid;
			break;
		}
		case FIGPRINT_OPEN: 
		{
			info.open_type 		= 2;
			info.type_id 		= 1;
			userid_tmp = userid;
			break;
		}
		case CARD_OPEN: 
		{
			info.open_type 		= 3;
			info.type_id 		= 1;
			userid_tmp = userid;
			break;
		}
		case FACE_OPEN: 
		{
			info.open_type 		= 4;
			info.type_id 		= 1;
			userid_tmp = userid;
			break;
		}
		case FIG_VEIN_OPEN: 
		{
			info.open_type 		= 14;
			info.type_id 		= 1;
			userid_tmp = userid;
			break;
		}
		case INDOOR_OPEN: 
		{
			info.open_type 		= 6;
			info.type_id 		= 1;
			userid_tmp = userid;
			break;
		}
		case TEMP_PWD_OPEN: 
		{
			info.open_type 		= 8;
			info.type_id 		= 1;
			userid_tmp = 1;
			break;
		}
		case IRIS_OPEN: 
		{
			info.open_type 		= 12;
			info.type_id 		= 1;
			userid_tmp = userid;
			break;
		}
		case PALMPRINT_OPEN: 
		{
			info.open_type 		= 13;
			info.type_id 		= 1;
			userid_tmp = userid;
			break;
		}
		case REMOTE_OPEN: 
		{
			info.open_type 		= 7;
			info.type_id 		= 1;
			userid_tmp = 0;
			break;
		}


		default:
			check = false;
			UART_INFO("lock_open_type dont match");
			break;
	}

	if(check)
	{
		info.user_num 		= userid_tmp;
		info.user_category  = 1;
		int ret = lock_user_unlock_record_upload_server(&info);
		uart_send_packet(CMD_SET_OPEN_DOOR_RECORD_TO_VISUAL_MODULE, abs(ret), 1);		// 返回上报数据给锁端
	}
#endif

	return 0;
}

int set_user_list_to_module(uint8_t *data, int len)
{
	uint8_t temp = data[0];
	UART_INFO("cmd ack:%x,len:%d.", temp, len);
	lock_data_stright_upload_server(data +1, len-1, USER_LIST_SYNC);
	return 0;
}

int requeset_module_verify_password(uint8_t *data, int len)
{
	uint8_t check_ret = 0;
	uint8_t check_id = 0;
	uint8_t *check_passw = (uint8_t *)malloc_priv(8);
	uint8_t *admin_passw = (uint8_t *)malloc_priv(14);

	memset(check_passw, 0x0, 8);
	memset(admin_passw, 0x0, 14);

	if(len > 32)
	{
		LOCK_INFO("data error");
		check_ret = 0xf0;
		check_id = 0;
		goto check_success;
	}

	uint8_t cnt = 1;
	memcpy(check_passw, data + cnt, 6);
	cnt += 7;
	memcpy(admin_passw, data + cnt, 6);
	cnt += 13;

	UART_INFO("check_passw:%s,admin_passw:%s.", check_passw, admin_passw);
	int ret = 0;
	if(len > 21)
	{
		__time_t u32VerifyTimeBCD = 0;
		struct tm datetime = {0};
		datetime.tm_year = convert_BSD_to_int(data[cnt++]);
		datetime.tm_mon = convert_BSD_to_int(data[cnt++]);
		datetime.tm_mday = convert_BSD_to_int(data[cnt++]);
		datetime.tm_hour = convert_BSD_to_int(data[cnt++]);
		datetime.tm_min = convert_BSD_to_int(data[cnt++]);
		datetime.tm_sec = convert_BSD_to_int(data[cnt++]);
		UART_INFO("year:%d,mon:%d,day:%d,hour:%d,min:%d,sec:%d.", datetime.tm_year, datetime.tm_mon, datetime.tm_mday, datetime.tm_hour, datetime.tm_min, datetime.tm_sec);
		if((datetime.tm_year == 0) || (datetime.tm_mon == 0) || (datetime.tm_mday == 0) || (datetime.tm_hour == 0) || (datetime.tm_min == 0) || (datetime.tm_sec == 0)){
			u32VerifyTimeBCD = 0;
		}
		else{
			datetime.tm_year += 100;
			datetime.tm_mon -= 1;
			u32VerifyTimeBCD = mktime(&datetime);
		}
		UART_INFO("verifyTime from BCD:%ld.", u32VerifyTimeBCD);

		signed char s8TimeZone = data[cnt++];
		__time_t u32TimeStamp = 0;
		// u32TimeStamp = (data[cnt++] << 24) | (data[cnt++] << 16) | (data[cnt++] << 8) | data[cnt++];//编译不通过？
		u32TimeStamp += data[cnt++] << 24;
		u32TimeStamp += data[cnt++] << 16;
		u32TimeStamp += data[cnt++] << 8;
		u32TimeStamp += data[cnt++];

		// u32TimeStamp = u32TimeStamp + 3600 * s8TimeZone;

		UART_INFO("verifyTime from timeStamp:%ld, Timezone:%d.", u32TimeStamp, s8TimeZone);
		
		__time_t encryTime = 0;
		unsigned int curTime = 0, diffTime = 0;

		ret = wja_wait_sntp_sync(20);
		if(ret < 0){
			LOCK_INFO("wja_wait_sntp_sync faild!\n");
			check_ret = 1;
			check_id = 0;
			goto check_success;
		}

		curTime = time(NULL);

		if(u32VerifyTimeBCD > TIME_HAS_SYNC)
		{
			encryTime = u32VerifyTimeBCD;
		}
		if(u32TimeStamp > TIME_HAS_SYNC)
		{
			encryTime = u32TimeStamp;
		}

		if(encryTime == 0){
			LOCK_INFO("dont get u32VerifyTime!\n");
			check_ret = 0xf0;
			check_id = 0;
			goto check_success;
		}

		if(curTime > encryTime){
			diffTime = curTime - encryTime;
		}
		else
			diffTime = encryTime - curTime;
		
		LOCK_INFO("curTime:%u, encryTime:%ld, diffTime:%u\n", curTime, encryTime, diffTime);

		if(diffTime > 2)
		{
			LOCK_INFO("diffTime bigger than 2!\n");
			check_ret = 1;
			check_id = 0;
			goto check_success;
		}
	}

	ret = single_temp_pass_verify((char*)admin_passw, (char*)check_passw);
	if(ret == 0)
	{
		check_ret = 0;
		check_id = 0;
		goto check_success;
	}

	ret = time_limited_password_check((char*)check_passw, &check_id);
	if(ret == 0)
	{
		check_ret = 0;
	}
	else// if(ret == -3)
	{
		check_ret = 1;
	}

check_success:
	wja_lock_visual_module_password_verify_sync(check_ret, check_id);
	free_priv(check_passw);
	free_priv(admin_passw);
	return 0;
}

int set_module_app_ok_unlock_response(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("status ack:%x,len:%d.", temp, len);

	lock_uart_cmd_ret_check_t check_value = {0};

	check_value.ret_cmd = CMD_SET_LOCK_APP_OK_UNLOCK;
	check_value.ret_value = temp;
	set_uart_last_cmd_ack(check_value);

	return 0;
}

int set_module_factory_test(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	int ret;
	UART_INFO("status ack:%x,len:%d.", temp, len); 
	
	if(temp == 1)
	{
		if (0 == g_u8FactoryTest)
		{
			UART_WARM("factory test has not started!");
			return 0;
		}
		
		ret = 1;
		printf("Production exit factory test mode!!!\n");

		// 开始休眠倒计时  todo
		// system_task_exit_sleep();
		// TODO
		system_active_set(ACTIVE_LEVEL_ENABLE_SLEEP);
	}
	else
	{
		if (1 == g_u8FactoryTest)
		{
			UART_WARM("factory test had started!");
			return 0;
		}
		
		ret = 0;
		printf("Please note that the device is in production test mode!!!\n");

		system_active_set(ACTIVE_LEVEL_DISABLE_SLEEP);
	}
	
	wja_fastory_test(ret);
	
	g_u8FactoryTest = !temp;
	
	return 0;
}

#if 0
int set_module_factory_test(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("status ack:%x,len:%d.", temp, len);

	g_u8FactoryTest = data[0];

	return 0;
}
#endif

int get_module_factory_test(void)
{
	return g_u8FactoryTest;
}

read_lock_dev_fullRange_attribute_t *get_lock_dev_attribute_table(void)
{
	return &lock_dev_attribute;
}

int lock_setting_info_to_server(lock_send_setting_t *setting_info)
{
	Msg_T tMsg;
	zdk_memset(&tMsg,0x0,sizeof(Msg_T));

	WJA_ASSERT_CHECK(setting_info != NULL && setting_info->set_type < LOCK_SETTING_TYPE_MAX);
	
	//get uuid
	zdk_strcpy(tMsg.device_id, gDevInfo.uuid);
	
	tMsg.command = iot_event_callback;
	tMsg.Msg_u.lock_setting.value = setting_info->value;
	getTimeStamp(&tMsg.event_time, NULL);

	switch(setting_info->set_type)
	{
		case LOCK_SETTING_VOL:
   			 tMsg.union_type = MSG_U_TYPE_LOCK_VOL;	
		     break;
			
		case LOCK_SETTING_BELL_VOL:
   			 tMsg.union_type = MSG_U_TYPE_BELL_VOL;	
		     break;
		
		case LOCK_SETTING_BELL_MUSIC:
	         tMsg.union_type = MSG_U_TYPE_BELL_RING;
			 tMsg.Msg_u.lock_setting.value += 1;
		     break;
		     
		case LOCK_SETTING_ANTI_SWITCH:
			 tMsg.union_type = MSG_U_TYPE_ANTI_SWITCH;
			 break;
			 
		default:
			 LOCK_ERROR("%s No this setting type \n", __func__);
			 break;
	}

    LOCK_INFO("%s union_type:0x%x value:%d \n", __func__, tMsg.union_type, tMsg.Msg_u.lock_setting.value);

	zdk_task_send_data_to_mqtt_back(&tMsg);

	return 0;
}

int set_lock_dev_attribute_table(uint8_t *data, int len)
{
	uint8_t temp = data[0];
	UART_INFO("cmd ack:%x,len:%d.", temp, len);

	uint8_t cnt = 1;
	memset(&lock_dev_attribute, 0x0, sizeof(read_lock_dev_fullRange_attribute_t));

#if !LOCK_BOARD_UART_WJA_OWN_PROTOCOL
	lock_dev_attribute.dev_fullRange_attribute_get_once = true;
	memset(lock_dev_attribute.lock_firmware_version, 0x0, sizeof(lock_dev_attribute.lock_firmware_version));
	memcpy(lock_dev_attribute.lock_firmware_version, &data[cnt], 20);
	UART_INFO("lock_firmware_version:%s.", lock_dev_attribute.lock_firmware_version);
	//todo
	cnt += 16;

	memcpy(lock_dev_attribute.lock_dev_type, &data[cnt], 10);
	UART_INFO("lock_dev_type:%s.", lock_dev_attribute.lock_dev_type);
	cnt += 10;

	lock_dev_attribute.lock_always_status = data[cnt];
	UART_INFO("lock_always_status:%d.", lock_dev_attribute.lock_always_status);
	cnt ++;

	lock_dev_attribute.lock_volume = data[cnt];
	UART_INFO("lock_volume:%d.", lock_dev_attribute.lock_volume);
	cnt ++;

	lock_dev_attribute.lock_auth_mode = data[cnt];
	UART_INFO("lock_auth_mode:%d.", lock_dev_attribute.lock_auth_mode);
	cnt ++;

	lock_dev_attribute.lock_status = data[cnt];
	UART_INFO("lock_status:%d.", lock_dev_attribute.lock_status);
	cnt ++;

	lock_dev_attribute.lock_anti_status = data[cnt];
	UART_INFO("lock_anti_status:%d.", lock_dev_attribute.lock_anti_status);
	cnt ++;

	lock_dev_attribute.lock_board_battery_level = data[cnt];
	UART_INFO("lock_board_battery_level:%d.", lock_dev_attribute.lock_board_battery_level);
	cnt ++;

	lock_dev_attribute.visual_module_battery_level = data[cnt];
	UART_INFO("visual_module_battery_level:%d.", lock_dev_attribute.visual_module_battery_level);
	cnt ++;

	lock_dev_attribute.lock_admin_passw_status = data[cnt];
	UART_INFO("lock_admin_passw_status:%d.", lock_dev_attribute.lock_admin_passw_status);
	cnt ++;

	lock_dev_attribute.lock_dev_language = data[cnt];
	UART_INFO("lock_dev_language:%d.", lock_dev_attribute.lock_dev_language);
	cnt ++;
#else
	lock_dev_attribute.lock_volume = data[1];
	UART_INFO("lock_volume:%d.", lock_dev_attribute.lock_volume);

	lock_dev_attribute.lock_anti_status = data[5];
	UART_INFO("lock_anti_status:%d.", lock_dev_attribute.lock_anti_status);

	lock_dev_attribute.lock_dev_language = data[8];
	UART_INFO("lock_dev_language:%d.", lock_dev_attribute.lock_dev_language);
#endif
	return 0;
}

int upload_lock_setting_info(uint8_t *data, int len)
{
	uint8_t offset = 0;
	uint8_t param[1] = {0};
	lock_setting_info_t setting_data;

	WJA_ASSERT_CHECK(data != NULL);
	UART_INFO("%s \n", __func__);
	
	offset++; //cmd	
	offset++; //ret
	
	memset(&setting_data, 0, sizeof(setting_data));
	for(int i = 0; i < sizeof(setting_data.set_info) / sizeof(lock_send_setting_t); i++)
	{	
		setting_data.set_info[i].set_type = i;
		setting_data.set_info[i].value = data[offset++];
		UART_INFO("%s setting_item[%d]:%d \n", __func__, i, setting_data.set_info[i].value);
	}
	
	lock_setting_info_to_server(&setting_data.set_info[LOCK_SETTING_VOL]);		
	lock_setting_info_to_server(&setting_data.set_info[LOCK_SETTING_BELL_VOL]);		
	lock_setting_info_to_server(&setting_data.set_info[LOCK_SETTING_ANTI_SWITCH]);
	lock_setting_info_to_server(&setting_data.set_info[LOCK_SETTING_BELL_MUSIC]);

	return 0;
}

int set_lock_dev_attribute_response(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("status ack:%x,len:%d.", temp, len);

	lock_uart_cmd_ret_check_t check_value = {0};

	check_value.ret_cmd = CMD_SET_LOCK_FULL_RANGE_ATTRIBUTE;
	check_value.ret_value = temp;
	set_uart_last_cmd_ack(check_value);

	return 0;
}

int read_power_save_mode_parse(uint8_t *data, int len)
{
	UART_INFO("read_power_save_mode_parse\n");
	/* 省电模式由可视模块实现，不需要实现本命令 */
	
	return 0;
}

int set_pir_switch_parse(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("status ack:%x,len:%d.", temp, len);

	lock_uart_cmd_ret_check_t check_value = {0};

	check_value.ret_cmd = CMD_SET_LOCK_PIR_MODE;
	check_value.ret_value = temp;
	set_uart_last_cmd_ack(check_value);

	return 0;
}

int set_cmei(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("status ack:%x,len:%d.", temp, len);
	
	uint8_t buff[20];
	memcpy(gDevInfo.cmei, data + 1, 20);
	printf("cmei: %s !!!", gDevInfo.cmei);

	struct un_report_t report;
	bzero(&report, sizeof(struct un_report_t));
	report.cmd = CMD_SET_CMEI;
	report.send_times = 1;
	report.u8NoRespone = 1;	
	
	#if INRAW_RW_FLASH_TASK
	if(0 == inRamRwFlash_Stroge_Write(STROGE_DEV_CMEI, gDevInfo.cmei, 1+strlen(gDevInfo.cmei), true)){
#else
	if(0 == stroge_partion_write(STROGE_DEV_CMEI,cmei, 1+strlen(cmei))){
#endif
		UART_INFO("write CMEI:%s successed\r\n",gDevInfo.cmei);
		report.payload.exe_status = 0x00;
	}
	else
	{
		UART_INFO("write CMEI:%s successed\r\n",gDevInfo.cmei);
		report.payload.exe_status = 0x01;
	}
	send_n2u_queue(&report);
	return 0;
}

int get_cmei(uint8_t *data, int len)
{
	uint8_t temp = data[1];
	UART_INFO("status ack:%x,len:%d.", temp, len);

	printf("Start read product's defualt CMEI.\n");

#if INRAW_RW_FLASH_TASK
	inRamRwFlash_Stroge_Read(STROGE_DEV_CMEI, gDevInfo.cmei, sizeof(gDevInfo.cmei));
#else
	stroge_partion_read(STROGE_DEV_CMEI, gDevInfo.cmei, sizeof(gDevInfo.cmei));
#endif
	gDevInfo.dev_key[sizeof(gDevInfo.dev_key) - 1] = '\0';
	UART_INFO("Read CMEI:%s ", gDevInfo.cmei);

	struct un_report_t report;
	bzero(&report, sizeof(struct un_report_t));

	report.cmd = CMD_GET_CMEI;
	report.send_times = 1;
	report.u8NoRespone = 1;

	send_n2u_queue(&report);

	return 0;
}

int get_module_authkey(uint8_t *data, int len)
{
	
	uint8_t temp = data[1];
	UART_INFO("status ack:%x,len:%d.", temp, len);

	printf("Start read product's defualt authkey.\n");

	#if INRAW_RW_FLASH_TASK
	inRamRwFlash_Stroge_Read(STROGE_DEV_KEY, gDevInfo.dev_key, sizeof(gDevInfo.dev_key));
#else
	stroge_partion_read(STROGE_DEV_KEY, gDevInfo.dev_key, sizeof(gDevInfo.dev_key));
#endif
	printf("Factory test Read AUTHKEY: %s", gDevInfo.dev_key);

	struct un_report_t report;
	bzero(&report, sizeof(struct un_report_t));

	report.cmd = CMD_GET_VISUAL_MODULE_AUTHKEY;
	report.send_times = 1;
	report.u8NoRespone = 1;

	send_n2u_queue(&report);	
	
	return 0;
}

int mic_speaker_test(uint8_t *data, int len)
{
	uint8_t u8ErrCode = 0;
	int s32Ret = 0;
	uint8_t u8Open = data[1];

	if (1 == u8Open)
	{
		s32Ret = wja_media_factory_test_audio_open();
	}
	else
	{
		s32Ret = wja_media_factory_test_audio_close();
	}

	if (0 == s32Ret)
	{
		u8ErrCode = 0;
	}
	else
	{
		u8ErrCode = 1;
	}
	
	uart_send_packet(CMD_MIC_SPEAKER_TEST, &u8ErrCode, 1);

	return 0;
}

static struct value_func_map_t g_uart_map[] = {
	{CMD_VISUAL_MODULE_OR_LOCK_SLEEP_NOTICE,	lock_report_sleep_status, 		NULL, NULL},
	{CMD_VISUAL_MODULE_OR_LOCK_BOARD_WAKEUP,	set_lock_board_wakeup_response, NULL, NULL},
	{CMD_SET_VISUAL_MODULE_WIFI_PROVISION,		set_module_wifi_provision,	  	NULL, NULL},
	{CMD_SET_VISUAL_MODULE_RESET_APP_BIND,		set_lock_board_default_status_response,	  	NULL, NULL},
	// {CMD_SET_LOCK_BOARD_DEFAULT_STATUS,			set_lock_board_default_status_response,	  	NULL, NULL},
	{CMD_SET_VISUAL_MODULE_DEFAULT_STATUS,		set_module_default_status,	  	NULL, NULL},
	{CMD_GET_VISUAL_MODULE_VERSION_AND_UUID,	get_module_version_and_uuid,	NULL, NULL},
	{CMD_GET_VISUAL_MODULE_VERSION,				get_module_version,	  			NULL, NULL},
	{CMD_GET_VISUAL_MODULE_UUID,				get_module_uuid,	  			NULL, NULL},
	{CMD_SET_VISUAL_MODULE_UUID,				set_module_uuid,	  			NULL, NULL},
	{CMD_SET_VISUAL_MODULE_AUTHKEY,				set_module_authkey,	  			NULL, NULL},
	{CMD_GET_VISUAL_MODULE_MAC,					get_module_mac,	  				NULL, NULL},
	{CMD_GET_VISUAL_MODULE_WIFI_STRENGTH,		get_module_wifi_strength,	  	NULL, NULL},
	{CMD_GET_VISUAL_MODULE_BATTERY_LEVEL,		get_module_board_battery_level,	NULL, NULL},
	{CMD_GET_VISUAL_MODULE_NET_TIME,			get_module_net_time,			NULL, NULL},
	{CMD_SET_LOCK_BOARD_NET_TIME,				set_module_net_time_resp,		NULL, NULL},
	{CMD_GET_LOCK_BOARD_CURRENT_TIME,			get_lock_board_current_time,	NULL, NULL},
	{CMD_GET_LOCK_BOARD_VERSION,				get_lock_board_version,	  		NULL, NULL},
	{CMD_GET_LOCK_BOARD_LOCK_STATUS,			get_lock_board_lock_status,	  	NULL, NULL},
	{CMD_GET_LOCK_BOARD_USER_LIST,				get_lock_board_user_lsit,	  	NULL, NULL},
	{CMD_GET_LOCK_BOARD_MAIN_BAT_LEVEL,			get_lock_board_main_bat_level,	NULL, NULL},
	{CMD_UPLOAD_LOCK_BOARD_MBAT_LEVEL,			upload_lock_board_mbat_level_response,	NULL, NULL},
	{CMD_SET_VISUAL_MODULE_CAPTURE_EVENT,		set_module_capture_event,		NULL, NULL},

	{CMD_GET_VISUAL_MODULE_CURRENT_STATUS,		get_module_current_status,		NULL, NULL},
	{CMD_REQUESET_VISUAL_MODULE_UNLOCK_LOCK,	requeset_module_unlock_lock,	NULL, NULL},
	{CMD_REQUESET_VISUAL_MODULE_LOCK_LOCK,		requeset_module_lock_lock,		NULL, NULL},
	{CMD_SET_ALARM_EVNET_TO_VISUAL_MODULE,		set_alarm_event_to_module,		NULL, NULL},
	{CMD_SET_OPEN_DOOR_RECORD_TO_VISUAL_MODULE,	set_open_record_to_module,		NULL, NULL},
	{CMD_SET_USER_LIST_TO_VISUAL_MODULE,		set_user_list_to_module,		NULL, NULL},
	{CMD_REQUESET_VISUAL_MODULE_VERIFY_PASSW,	requeset_module_verify_password,NULL, NULL},
	{CMD_SET_LOCK_APP_OK_UNLOCK,				set_module_app_ok_unlock_response,			NULL, NULL},
	{CMD_VISUAL_MODULE_FACTORY_TEST,			set_module_factory_test,		NULL, NULL},
	// {CMD_GET_LOCK_FULL_RANGE_ATTRIBUTE,			set_lock_dev_attribute_table,	NULL, NULL},
	{CMD_GET_LOCK_FULL_RANGE_ATTRIBUTE,			upload_lock_setting_info,	NULL, NULL},
	{CMD_SET_LOCK_FULL_RANGE_ATTRIBUTE,			set_lock_dev_attribute_response, NULL, NULL},
	// {CMD_GET_POWER_SAVE, 	    				read_power_save_mode_parse,     NULL, NULL},
	{CMD_SET_LOCK_PIR_MODE, 	    			set_pir_switch_parse,      		NULL, NULL},
	{CMD_SET_CMEI, 	    						set_cmei,			      		NULL, NULL},
	{CMD_GET_CMEI,								get_cmei,						NULL, NULL},
	{CMD_GET_VISUAL_MODULE_AUTHKEY,				get_module_authkey,				NULL, NULL},
	{CMD_MIC_SPEAKER_TEST, 	    				mic_speaker_test,      		NULL, NULL},
	{CMD_PROVISE_WIFI, 	    					set_module_set_wifi, 		NULL, NULL},	
	{CMD_GET_WIFI_STRENGHT, 	    			get_wifi_strenght, 	NULL, NULL},	
	{CMD_REQUIRE_WIFI_PROVISION_STAT, 	    	require_module_provision_wifi_state, 	NULL, NULL},	

};

// #endif
/*---------------------------------uart protocol logic for custom over----------------------------------*/

/*
解析数据
返回：失败返回-1，成功返回最后一个被解析报文的最后一个字节索引
*/
int proto_parse_data(uint8_t *in_data, int in_len)
{
	uint8_t cmd = 0;
	uint8_t cmd_idx = 0;
	uint8_t head_idx = 0;
	uint16_t cal_checksum = 0;
	uint16_t src_checksum = 0;
	uint16_t packet_len = 0;
	bool is_found = 0;
	int ret = 0;
	int exit_idx = -1;
	int i = 0;

	if (NULL == in_data || 0 == in_len)
		return -1;
	
	//循环查找
	for(i = 0; i < in_len; i++)
	{
		#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
		if (in_data[i] != STX)
		{	
			continue;
		}

		head_idx = i;

		//检查checksum
		packet_len = (in_data[head_idx+OFFSET_LEN] << 8) + in_data[head_idx+OFFSET_LEN+1];
		if (head_idx + OFFSET_LEN > in_len )
			continue;

		src_checksum = (in_data[ head_idx + OFFSET_LRC ] << 8) + in_data[ head_idx + OFFSET_LRC + 1];
		cal_checksum = get_lrc_checksum(in_data + head_idx + OFFSET_LEN, packet_len + 2);

		if (src_checksum != cal_checksum)
		{
			UART_ERROR("uart checksum wrong, cal/src is 0x%04X/0x%04X \n",  cal_checksum, src_checksum);
			continue;
		}
		
		exit_idx = head_idx + 4 + packet_len;
		#else
		if (in_data[i] != STX1)
		{	
			continue;
		}

		if (in_data[i+1] != STX2)
		{	
			continue;
		}

		if ((in_data[i+2] != 0x01) && (in_data[i+2] != 0x02) && (in_data[i+2] != 0x07)) // response packet
		{	
			continue;
		}

		head_idx = i;

		//检查checksum
		packet_len = (in_data[head_idx+OFFSET_LEN1] << 8) + in_data[head_idx+OFFSET_LEN2];
		if (head_idx + OFFSET_LEN2 > in_len )
			continue;
		
		cal_checksum = get_lrc_checksum(in_data + head_idx + OFFSET_CMD_TYPE, packet_len + 1);
		src_checksum = (in_data[ head_idx + packet_len + 3]) + (in_data[ head_idx + packet_len + 4]<< 8);

		if (src_checksum != cal_checksum)
		{
			UART_ERROR("uart checksum wrong, cal/src is 0x%04X/0x%04X \n",  cal_checksum, src_checksum);
			continue;
		}

		if (in_data[head_idx + packet_len + 5] != END1)
		{	
			UART_ERROR("uart END1 wrong");
			continue;
		}

		if (in_data[head_idx + packet_len + 6] != END2)
		{	
			UART_ERROR("uart END2 wrong");
			continue;
		}

		exit_idx = head_idx + 7 + packet_len;
		#endif
		is_found = true;
		is_found = is_found; //avoid compiler error

		//获取cmd
		cmd = in_data[ head_idx + OFFSET_CMD ];
		UART_INFO("recv uart cmd:[%x] \n", cmd);


		//根据cmd调用对应的处理函数
		for (cmd_idx = 0; cmd_idx < NUMBER_OF(g_uart_map); cmd_idx++)
		{
			if (cmd != 0x00)
			{
				if (g_uart_map[cmd_idx].cmd == cmd && g_uart_map[cmd_idx].parse_func)
				{	
					if(cmd == CMD_VISUAL_MODULE_OR_LOCK_SLEEP_NOTICE)
					{
						wja_set_lock_board_sleep_status_store(true);	
					}
					else
					{
						// system_sleep_task_resume();
						system_active_set(ACTIVE_LEVEL_HIGH);
                		UART_WARM("Send uart wakeup group event");
						set_wakeup_event_group_uart_bit();
						set_uart_wakeup_flag(0);
						// int ret = inRamRwFlash_exec_func((EXEC_CB_FUNC)uart_wakeup_check_timer_delete, NULL, true);
						uart_wakeup_check_timer_delete();
						wja_set_lock_board_sleep_status_store(false);	
					}
					#if LOCK_BOARD_UART_WJA_OWN_PROTOCOL
					ret = g_uart_map[cmd_idx].parse_func( in_data + head_idx + OFFSET_CMD, packet_len );
					#else
					ret = g_uart_map[cmd_idx].parse_func( in_data + head_idx + OFFSET_CMD, packet_len - 2);	
					#endif			
					UART_INFO("%s parse[%02X] ret is %d\n", __func__, cmd, ret);

					if(g_uart_map[cmd_idx].cb_func != NULL)
					{	
						g_uart_map[cmd_idx].cb_func();
					}	
					break;
				}
			}
		}

		if (cmd_idx == NUMBER_OF(g_uart_map) )
		{
			UART_ERROR("not found cmd\n");
		}
	}
	
	return exit_idx;
}

bool proto_register_callback(LOCK_CMD_ENUM cmd, CALLBACK_FUNC cb)
{
	int i = 0;
	bool ret = false;

	for (i = 0; i < NUMBER_OF(g_uart_map); i++)
	{
		if (g_uart_map[i].cmd == cmd)
		{
			g_uart_map[i].cb_func = cb;
			ret = true;
			break;
		}
	}

	return ret;
}

int lock_sleep_status_store_access_mutex_init(void)
{
	lock_sleep_status_store_access_mutex = xSemaphoreCreateMutex();
	if(lock_sleep_status_store_access_mutex == NULL)
		UART_INFO("lock_sleep_status_store_access_mutex Create failed\n");

	return 0;
}
int lock_sleep_status_store_access_mutex_take(void)
{
	int ret = 0;

	// UART_INFO("%s,%d\n", __func__, __LINE__);
	if(lock_sleep_status_store_access_mutex != NULL)
	{
		if( xSemaphoreTake( lock_sleep_status_store_access_mutex, portMAX_DELAY) == pdTRUE )
		{
			// UART_INFO("%s,%d\n", __func__, __LINE__);
		}
	}
	else
	{
		// UART_INFO("%s,%d,mutex NULL\n", __func__, __LINE__);
		ret = -1;
	}
	return ret;
}
int lock_sleep_status_store_access_mutex_release(void)
{
	int ret = 0;
	// UART_INFO("%s,%d\n", __func__, __LINE__);
	if(lock_sleep_status_store_access_mutex != NULL)
	{
		if( xSemaphoreGive( lock_sleep_status_store_access_mutex) == pdTRUE )
		{
			// UART_INFO("%s,%d\n", __func__, __LINE__);
		}
	}
	else
	{
		// UART_INFO("%s,%d,mutex NULL\n", __func__, __LINE__);
		ret = -1;
	}
	return ret;
}

