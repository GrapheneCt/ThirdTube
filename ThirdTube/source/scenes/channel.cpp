#include "headers.hpp"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <numeric>

#include "scenes/channel.hpp"
#include "scenes/video_player.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/ui_common.hpp"
#include "ui/scroller.hpp"
#include "ui/overlay.hpp"
#include "ui/colors.hpp"
#include "network/thumbnail_loader.hpp"
#include "system/util/async_task.hpp"
#include "system/util/misc_tasks.hpp"
#include "system/util/subscription_util.hpp"

#define THUMBNAIL_HEIGHT 54
#define THUMBNAIL_WIDTH 96
#define VIDEOS_MARGIN 6
#define VIDEOS_VERTICAL_INTERVAL (THUMBNAIL_HEIGHT + VIDEOS_MARGIN)
#define LOAD_MORE_MARGIN 30
#define BANNER_HEIGHT 55
#define ICON_SIZE 55
#define TAB_SELECTOR_HEIGHT 20
#define TAB_SELECTOR_SELECTED_LINE_HEIGHT 3
#define SUBSCRIBE_BUTTON_WIDTH 90
#define SUBSCRIBE_BUTTON_HEIGHT 25

#define MAX_THUMBNAIL_LOAD_REQUEST 30

#define TAB_NUM 2

namespace Channel {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	int VIDEO_LIST_Y_HIGH = 240;
	VerticalScroller videos_scroller = VerticalScroller(0, 320, 0, VIDEO_LIST_Y_HIGH);
	
	int selected_tab = 0;
	
	Handle resource_lock;
	std::string cur_channel_url;
	YouTubeChannelDetail channel_info;
	std::map<std::string, YouTubeChannelDetail> channel_info_cache;
	int thumbnail_request_l = 0;
	int thumbnail_request_r = 0;
	std::vector<int> thumbnail_handles;
	int banner_thumbnail_handle = -1;
	int icon_thumbnail_handle = -1;
	std::vector<std::vector<std::string> > wrapped_titles;
};
using namespace Channel;

static void reset_channel_info() {
	for (int i = thumbnail_request_l; i < thumbnail_request_r; i++) thumbnail_cancel_request(thumbnail_handles[i]);
	thumbnail_handles.clear();
	thumbnail_request_l = thumbnail_request_r = 0;
	if (icon_thumbnail_handle != -1) thumbnail_cancel_request(icon_thumbnail_handle), icon_thumbnail_handle = -1;
	if (banner_thumbnail_handle != -1) thumbnail_cancel_request(banner_thumbnail_handle), banner_thumbnail_handle = -1;
	channel_info = YouTubeChannelDetail();
}

