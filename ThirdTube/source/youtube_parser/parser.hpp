#pragma once
#include <vector>
#include <string>
#include <map>

#ifdef TT_PRX
#define PRX_EXPORT __declspec(dllexport)
#else
#define PRX_EXPORT
#endif

PRX_EXPORT void youtube_change_content_language(std::string language_code);

// util function
PRX_EXPORT std::string youtube_get_video_thumbnail_url_by_id(const std::string &id);
PRX_EXPORT std::string youtube_get_video_thumbnail_hq_url_by_id(const std::string &id);
PRX_EXPORT std::string youtube_get_video_url_by_id(const std::string &id);
PRX_EXPORT std::string get_video_id_from_thumbnail_url(const std::string &url);
PRX_EXPORT bool youtube_is_valid_video_id(const std::string &id);
PRX_EXPORT bool is_youtube_url(const std::string &url);
PRX_EXPORT bool is_youtube_thumbnail_url(const std::string &url);

enum class YouTubePageType {
	VIDEO,
	CHANNEL,
	SEARCH,
	INVALID
};
PRX_EXPORT YouTubePageType youtube_get_page_type(std::string url);

#ifdef TT_PORTABLE
struct YouTubeChannelSuccinct {
	std::string name;
	std::string url;
	std::string icon_url;
	std::string subscribers;
	std::string video_num;
};
struct YouTubeVideoSuccinct {
	std::string url;
	std::string title;
	std::string duration_text;
	std::string publish_date;
	std::string views_str;
	std::string author;
	std::string thumbnail_url;
};
struct YouTubePlaylistSuccinct {
	std::string url;
	std::string title;
	std::string video_count_str;
	std::string thumbnail_url;
};

struct YouTubeSuccinctItem {
	// TODO : use union or std::variant
	enum {
		VIDEO,
		CHANNEL,
		PLAYLIST
	} type;
	YouTubeVideoSuccinct video;
	YouTubeChannelSuccinct channel;
	YouTubePlaylistSuccinct playlist;

	YouTubeSuccinctItem() = default;
	YouTubeSuccinctItem(YouTubeVideoSuccinct video) : type(VIDEO), video(video) {}
	YouTubeSuccinctItem(YouTubeChannelSuccinct channel) : type(CHANNEL), channel(channel) {}
	YouTubeSuccinctItem(YouTubePlaylistSuccinct playlist) : type(PLAYLIST), playlist(playlist) {}

	std::string get_url() const { return type == VIDEO ? video.url : type == CHANNEL ? channel.url : playlist.url; }
	std::string get_thumbnail_url() const { return type == VIDEO ? video.thumbnail_url : type == CHANNEL ? channel.icon_url : playlist.thumbnail_url; }
	std::string get_name() const { return type == VIDEO ? video.title : type == CHANNEL ? channel.name : playlist.title; }
};


struct YouTubeSearchResult {
	std::string error;
	int estimated_result_num;
	std::vector<YouTubeSuccinctItem> results;

	std::string continue_token;
	std::string continue_key;

	bool has_continue() const { return continue_token != "" && continue_key != ""; }
};
PRX_EXPORT YouTubeSearchResult *youtube_parse_search(std::string url);
PRX_EXPORT YouTubeSearchResult *youtube_parse_search(char *url);
// takes the previous result, returns the new result with both old items and new items
PRX_EXPORT YouTubeSearchResult *youtube_continue_search(const YouTubeSearchResult &prev_result);
PRX_EXPORT YouTubeSearchResult *youtube_parse_search_word(char *search_word);


struct YouTubeVideoDetail {
	std::string error;
	std::string url;
	std::string title;
	std::string description;
	YouTubeChannelSuccinct author;
	std::string audio_stream_url;
	std::map<int, std::string> video_stream_urls; // first : video size (144p, 240p, 360p ...)
	std::string both_stream_url;
	int duration_ms;
	bool is_livestream;
	enum class LivestreamType {
		PREMIERE,
		LIVESTREAM,
	};
	LivestreamType livestream_type;
	bool is_upcoming;
	std::string playability_status;
	std::string playability_reason;
	int stream_fragment_len; // used only for livestreams
	std::string like_count_str;
	std::string dislike_count_str;
	std::string publish_date;
	std::string views_str;

