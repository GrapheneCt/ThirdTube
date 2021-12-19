#pragma once
#include <vector>
#include <map>
#include <string>
#include <kernel.h>
#include <curl/curl.h>

#define NETWORK_FRAMEWORK_HTTPC 0
#define NETWORK_FRAMEWORK_SSLC 1
#define NETWORK_FRAMEWORK_LIBCURL 2

struct NetworkResult {

	NetworkResult()
	{

	}

	~NetworkResult()
	{
		data.clear();
	}

	std::string redirected_url;
	bool fail = false; // receiving http error code like 404 is still counted as a 'success'
	std::string error;
	int status_code = -1;
	std::string status_message;
	std::vector<uint8_t> data;
	std::map<std::string, std::string> response_headers;
	char *responseHeaders;
	SceSize responseHeadersLen;
	SceInt32 templateId;
	SceInt32 connectionId;
	SceInt32 requestId;
	
	bool status_code_is_success() { return status_code / 100 == 2; }
	std::string get_header(std::string key);
	void finalize();
};

struct NetworkSession {
	bool inited = false;
	bool fail = false;
	SceNetId sockfd = -1;
	std::string host_name;
	
	void open(std::string host_name);
	void close();
};

NetworkResult Access_http_get(std::string url, const std::map<std::string, std::string> &request_headers, bool follow_redirect = true);
NetworkResult Access_http_post(const std::string &url, const std::map<std::string, std::string> &request_headers,
	const std::string &body);

std::string url_get_host_name(const std::string &url);

#define HTTP_STATUS_CODE_OK 200
#define HTTP_STATUS_CODE_NO_CONTENT 204
#define HTTP_STATUS_CODE_PARTIAL_CONTENT 206
#define HTTP_STATUS_CODE_FORBIDDEN 403
#define HTTP_STATUS_CODE_NOT_FOUND 404
