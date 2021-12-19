﻿#include "headers.hpp"

#include "scenes/video_player.hpp"
#include "scenes/search.hpp"
#include "scenes/channel.hpp"
#include "scenes/about.hpp"
#include "scenes/setting_menu.hpp"
#include "scenes/watch_history.hpp"
#include "scenes/subscription.hpp"
#include "network/network_io.hpp"
#include "network/thumbnail_loader.hpp"
#include "system/util/async_task.hpp"
#include "system/util/misc_tasks.hpp"
#include "ui/colors.hpp"
// add here

bool menu_thread_run = false;
bool menu_check_exit_request = false;
bool menu_update_available = false;
Thread menu_worker_thread, menu_check_connectivity_thread, menu_update_thread, thumbnail_downloader_thread, async_task_thread, misc_tasks_thread;
C2D_Image menu_app_icon[4];

static SceneType current_scene;

void Menu_check_connectivity_thread(void* arg);
void Menu_worker_thread(void* arg);
void Menu_update_thread(void* arg);

static Result sound_init_result;
static bool is_new_3ds;

void Menu_init(void)
{
	Result_with_string result;
	
	Util_log_init();
	Util_log_save(DEF_MENU_INIT_STR, "Initializing..." + DEF_CURRENT_APP_VER);

	osSetSpeedupEnable(true);
	aptSetSleepAllowed(true);
	svcSetThreadPriority(CUR_THREAD_HANDLE, DEF_THREAD_PRIORITY_HIGH - 1);

	Util_log_save(DEF_MENU_INIT_STR, "fsInit()...", fsInit());
	Util_log_save(DEF_MENU_INIT_STR, "acInit()...", acInit());
	Util_log_save(DEF_MENU_INIT_STR, "aptInit()...", aptInit());
	Util_log_save(DEF_MENU_INIT_STR, "mcuHwcInit()...", mcuHwcInit());
	Util_log_save(DEF_MENU_INIT_STR, "ptmuInit()...", ptmuInit());
	{
		constexpr int SOC_BUFFERSIZE = 0x100000;
		u32 *soc_buffer = (u32 *) memalign(0x1000, SOC_BUFFERSIZE);
		if (!soc_buffer) Util_log_save(DEF_MENU_INIT_STR, "soc buffer out of memory");
		else Util_log_save(DEF_MENU_INIT_STR, "socInit()...", socInit(soc_buffer, SOC_BUFFERSIZE));
	}
	Util_log_save(DEF_MENU_INIT_STR, "sslcInit()...", sslcInit(0));
	Util_log_save(DEF_MENU_INIT_STR, "httpcInit()...", httpcInit(0x500000));
	Util_log_save(DEF_MENU_INIT_STR, "romfsInit()...", romfsInit());
	Util_log_save(DEF_MENU_INIT_STR, "cfguInit()...", cfguInit());
	Util_log_save(DEF_MENU_INIT_STR, "amInit()...", amInit());
	Util_log_save(DEF_MENU_INIT_STR, "ndspInit()...", (sound_init_result = ndspInit()));//0xd880A7FA
	Util_log_save(DEF_MENU_INIT_STR, "APT_SetAppCpuTimeLimit()...", APT_SetAppCpuTimeLimit(30));
	
	APT_CheckNew3DS(&is_new_3ds);
	
	
	Util_log_save(DEF_MENU_INIT_STR, "Draw_init()...", Draw_init(var_high_resolution_mode).code);
	Draw_frame_ready();
	Draw_screen_ready(0, DEF_DRAW_WHITE);
	Draw_screen_ready(1, DEF_DRAW_WHITE);
	Draw_apply_draw();

	Util_hid_init();
	Util_expl_init();
	Exfont_init();
	for (int i = 0; i < DEF_EXFONT_NUM_OF_FONT_NAME; i++)
		Exfont_set_external_font_request_state(i, true);

	for(int i = 0; i < 4; i++)
		Exfont_set_system_font_request_state(i, true);

	Exfont_request_load_external_font();
	Exfont_request_load_system_font();
	
	/*
	if (var_allow_send_app_info)
		menu_send_app_info_thread = threadCreate(Menu_send_app_info_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_LOW, 1, true);
	*/

	result = Draw_load_texture("romfs:/gfx/draw/app_icon.t3x", 60, menu_app_icon, 0, 4);
	Util_log_save(DEF_MENU_INIT_STR, "Draw_load_texture()..." + result.string + result.error_description, result.code);

	menu_thread_run = true;
	menu_worker_thread = threadCreate(Menu_worker_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_REALTIME, 1, false);
	menu_check_connectivity_thread = threadCreate(Menu_check_connectivity_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 1, false);
	menu_update_thread = threadCreate(Menu_update_thread, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_REALTIME, 1, false);
	
	Sem_init();
	Sem_suspend();
	VideoPlayer_init();
	VideoPlayer_suspend();
	Channel_init();
	Channel_suspend();
	About_init();
	About_suspend();
	History_init();
	History_suspend();
	Subscription_init();
	Subscription_suspend();
	// add here
	Search_init(); // first running
	current_scene = SceneType::SEARCH;
	
	thumbnail_downloader_thread = threadCreate(thumbnail_downloader_thread_func, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	async_task_thread = threadCreate(async_task_thread_func, NULL, DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	misc_tasks_thread = threadCreate(misc_tasks_thread_func, NULL, DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);

	Menu_get_system_info();

	Util_log_save(DEF_MENU_INIT_STR, "Initialized");
}

void Menu_exit(void)
{
	Util_log_save(DEF_MENU_EXIT_STR, "Exiting...");
	u64 time_out = 10000000000;
	Result_with_string result;

	menu_thread_run = false;

	VideoPlayer_exit();
	Channel_exit();
	Search_exit();
	Sem_exit();
	About_exit();
	History_exit();
	Subscription_exit();
	// add here

	Util_hid_exit();
	Util_expl_exit();
	Exfont_exit();

	thumbnail_downloader_thread_exit_request();
	async_task_thread_exit_request();
	misc_tasks_thread_exit_request();
	Util_log_save(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(menu_worker_thread, time_out));
	Util_log_save(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(menu_check_connectivity_thread, time_out));
	// Util_log_save(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(menu_send_app_info_thread, time_out));
	Util_log_save(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(menu_update_thread, time_out));
	Util_log_save(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(thumbnail_downloader_thread, time_out));
	Util_log_save(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(async_task_thread, time_out));
	Util_log_save(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(misc_tasks_thread, time_out));
	threadFree(menu_worker_thread);
	threadFree(menu_check_connectivity_thread);
	// threadFree(menu_send_app_info_thread);
	threadFree(menu_update_thread);
	threadFree(thumbnail_downloader_thread);
	threadFree(async_task_thread);
	threadFree(misc_tasks_thread);
	
	NetworkSessionList::at_exit();

	fsExit();
	acExit();
	aptExit();
	mcuHwcExit();
	ptmuExit();
	httpcExit();
	romfsExit();
	cfguExit();
	amExit();
	ndspExit();
	sslcExit();
	socExit();
	Draw_exit();

	Util_log_save(DEF_MENU_EXIT_STR, "Exited.");
}

static std::vector<Intent> scene_stack = {{SceneType::SEARCH, ""}};

bool Menu_main(void)
{
	if (sound_init_result != 0 || !is_new_3ds) {
		std::string error_msg;
		if (!is_new_3ds) error_msg = "This app only supports New 3DSes due to performance issues";
		else error_msg = std::string("Could not initialize NDSP (sound service)\nThis is probably because you haven't run DSP1\n") +
			"You can download it from the link below\n\nhttps://github.com/zoogie/DSP1/releases/";
		error_msg += "\n\nPress A to close the app";
		
		Hid_info key;
		Util_hid_query_key_state(&key);
		Util_hid_key_flag_reset();
		
		Draw_frame_ready();
		Draw_screen_ready(0, DEF_DRAW_WHITE);
		
		{
			int width = Draw_get_width(error_msg, 0.5, 0.5);
			int height = Draw_get_height(error_msg, 0.5, 0.5);
			Draw(error_msg, (400 - width) / 2, (240 - height) / 2, 0.5, 0.5, DEF_DRAW_BLACK);
		}
		Draw_top_ui();

		Draw_screen_ready(1, DEF_DRAW_WHITE);
		
		Draw_apply_draw();
		
		if (key.h_a) return false;
		return true;
	}
	
	if (var_show_fps) sprintf(var_status, "%02dfps %04d/%02d/%02d %02d:%02d:%02d ", (int)Draw_query_fps(), var_years, var_months, var_days, var_hours, var_minutes, var_seconds);
	else sprintf(var_status, "%04d/%02d/%02d %02d:%02d:%02d ", var_years, var_months, var_days, var_hours, var_minutes, var_seconds);
	
	if(var_debug_mode)
		var_need_reflesh = true;
	
	Intent intent;
	if (current_scene == SceneType::VIDEO_PLAYER) {
		intent = VideoPlayer_draw();
		if (intent.next_scene != SceneType::NO_CHANGE) VideoPlayer_suspend();
	} else if (current_scene == SceneType::SEARCH) {
		intent = Search_draw();
		if (intent.next_scene != SceneType::NO_CHANGE) Search_suspend();
	} else if (current_scene == SceneType::CHANNEL) {
		intent = Channel_draw();
		if (intent.next_scene != SceneType::NO_CHANGE) Channel_suspend();
	} else if (current_scene == SceneType::SETTINGS) {
		intent = Sem_draw();
		if (intent.next_scene != SceneType::NO_CHANGE) Sem_suspend();
	} else if (current_scene == SceneType::ABOUT) {
		intent = About_draw();
		if (intent.next_scene != SceneType::NO_CHANGE) About_suspend();
	} else if (current_scene == SceneType::HISTORY) {
		intent = History_draw();
		if (intent.next_scene != SceneType::NO_CHANGE) History_suspend();
	} else if (current_scene == SceneType::SUBSCRIPTION) {
		intent = Subscription_draw();
		if (intent.next_scene != SceneType::NO_CHANGE) Subscription_suspend();
	}
	// add here
	
	if (intent.next_scene == SceneType::EXIT) return false;
	else if (intent.next_scene != SceneType::NO_CHANGE && intent != scene_stack.back()) {
		if (scene_stack.size() >= 2 && intent == scene_stack[scene_stack.size() - 2]) intent.next_scene = SceneType::BACK;
		if (intent.next_scene == SceneType::BACK) {
			if (scene_stack.size() >= 2) scene_stack.pop_back();
		} else scene_stack.push_back(intent);
		
		current_scene = scene_stack.back().next_scene;
		std::string arg = scene_stack.back().arg;
		
		if (current_scene == SceneType::VIDEO_PLAYER) VideoPlayer_resume(arg);
		else if (current_scene == SceneType::SEARCH) Search_resume(arg);
		else if (current_scene == SceneType::CHANNEL) Channel_resume(arg);
		else if (current_scene == SceneType::SETTINGS) Sem_resume(arg);
		else if (current_scene == SceneType::ABOUT) About_resume(arg);
		else if (current_scene == SceneType::HISTORY) History_resume(arg);
		else if (current_scene == SceneType::SUBSCRIPTION) Subscription_resume(arg);
		// add here
	}
	
	return true;
}

void Menu_get_system_info(void)
{
	u8 battery_level = -1;
	u8 battery_voltage = -1;
	char* ssid = (char*)malloc(512);
	Result_with_string result;

	PTMU_GetBatteryChargeState(&var_battery_charge);//battery charge
	result.code = MCUHWC_GetBatteryLevel(&battery_level);//battery level(%)
	if(result.code == 0)
	{
		MCUHWC_GetBatteryVoltage(&battery_voltage);
		var_battery_voltage = 5.0 * (battery_voltage / 256); 
		var_battery_level_raw = battery_level;
	}
	else
	{
		PTMU_GetBatteryLevel(&battery_level);
		if ((int)battery_level == 0)
			var_battery_level_raw = 0;
		else if ((int)battery_level == 1)
			var_battery_level_raw = 5;
		else if ((int)battery_level == 2)
			var_battery_level_raw = 10;
		else if ((int)battery_level == 3)
			var_battery_level_raw = 30;
		else if ((int)battery_level == 4)
			var_battery_level_raw = 60;
		else if ((int)battery_level == 5)
			var_battery_level_raw = 100;
	}

	//ssid
	result.code = ACU_GetSSID(ssid);
	if(result.code == 0)
		var_connected_ssid = ssid;
	else
		var_connected_ssid = "";

	free(ssid);
	ssid = NULL;

	var_wifi_signal = osGetWifiStrength();
	//Get wifi state from shared memory #0x1FF81067
	var_wifi_state = *(u8 *) 0x1FF81067;
	if(var_wifi_state == 2)
	{
		if (!var_connect_test_succes)
			var_wifi_signal += 4;
	}
	else
	{
		var_wifi_signal = 8;
		var_connect_test_succes = false;
	}

	//Get time
	time_t unixTime = time(NULL);
	struct tm* timeStruct = gmtime((const time_t*)&unixTime);
	var_years = timeStruct->tm_year + 1900;
	var_months = timeStruct->tm_mon + 1;
	var_days = timeStruct->tm_mday;
	var_hours = timeStruct->tm_hour;
	var_minutes = timeStruct->tm_min;
	var_seconds = timeStruct->tm_sec;

	if (var_debug_mode)
	{
		//check free RAM
		var_free_ram = Menu_check_free_ram();
		var_free_linear_ram = linearSpaceFree();
	}
}

int Menu_check_free_ram(void)
{
	u8* malloc_check[200];
	int count;

	for (int i = 0; i < 200; i++)
		malloc_check[i] = NULL;

	for (count = 0; count < 200; count++)
	{
		malloc_check[count] = (u8*)malloc(1000000);// 1 MB
		if (malloc_check[count] == NULL)
			break;
	}

	for (int i = 0; i <= count; i++)
		free(malloc_check[i]);

	return count * 1000; // return free KB
}

/*
void Menu_send_app_info_thread(void* arg)
{
	Util_log_save(DEF_MENU_SEND_APP_INFO_STR, "Thread started.");
	OS_VersionBin os_ver;
	bool is_new3ds = false;
	u8* dl_data = NULL;
	u32 status_code = 0;
	u32 downloaded_size = 0;
	char system_ver_char[0x50] = " ";
	std::string new3ds;
	dl_data = (u8*)malloc(0x10000);

	osGetSystemVersionDataString(&os_ver, &os_ver, system_ver_char, 0x50);
	std::string system_ver = system_ver_char;

	APT_CheckNew3DS(&is_new3ds);

	if (is_new3ds)
		new3ds = "yes";
	else
		new3ds = "no";

	std::string send_data = "{ \"app_ver\": \"" + DEF_CURRENT_APP_VER + "\",\"system_ver\" : \"" + system_ver + "\",\"start_num_of_app\" : \"" + std::to_string(var_num_of_app_start) + "\",\"language\" : \"" + var_lang + "\",\"new3ds\" : \"" + new3ds + "\",\"time_to_enter_sleep\" : \"" + std::to_string(var_time_to_turn_off_lcd) + "\",\"scroll_speed\" : \"" + std::to_string(var_scroll_speed) + "\" }";
	Util_httpc_post_and_dl_data(DEF_SEND_APP_INFO_URL, (char*)send_data.c_str(), send_data.length(), dl_data, 0x10000, &downloaded_size, &status_code, true, 5);
	free(dl_data);
	dl_data = NULL;

	Util_log_save(DEF_MENU_SEND_APP_INFO_STR, "Thread exit.");
	threadExit(0);
}*/

void Menu_check_connectivity_thread(void* arg)
{
	Util_log_save(DEF_MENU_CHECK_INTERNET_STR, "Thread started.");
	u8* http_buffer = NULL;
	u32 status_code = 0;
	u32 dl_size = 0;
	int count = 100;
	std::string last_url;
	http_buffer = (u8*)malloc(0x1000);

	while (menu_thread_run)
	{
		if (count >= 100)
		{
			count = 0;
			Util_httpc_dl_data(DEF_CHECK_INTERNET_URL, http_buffer, 0x1000, &dl_size, &status_code, false, 0);

			if (status_code == 204)
				var_connect_test_succes = true;
			else
				var_connect_test_succes = false;
		}
		else
			usleep(DEF_ACTIVE_THREAD_SLEEP_TIME);

		count++;
	}
	Util_log_save(DEF_MENU_CHECK_INTERNET_STR, "Thread exit.");
	threadExit(0);
}

void Menu_worker_thread(void* arg)
{
	Util_log_save(DEF_MENU_WORKER_THREAD_STR, "Thread started.");
	int count = 0;
	Result_with_string result;

	int prev_state = 2; // 0 : turned off, 1 : set to 10, 2 : full
	while (menu_thread_run)
	{
		usleep(49000);
		count++;

		if (count >= 20)
		{
			Menu_get_system_info();
			var_need_reflesh = true;
			count = 0;
		}
		
		var_afk_time += 0.05;
		
		int cur_state;
		if(var_afk_time > var_time_to_turn_off_lcd) cur_state = 0;
		else if(var_afk_time > std::max<float>(var_time_to_turn_off_lcd * 0.5, (var_time_to_turn_off_lcd - 10))) cur_state = 1;
		else cur_state = 2;

		if (cur_state != prev_state) {
			if (prev_state == 0) Util_cset_set_screen_state(true, true, true);
			if (cur_state == 0) Util_cset_set_screen_state(true, true, false);
			if (cur_state == 1) {
				result = Util_cset_set_screen_brightness(true, true, 10);
				if(result.code != 0)
					Util_log_save(DEF_MENU_WORKER_THREAD_STR, "Util_cset_set_screen_brightness()..." + result.string + result.error_description, result.code);
			} else if (cur_state == 2) {
				result = Util_cset_set_screen_brightness(true, true, var_lcd_brightness);
				if(result.code != 0)
					Util_log_save(DEF_MENU_WORKER_THREAD_STR, "Util_cset_set_screen_brightness()..." + result.string + result.error_description, result.code);
			}
			prev_state = cur_state;
		}

		if (var_flash_mode)
		{
			var_night_mode = !var_night_mode;
			var_need_reflesh = true;
		}
	}
	Util_log_save(DEF_MENU_WORKER_THREAD_STR, "Thread exit.");
	threadExit(0);
}

void Menu_update_thread(void* arg)
{
	Util_log_save(DEF_MENU_UPDATE_THREAD_STR, "Thread started.");
	u8* http_buffer = NULL;
	u32 status_code = 0;
	u32 dl_size = 0;
	size_t pos[2] = { 0, 0, };
	std::string data = "";
	Result_with_string result;
	http_buffer = (u8*)malloc(0x1000);

	result = Util_httpc_dl_data(DEF_CHECK_UPDATE_URL, http_buffer, 0x1000, &dl_size, &status_code, true, 3);
	Util_log_save(DEF_MENU_UPDATE_THREAD_STR, "Util_httpc_dl_data()..." + result.string + result.error_description, result.code);
	if(result.code == 0)
	{
		data = (char*)http_buffer;
		pos[0] = data.find("<newest>");
		pos[1] = data.find("</newest>");
		if(pos[0] != std::string::npos && pos[1] != std::string::npos)
		{
			data = data.substr(pos[0] + 8, pos[1] - (pos[0] + 8));
			if(DEF_CURRENT_APP_VER_INT < atoi(data.c_str()))
				menu_update_available = true;
		}
	}

	Util_log_save(DEF_MENU_UPDATE_THREAD_STR, "Thread exit.");
	threadExit(0);
}