	// caption-related data
	std::string caption_base_url;
	struct CaptionBaseLanguage {
		std::string name; // e.g. "English", "Japanese"
		std::string id; // e.g. "en", "ja"
		std::string base_url; // empty string for instances in caption_translated_languages
		bool is_translatable;
	};
	struct CaptionTranslationLanguage {
		std::string name;
		std::string id;
	};
	std::vector<CaptionBaseLanguage> caption_base_languages;
	std::vector<CaptionTranslationLanguage> caption_translation_languages;
	struct CaptionPiece {
		float start_time;
		float end_time;
		std::string content;
	};
	using Caption = std::vector<CaptionPiece>;
	std::map<std::pair<std::string, std::string>, Caption> caption_data;

	std::vector<YouTubeSuccinctItem> suggestions;
	struct Playlist {
		std::string id;
		std::string title;
		std::string author_name;
		int total_videos;
		std::vector<YouTubeVideoSuccinct> videos;
		int selected_index;
	};
	Playlist playlist;

	struct Comment {
		YouTubeChannelSuccinct author;
		std::string content;
		std::string id;

		int reply_num;
		std::vector<Comment> replies;
		std::string continue_key; // innertube key, equals to YouTubeChannelDetail.continue_key
		std::string replies_continue_token;
		bool has_more_replies() const { return replies_continue_token != ""; }
	};
	std::vector<Comment> comments;

	std::string continue_key; // innertube key
	std::string suggestions_continue_token;
	std::string comment_continue_token;
	int comment_continue_type; // -1 : unavailable, 0 : using watch_comments, 1 : using innertube
	bool comments_disabled;

	bool has_more_suggestions() const { return continue_key != "" && suggestions_continue_token != ""; }
	bool has_more_comments() const { return comment_continue_type != -1; }
	bool needs_timestamp_adjusting() const { return is_livestream && livestream_type == LivestreamType::PREMIERE; }
	bool is_playable() const { return playability_status == "OK" && (both_stream_url != "" || (audio_stream_url != "" && video_stream_urls.size())); }
};
// this function does not load comments; call youtube_video_page_load_more_comments() if necessary
PRX_EXPORT YouTubeVideoDetail *youtube_parse_video_page(std::string url);
PRX_EXPORT YouTubeVideoDetail *youtube_parse_video_page(char *url);
PRX_EXPORT YouTubeVideoDetail *youtube_video_page_load_more_suggestions(const YouTubeVideoDetail &prev_result);
PRX_EXPORT YouTubeVideoDetail *youtube_video_page_load_more_comments(const YouTubeVideoDetail &prev_result);
PRX_EXPORT YouTubeVideoDetail::Comment *youtube_video_page_load_more_replies(const YouTubeVideoDetail::Comment &comment);
PRX_EXPORT YouTubeVideoDetail *youtube_video_page_load_caption(const YouTubeVideoDetail &prev_result, const std::string &base_lang_id, const std::string &translation_lang_id);



struct YouTubeChannelDetail {
	std::string id;
	std::string error;
	std::string name;
	std::string url;
	std::string url_original;
	std::string icon_url;
	std::string banner_url;
	std::string description;
	std::string subscriber_count_str;
	std::vector<YouTubeVideoSuccinct> videos;

	std::string continue_token;
	std::string continue_key;

	bool has_continue() const { return continue_token != "" && continue_key != ""; }
};
PRX_EXPORT YouTubeChannelDetail *youtube_parse_channel_page(std::string url);
PRX_EXPORT YouTubeChannelDetail *youtube_parse_channel_page(char *url);
// takes the previous result, returns the new result with both old items and new items
PRX_EXPORT YouTubeChannelDetail *youtube_channel_page_continue(const YouTubeChannelDetail &prev_result);

