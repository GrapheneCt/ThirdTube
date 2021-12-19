#include "parser.hpp"
#include <stdio.h>

void youtube_destroy_struct(YouTubeChannelDetail *s)
{
	delete s;
}

void youtube_destroy_struct(YouTubeVideoDetail *s)
{
	malloc_managed_size sz;
	malloc_stats_fast(&sz);
	printf("before: %u\n", sz.current_system_size - sz.current_inuse_size);

	delete s;

	malloc_stats_fast(&sz);
	printf("after: %u\n", sz.current_system_size - sz.current_inuse_size);
}

void youtube_destroy_struct(YouTubeSearchResult *s)
{
	delete s;
}

void youtube_destroy_struct(YouTubeVideoDetail::Comment *s)
{
	delete s;
}

	void youtube_change_content_language(const char *language_code)
	{
		std::string str(language_code);
		youtube_change_content_language(str);
	}

	// util function
	void youtube_get_video_thumbnail_url_by_id(const char *id, char *url, int urlLen)
	{
		std::string str(id);
		std::string res = youtube_get_video_thumbnail_url_by_id(str);
		strncpy(url, res.c_str(), urlLen);
	}
	void youtube_get_video_thumbnail_hq_url_by_id(const char *id, char *url, int urlLen)
	{
		std::string str(id);
		std::string res = youtube_get_video_thumbnail_hq_url_by_id(str);
		strncpy(url, res.c_str(), urlLen);
	}
	void youtube_get_video_url_by_id(const char *id, char *url, int urlLen)
	{
		std::string str(id);
		std::string res = youtube_get_video_url_by_id(str);
		strncpy(url, res.c_str(), urlLen);
	}
	void get_video_id_from_thumbnail_url(const char *url, char *id, int idLen)
	{
		std::string str(url);
		std::string res = get_video_id_from_thumbnail_url(str);
		strncpy(id, res.c_str(), idLen);
	}
	bool youtube_is_valid_video_id(const char *id)
	{
		std::string str(id);
		return youtube_is_valid_video_id(str);
	}
	bool is_youtube_url(const char *url)
	{
		std::string str(url);
		return is_youtube_url(str);
	}
	bool is_youtube_thumbnail_url(const char *url)
	{
		std::string str(url);
		return is_youtube_thumbnail_url(str);
	}

	YouTubePageType youtube_get_page_type(const char *url)
	{
		std::string str(url);
		return youtube_get_page_type(str);
	}