void load_channel(void *) {
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	auto url = cur_channel_url;
	YouTubeChannelDetail result;
	bool need_loading = false;
	if (channel_info_cache.count(url)) result = channel_info_cache[url];
	else need_loading = true;
	svcReleaseMutex(resource_lock);
	
	if (need_loading) {
		add_cpu_limit(25);
		result = youtube_parse_channel_page(url);
		remove_cpu_limit(25);
	}
	
	// wrap and truncate here
	Util_log_save("channel", "truncate start");
	std::vector<std::vector<std::string> > new_wrapped_titles(result.videos.size());
	for (size_t i = 0; i < result.videos.size(); i++)
		new_wrapped_titles[i] = truncate_str(result.videos[i].title, 320 - (THUMBNAIL_WIDTH + 3), 2, 0.5, 0.5);
	Util_log_save("channel", "truncate end");
	
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	channel_info = result;
	wrapped_titles = new_wrapped_titles;
	channel_info_cache[url] = result;
	
	thumbnail_handles.assign(channel_info.videos.size(), -1);
	if (channel_info.icon_url != "") icon_thumbnail_handle = thumbnail_request(channel_info.icon_url, SceneType::CHANNEL, 1001, ThumbnailType::ICON);
	if (channel_info.banner_url != "") banner_thumbnail_handle = thumbnail_request(channel_info.banner_url, SceneType::CHANNEL, 1000, ThumbnailType::VIDEO_BANNER);
	var_need_reflesh = true;
	svcReleaseMutex(resource_lock);
}
void load_channel_more(void *) {
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	auto prev_result = channel_info;
	svcReleaseMutex(resource_lock);
	
	auto new_result = youtube_channel_page_continue(prev_result);
	
	Util_log_save("channel-c", "truncate start");
	std::vector<std::vector<std::string> > wrapped_titles_add(new_result.videos.size() - prev_result.videos.size());
	for (size_t i = prev_result.videos.size(); i < new_result.videos.size(); i++)
		wrapped_titles_add[i - prev_result.videos.size()] = truncate_str(new_result.videos[i].title, 320 - (THUMBNAIL_WIDTH + 3), 2, 0.5, 0.5);
	Util_log_save("channel-c", "truncate end");
	
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (new_result.error != "") channel_info.error = new_result.error;
	else {
		channel_info = new_result;
		channel_info_cache[channel_info.url_original] = channel_info;
		wrapped_titles.insert(wrapped_titles.end(), wrapped_titles_add.begin(), wrapped_titles_add.end());
	}
	thumbnail_handles.resize(channel_info.videos.size(), -1);
	var_need_reflesh = true;
	svcReleaseMutex(resource_lock);
}
static bool send_load_request(std::string url) {
	if (!is_async_task_running(load_channel)) {
		remove_all_async_tasks_with_type(load_channel_more);
		
		svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
		cur_channel_url = url;
		reset_channel_info();
		svcReleaseMutex(resource_lock);
		
		queue_async_task(load_channel, NULL);
		return true;
	} else return false;
}
static bool send_load_more_request() {
	bool res = false;
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	if (channel_info.videos.size() && channel_info.has_continue()) {
		queue_async_task(load_channel_more, NULL);
		res = true;
	}
	svcReleaseMutex(resource_lock);
	return res;
}

bool Channel_query_init_flag(void) {
	return already_init;
}


void Channel_resume(std::string arg)
{
	if (arg != "" && arg != cur_channel_url) send_load_request(arg);
	overlay_menu_on_resume();
	videos_scroller.on_resume();
	thread_suspend = false;
	var_need_reflesh = true;
}

void Channel_suspend(void)
{
	thread_suspend = true;
}

void Channel_init(void)
{
	Util_log_save("channel/init", "Initializing...");
	Result_with_string result;
	
	reset_channel_info();
	svcCreateMutex(&resource_lock, false);
	
	Channel_resume("");
	already_init = true;
}

void Channel_exit(void)
{
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	Util_log_save("search/exit", "Exited.");
}


struct TemporaryCopyOfChannelInfo {
	std::string id;
	std::string url;
	std::string error;
	std::string name;
	std::string banner_url;
	std::string icon_url;
	std::string description;
	std::string subscriber_count_str;
	int video_num;
	int displayed_l;
	int displayed_r;
	std::map<int, YouTubeVideoSuccinct> videos;
	std::map<int, std::vector<std::string> > wrapped_titles;
	bool has_continue;
};