PRX_EXPORT void youtube_destroy_struct(YouTubeChannelDetail *s);
PRX_EXPORT void youtube_destroy_struct(YouTubeVideoDetail *s);
PRX_EXPORT void youtube_destroy_struct(YouTubeSearchResult *s);
PRX_EXPORT void youtube_destroy_struct(YouTubeVideoDetail::Comment *s);

PRX_EXPORT void youtube_change_content_language(const char *language_code);

// util function
PRX_EXPORT void youtube_get_video_thumbnail_url_by_id(const char *id, char *url, int urlLen);
PRX_EXPORT void youtube_get_video_thumbnail_hq_url_by_id(const char *id, char *url, int urlLen);
PRX_EXPORT void youtube_get_video_url_by_id(const char *id, char *url, int urlLen);
PRX_EXPORT void get_video_id_from_thumbnail_url(const char *url, char *id, int idLen);
PRX_EXPORT bool youtube_is_valid_video_id(const char *id);
PRX_EXPORT bool is_youtube_url(const char *url);
PRX_EXPORT bool is_youtube_thumbnail_url(const char *url);
PRX_EXPORT YouTubePageType youtube_get_page_type(const char *url);

#else

struct YouTubeChannelSuccinct {
	std::string name;
	std::string url;
	std::string icon_url;
	std::string subscribers;
	std::string video_num;
};
struct YouTubeVideoSuccinct {
	std::string url;
	std::string title;
	std::string duration_text;
	std::string publish_date;
	std::string views_str;
	std::string author;
	std::string thumbnail_url;
};
struct YouTubePlaylistSuccinct {
	std::string url;
	std::string title;
	std::string video_count_str;
	std::string thumbnail_url;
};

struct YouTubeSuccinctItem {
	// TODO : use union or std::variant
	enum {
		VIDEO,
		CHANNEL,
		PLAYLIST
	} type;
	YouTubeVideoSuccinct video;
	YouTubeChannelSuccinct channel;
	YouTubePlaylistSuccinct playlist;
	
	YouTubeSuccinctItem () = default;
	YouTubeSuccinctItem (YouTubeVideoSuccinct video) : type(VIDEO), video(video) {}
	YouTubeSuccinctItem (YouTubeChannelSuccinct channel) : type(CHANNEL), channel(channel) {}
	YouTubeSuccinctItem (YouTubePlaylistSuccinct playlist) : type(PLAYLIST), playlist(playlist) {}
	
	std::string get_url() const { return type == VIDEO ? video.url : type == CHANNEL ? channel.url : playlist.url; }
	std::string get_thumbnail_url() const { return type == VIDEO ? video.thumbnail_url : type == CHANNEL ? channel.icon_url : playlist.thumbnail_url; }
	std::string get_name() const { return type == VIDEO ? video.title : type == CHANNEL ? channel.name : playlist.title; }
};


struct YouTubeSearchResult {
	std::string error;
	int estimated_result_num;
	std::vector<YouTubeSuccinctItem> results;
	
	std::string continue_token;
	std::string continue_key;
	
	bool has_continue() const { return continue_token != "" && continue_key != ""; }
};
PRX_EXPORT YouTubeSearchResult youtube_parse_search(std::string url);
// takes the previous result, returns the new result with both old items and new items
PRX_EXPORT YouTubeSearchResult youtube_continue_search(const YouTubeSearchResult &prev_result);


struct YouTubeVideoDetail {
	std::string error;
	std::string url;
	std::string title;
	std::string description;
	YouTubeChannelSuccinct author;
	std::string audio_stream_url;
	std::map<int, std::string> video_stream_urls; // first : video size (144p, 240p, 360p ...)
	std::string both_stream_url;
	int duration_ms;
	bool is_livestream;
	enum class LivestreamType {
		PREMIERE,
		LIVESTREAM,
	};
	LivestreamType livestream_type;
	bool is_upcoming;
	std::string playability_status;
	std::string playability_reason;
	int stream_fragment_len; // used only for livestreams
	std::string like_count_str;
	std::string dislike_count_str;
	std::string publish_date;
	std::string views_str;
	