#define DURATION_FONT_SIZE 0.4
static void draw_channel_content(TemporaryCopyOfChannelInfo &channel_info, Hid_info key) {
	if (is_async_task_running(load_channel)) {
		Draw_x_centered(LOCALIZED(LOADING), 0, 320, 0, 0.5, 0.5, DEFAULT_TEXT_COLOR);
	} else  {
		int y_offset = -videos_scroller.get_offset();
		if (channel_info.banner_url != "") {
			thumbnail_draw(banner_thumbnail_handle, 0, y_offset, 320, BANNER_HEIGHT); // banner
			y_offset += BANNER_HEIGHT;
		}
		y_offset += SMALL_MARGIN;
		if (channel_info.icon_url != "") {
			thumbnail_draw(icon_thumbnail_handle, SMALL_MARGIN, y_offset, ICON_SIZE, ICON_SIZE); // icon
		}
		Draw(channel_info.name, ICON_SIZE + SMALL_MARGIN * 3, y_offset - 3, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, DEFAULT_TEXT_COLOR); // channel name
		bool is_subscribed = subscription_is_subscribed(channel_info.id);
		u32 subscribe_button_color = is_subscribed ? LIGHT1_BACK_COLOR : 0xFF4040EE;
		std::string subscribe_button_str = is_subscribed ? LOCALIZED(SUBSCRIBED) : LOCALIZED(SUBSCRIBE);
		float button_y = y_offset + ICON_SIZE - SUBSCRIBE_BUTTON_HEIGHT - SMALL_MARGIN;
		Draw_texture(var_square_image[0], subscribe_button_color, 320 - SMALL_MARGIN * 2 - SUBSCRIBE_BUTTON_WIDTH, button_y, SUBSCRIBE_BUTTON_WIDTH, SUBSCRIBE_BUTTON_HEIGHT);
		Draw_x_centered(subscribe_button_str, 320 - SMALL_MARGIN * 2 - SUBSCRIBE_BUTTON_WIDTH, 320 - SMALL_MARGIN * 2, button_y + 4, 0.5, 0.5, 0xFF000000);
		y_offset += ICON_SIZE;
		y_offset += SMALL_MARGIN;
		
		// tab selector
		Draw_texture(var_square_image[0], LIGHT1_BACK_COLOR, 0, y_offset, 320, TAB_SELECTOR_HEIGHT);
		Draw_texture(var_square_image[0], LIGHT2_BACK_COLOR, selected_tab * 320 / TAB_NUM, y_offset, 320 / TAB_NUM + 1, TAB_SELECTOR_HEIGHT);
		Draw_texture(var_square_image[0], LIGHT3_BACK_COLOR, selected_tab * 320 / TAB_NUM, y_offset + TAB_SELECTOR_HEIGHT - TAB_SELECTOR_SELECTED_LINE_HEIGHT,
			320 / TAB_NUM + 1, TAB_SELECTOR_SELECTED_LINE_HEIGHT);
		for (int i = 0; i < TAB_NUM; i++) {
			float font_size = 0.4;
			float x_l = i * 320 / TAB_NUM;
			float x_r = (i + 1) * 320 / TAB_NUM;
			std::string tab_string;
			if (i == 0) tab_string = LOCALIZED(VIDEOS);
			else if (i == 1) tab_string = LOCALIZED(INFO);
			float y = y_offset + 3;
			if (i == selected_tab) y -= 1;
			Draw_x_centered(tab_string, x_l, x_r, y, font_size, font_size, DEFAULT_TEXT_COLOR);
		}
		y_offset += TAB_SELECTOR_HEIGHT;
		
		if (selected_tab == 0) { // videos
			if (channel_info.video_num) {
				for (int i = channel_info.displayed_l; i < channel_info.displayed_r; i++) {
					int y_l = y_offset + i * VIDEOS_VERTICAL_INTERVAL;
					int y_r = y_l + THUMBNAIL_HEIGHT;
					
					if (key.touch_y != -1 && key.touch_y >= y_l && key.touch_y < y_r) {
						u8 darkness = std::min<int>(0xFF, 0xD0 + (1 - videos_scroller.selected_overlap_darkness()) * 0x30);
						if (var_night_mode) darkness = 0xFF - darkness;
						u32 color = 0xFF000000 | darkness << 16 | darkness << 8 | darkness;
						Draw_texture(var_square_image[0], color, 0, y_l, 320, y_r - y_l);
					}
					
					auto cur_video = channel_info.videos[i];
					int cur_y = y_l;
					// thumbnail
					thumbnail_draw(thumbnail_handles[i], 0, cur_y, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
					// duration text
					float duration_width = Draw_get_width(cur_video.duration_text, DURATION_FONT_SIZE, DURATION_FONT_SIZE);
					Draw_texture(var_square_image[0], 0xBB000000, THUMBNAIL_WIDTH - duration_width - 2, cur_y + THUMBNAIL_HEIGHT - 10, duration_width + 2, 10);
					Draw(cur_video.duration_text, THUMBNAIL_WIDTH - duration_width - 1, cur_y + THUMBNAIL_HEIGHT - 11, DURATION_FONT_SIZE, DURATION_FONT_SIZE, (u32) -1);
					// title
					auto title_lines = channel_info.wrapped_titles[i];
					for (size_t line = 0; line < title_lines.size(); line++) {
						Draw(title_lines[line], THUMBNAIL_WIDTH + 3, cur_y, 0.5, 0.5, DEFAULT_TEXT_COLOR);
						cur_y += 13;
					}
					cur_y += 2;
					Draw(cur_video.publish_date, THUMBNAIL_WIDTH + 3, cur_y, 0.45, 0.45, DEF_DRAW_GRAY);
					cur_y += 13;
					Draw(cur_video.views_str, THUMBNAIL_WIDTH + 3, cur_y, 0.45, 0.45, DEF_DRAW_GRAY);
				}
				y_offset += channel_info.video_num * VIDEOS_VERTICAL_INTERVAL;
				if (channel_info.error != "" || channel_info.has_continue) {
					std::string draw_string = "";
					if (is_async_task_running(load_channel_more)) draw_string = "Loading...";
					else if (channel_info.error != "") draw_string = channel_info.error;
					
					if (y_offset < VIDEO_LIST_Y_HIGH) Draw_x_centered(draw_string, 0, 320, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
					y_offset += LOAD_MORE_MARGIN;
				}
			} else Draw_x_centered(LOCALIZED(NO_VIDEOS), 0, 320, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
		} else if (selected_tab == 1) { // channel description
			Draw(LOCALIZED(CHANNEL_DESCRIPTION), 3, y_offset, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE, DEFAULT_TEXT_COLOR);
			y_offset += Draw_get_height(LOCALIZED(CHANNEL_DESCRIPTION), MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
			y_offset += SMALL_MARGIN; // without this, the following description somehow overlaps with the above text
			Draw(channel_info.description, 3, y_offset, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			y_offset += Draw_get_height(channel_info.description, 0.5, 0.5);
			y_offset += SMALL_MARGIN;
			Draw_line(SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 320 - 1 - SMALL_MARGIN, y_offset, DEF_DRAW_GRAY, 1);
			y_offset += SMALL_MARGIN;
		}
	}
	videos_scroller.draw_slider_bar();
}


Intent Channel_draw(void)
{
	Intent intent;
	intent.next_scene = SceneType::NO_CHANGE;
	Hid_info key;
	Util_hid_query_key_state(&key);
	Util_hid_key_flag_reset();
	
	thumbnail_set_active_scene(SceneType::CHANNEL);
	
	bool video_playing_bar_show = video_is_playing();
	VIDEO_LIST_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	videos_scroller.change_area(0, 320, 0, VIDEO_LIST_Y_HIGH);
	
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
	int video_num = channel_info.videos.size();
	int displayed_l, displayed_r;
	{
		
		int y_offset = 0;
		if (channel_info.banner_url != "") y_offset += BANNER_HEIGHT;
		y_offset += ICON_SIZE + SMALL_MARGIN * 2 + TAB_SELECTOR_HEIGHT;
		displayed_l = std::min(video_num, std::max(0, (videos_scroller.get_offset() - y_offset) / VIDEOS_VERTICAL_INTERVAL));
		displayed_r = std::min(video_num, std::max(0, (videos_scroller.get_offset() - y_offset + VIDEO_LIST_Y_HIGH - 1) / VIDEOS_VERTICAL_INTERVAL + 1));
	}
	// back up some information while `resource_lock` is locked
	TemporaryCopyOfChannelInfo channel_info_bak;
	channel_info_bak.id = channel_info.id;
	channel_info_bak.url = channel_info.url;
	channel_info_bak.error = channel_info.error;
	channel_info_bak.name = channel_info.name;
	channel_info_bak.icon_url = channel_info.icon_url;
	channel_info_bak.banner_url = channel_info.banner_url;
	channel_info_bak.description = channel_info.description;
	channel_info_bak.has_continue = channel_info.has_continue();
	channel_info_bak.video_num = video_num;
	channel_info_bak.subscriber_count_str = channel_info.subscriber_count_str;
	channel_info_bak.displayed_l = displayed_l;
	channel_info_bak.displayed_r = displayed_r;
	for (int i = displayed_l; i < displayed_r; i++) {
		channel_info_bak.videos[i] = channel_info.videos[i];
		channel_info_bak.wrapped_titles[i] = wrapped_titles[i];
	}
	
	// thumbnail request update (this should be done while `resource_lock` is locked)
	if (channel_info.videos.size()) {
		
		int request_target_l = std::max(0, displayed_l - (MAX_THUMBNAIL_LOAD_REQUEST - (displayed_r - displayed_l)) / 2);
		int request_target_r = std::min(video_num, request_target_l + MAX_THUMBNAIL_LOAD_REQUEST);
		// transition from [thumbnail_request_l, thumbnail_request_r) to [request_target_l, request_target_r)
		std::set<int> new_indexes, cancelling_indexes;
		for (int i = thumbnail_request_l; i < thumbnail_request_r; i++) cancelling_indexes.insert(i);
		for (int i = request_target_l; i < request_target_r; i++) new_indexes.insert(i);
		for (int i = thumbnail_request_l; i < thumbnail_request_r; i++) new_indexes.erase(i);
		for (int i = request_target_l; i < request_target_r; i++) cancelling_indexes.erase(i);
		
		for (auto i : cancelling_indexes) thumbnail_cancel_request(thumbnail_handles[i]), thumbnail_handles[i] = -1;
		for (auto i : new_indexes) thumbnail_handles[i] = thumbnail_request(channel_info.videos[i].thumbnail_url, SceneType::CHANNEL, 0, ThumbnailType::VIDEO_THUMBNAIL);
		
		thumbnail_request_l = request_target_l;
		thumbnail_request_r = request_target_r;
		
		std::vector<std::pair<int, int> > priority_list(request_target_r - request_target_l);
		auto dist = [&] (int i) { return i < displayed_l ? displayed_l - i : i - displayed_r + 1; };
		for (int i = request_target_l; i < request_target_r; i++) priority_list[i - request_target_l] = {thumbnail_handles[i], 500 - dist(i)};
		thumbnail_set_priorities(priority_list);
	}
	svcReleaseMutex(resource_lock);
	
	if(var_need_reflesh || !var_eco_mode)
	{
		var_need_reflesh = false;
		Draw_frame_ready();
		Draw_screen_ready(0, DEFAULT_BACK_COLOR);

		if(Util_log_query_log_show_flag())
			Util_log_draw();

		Draw_top_ui();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		draw_channel_content(channel_info_bak, key);
		
		if (video_playing_bar_show) video_draw_playing_bar();
		draw_overlay_menu(video_playing_bar_show ? 240 - OVERLAY_MENU_ICON_SIZE - VIDEO_PLAYING_BAR_HEIGHT : 240 - OVERLAY_MENU_ICON_SIZE);
		
		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

		Draw_touch_pos();

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();
	

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if(Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key, &intent, SceneType::CHANNEL);
		
		int content_height = 0;
		if (channel_info_bak.banner_url != "") content_height += BANNER_HEIGHT;
		content_height += ICON_SIZE + SMALL_MARGIN * 2 + TAB_SELECTOR_HEIGHT;
		if (selected_tab == 0) {
			content_height += channel_info_bak.video_num * VIDEOS_VERTICAL_INTERVAL;
			// load more
			if (content_height - videos_scroller.get_offset() < VIDEO_LIST_Y_HIGH && !is_async_task_running(load_channel_more) &&
				channel_info_bak.video_num) {
				send_load_more_request();
			}
			if (channel_info_bak.error != "" || channel_info_bak.has_continue) content_height += LOAD_MORE_MARGIN;
		} else if (selected_tab == 1) {
			content_height += Draw_get_height(LOCALIZED(CHANNEL_DESCRIPTION), MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
			content_height += SMALL_MARGIN;
			content_height += Draw_get_height(channel_info_bak.description, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
			content_height += SMALL_MARGIN + SMALL_MARGIN;
		}
		auto released_point = videos_scroller.update(key, content_height);
		
		if (video_playing_bar_show) video_update_playing_bar(key, &intent);
		// handle touches
		if (released_point.second != -1) do {
			int released_x = released_point.first;
			int released_y = released_point.second;
			int y_offset = 0;
			if (channel_info_bak.banner_url != "") y_offset += BANNER_HEIGHT;
			y_offset += SMALL_MARGIN;
			
			float button_y = y_offset + ICON_SIZE - SUBSCRIBE_BUTTON_HEIGHT - SMALL_MARGIN;
			if (button_y <= released_y && released_y < button_y + SUBSCRIBE_BUTTON_HEIGHT &&
				released_x >= 320 - SMALL_MARGIN * 2 - SUBSCRIBE_BUTTON_WIDTH && released_x < 320 - SMALL_MARGIN * 2) {
				bool cur_subscribed = subscription_is_subscribed(channel_info_bak.id);
				if (cur_subscribed) subscription_unsubscribe(channel_info_bak.id);
				else {
					SubscriptionChannel new_channel;
					new_channel.id = channel_info_bak.id;
					new_channel.url = channel_info_bak.url;
					new_channel.name = channel_info_bak.name;
					new_channel.icon_url = channel_info_bak.icon_url;
					new_channel.subscriber_count_str = channel_info_bak.subscriber_count_str;
					subscription_subscribe(new_channel);
				}
				misc_tasks_request(TASK_SAVE_SUBSCRIPTION);
				var_need_reflesh = true;
			}
			
			y_offset += ICON_SIZE + SMALL_MARGIN;
			
			if (y_offset <= released_y && released_y < y_offset + TAB_SELECTOR_HEIGHT) {
				int next_tab_index = released_x * TAB_NUM / 320;
				selected_tab = next_tab_index;
				var_need_reflesh = true;
				break;
			}
			y_offset += TAB_SELECTOR_HEIGHT;
			if (selected_tab == 0) {
				if (y_offset <= released_y && released_y < y_offset + (int) channel_info_bak.video_num * VIDEOS_VERTICAL_INTERVAL) {
					int index = (released_y - y_offset) / VIDEOS_VERTICAL_INTERVAL;
					int remainder = (released_y - y_offset) % VIDEOS_VERTICAL_INTERVAL;
					if (remainder < THUMBNAIL_HEIGHT) {
						if (displayed_l <= index && index < displayed_r) {
							intent.next_scene = SceneType::VIDEO_PLAYER;
							intent.arg = channel_info_bak.videos[index].url;
							break;
						} else Util_log_save("channel", "unexpected : a video that is not displayed is selected");
					}
				}
				y_offset += channel_info_bak.video_num * VIDEOS_VERTICAL_INTERVAL;
				if (channel_info_bak.error != "" || channel_info_bak.has_continue) y_offset += LOAD_MORE_MARGIN;
			} else {
				y_offset += Draw_get_height(LOCALIZED(CHANNEL_DESCRIPTION), MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
				y_offset += SMALL_MARGIN;
				y_offset += Draw_get_height(channel_info_bak.description, MIDDLE_FONT_SIZE, MIDDLE_FONT_SIZE);
				y_offset += SMALL_MARGIN + SMALL_MARGIN;
				// nothing to do on the description
			}
		} while (0);
		
		static int consecutive_scroll = 0;
		if (key.h_c_up || key.h_c_down) {
			if (key.h_c_up) consecutive_scroll = std::max(0, consecutive_scroll) + 1;
			else consecutive_scroll = std::min(0, consecutive_scroll) - 1;
			
			float scroll_amount = DPAD_SCROLL_SPEED0;
			if (std::abs(consecutive_scroll) > DPAD_SCROLL_SPEED1_THRESHOLD) scroll_amount = DPAD_SCROLL_SPEED1;
			if (key.h_c_up) scroll_amount *= -1;
			
			videos_scroller.scroll(scroll_amount);
			var_need_reflesh = true;
		} else consecutive_scroll = 0;
		
		if (key.p_b) intent.next_scene = SceneType::BACK;
		
		if(key.h_touch || key.p_touch)
			var_need_reflesh = true;
		if (key.p_select) Util_log_set_log_show_flag(!Util_log_query_log_show_flag());
	}

	if(Util_log_query_log_show_flag())
		Util_log_main(key);
	
	return intent;
}