	// caption-related data
	std::string caption_base_url;
	struct CaptionBaseLanguage {
		std::string name; // e.g. "English", "Japanese"
		std::string id; // e.g. "en", "ja"
		std::string base_url; // empty string for instances in caption_translated_languages
		bool is_translatable;
	};
	struct CaptionTranslationLanguage {
		std::string name;
		std::string id;
	};
	std::vector<CaptionBaseLanguage> caption_base_languages;
	std::vector<CaptionTranslationLanguage> caption_translation_languages;
	struct CaptionPiece {
		float start_time;
		float end_time;
		std::string content;
	};
	using Caption = std::vector<CaptionPiece>;
	std::map<std::pair<std::string, std::string>, Caption> caption_data;
	
	std::vector<YouTubeSuccinctItem> suggestions;
	struct Playlist {
		std::string id;
		std::string title;
		std::string author_name;
		int total_videos;
		std::vector<YouTubeVideoSuccinct> videos;
		int selected_index;
	};
	Playlist playlist;
	
	struct Comment {
		YouTubeChannelSuccinct author;
		std::string content;
		std::string id;
		
		int reply_num;
		std::vector<Comment> replies;
		std::string continue_key; // innertube key, equals to YouTubeChannelDetail.continue_key
		std::string replies_continue_token;
		bool has_more_replies() const { return replies_continue_token != ""; }
	};
	std::vector<Comment> comments;
	
	std::string continue_key; // innertube key
	std::string suggestions_continue_token;
	std::string comment_continue_token;
	int comment_continue_type; // -1 : unavailable, 0 : using watch_comments, 1 : using innertube
	bool comments_disabled;
	
	bool has_more_suggestions() const { return continue_key != "" && suggestions_continue_token != ""; }
	bool has_more_comments() const { return comment_continue_type != -1; }
	bool needs_timestamp_adjusting() const { return is_livestream && livestream_type == LivestreamType::PREMIERE; }
	bool is_playable() const { return playability_status == "OK" && (both_stream_url != "" || (audio_stream_url != "" && video_stream_urls.size())); }
};
// this function does not load comments; call youtube_video_page_load_more_comments() if necessary
PRX_EXPORT YouTubeVideoDetail youtube_parse_video_page(std::string url);
PRX_EXPORT YouTubeVideoDetail youtube_video_page_load_more_suggestions(const YouTubeVideoDetail &prev_result);
PRX_EXPORT YouTubeVideoDetail youtube_video_page_load_more_comments(const YouTubeVideoDetail &prev_result);
PRX_EXPORT YouTubeVideoDetail::Comment youtube_video_page_load_more_replies(const YouTubeVideoDetail::Comment &comment);
PRX_EXPORT YouTubeVideoDetail youtube_video_page_load_caption(const YouTubeVideoDetail &prev_result, const std::string &base_lang_id, const std::string &translation_lang_id);



struct YouTubeChannelDetail {
	std::string id;
	std::string error;
	std::string name;
	std::string url;
	std::string url_original;
	std::string icon_url;
	std::string banner_url;
	std::string description;
	std::string subscriber_count_str;
	std::vector<YouTubeVideoSuccinct> videos;
	
	std::string continue_token;
	std::string continue_key;
	
	bool has_continue() const { return continue_token != "" && continue_key != ""; }
};
PRX_EXPORT YouTubeChannelDetail youtube_parse_channel_page(std::string url);
// takes the previous result, returns the new result with both old items and new items
PRX_EXPORT YouTubeChannelDetail youtube_channel_page_continue(const YouTubeChannelDetail &prev_result);
#endif


