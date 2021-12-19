#include "headers.hpp"
#include "network/network_io.hpp"
#include <cassert>
#include <cctype>
#include <deque>
#include <stdio.h>

#include <libhttp.h>

extern "C" {
	SceInt32 g_httpTemplate = -1;
}

void NetworkSession::open(std::string host_name) {
	
	this->host_name = host_name;
	
	this->fail = false;
	inited = true;
	return;

}
void NetworkSession::close() {
	if (inited) {
		inited = false;
	}
}


/*
	HTTP/1.1 client
*/

std::string url_get_host_name(const std::string &url) {
	auto pos0 = url.find("://");
	if (pos0 == std::string::npos) return "";
	pos0 += 3;
	return std::string(url.begin() + pos0, std::find(url.begin() + pos0, url.end(), '/'));
}
static std::string get_page_url(const std::string &url) {
	auto pos0 = url.find("://");
	if (pos0 == std::string::npos) return "";
	pos0 += 3;
	auto res = std::string(std::find(url.begin() + pos0, url.end(), '/'), url.end());
	if (res == "") res = "/";
	return res;
}
static std::string remove_leading_whitespaces(std::string str) {
	size_t i = 0;
	while (i < str.size() && str[i] == ' ') i++;
	return str.substr(i, str.size() - i);
}

struct ResponseHeader {
	int status_code = -1;
	std::string status_message;
	std::map<std::string, std::string> headers;
};
static ResponseHeader parse_header(std::string header) {
	ResponseHeader res;
	std::vector<std::string> header_lines;
	{
		std::vector<std::string> header_lines_tmp = {""};
		for (auto c : header) {
			if (c == '\n') header_lines_tmp.push_back("");
			else header_lines_tmp.back().push_back(c);
		}	
		for (auto line : header_lines_tmp) {
			if (line.size() && line.back() == '\r') line.pop_back();
			if (!line.size()) continue;
			header_lines.push_back(line);
		}
	}
	// parse status line
	{
		if (!header_lines.size()) {
			//sceClibPrintf("http, Empty header");
			return res;
		}
		const std::string &status_line = header_lines[0];
		if (status_line.substr(0, 8) != "HTTP/1.1" && status_line.substr(0, 8) != "HTTP/1.0") {
			//sceClibPrintf("http, Invalid status line");
			return res;
		}
		size_t head = 8;
		while (head < status_line.size() && std::isspace(status_line[head])) head++;
		char *end;
		res.status_code = strtol(status_line.c_str() + head, &end, 10);
		if (end == status_line.c_str() + head) {
			//sceClibPrintf("http, failed to parse status code in the status line");
			res.status_code = -1;
			return res;
		}
		head = end - status_line.c_str();
		while (head < status_line.size() && std::isspace(status_line[head])) head++;
		res.status_message = status_line.c_str() + head;
		header_lines.erase(header_lines.begin());
	}
	for (auto line : header_lines) {
		auto colon = std::find(line.begin(), line.end(), ':');
		if (colon == line.end()) {
			//sceClibPrintf("http, Header line without a colon, ignoring");
			continue;
		}
		auto key = remove_leading_whitespaces(std::string(line.begin(), colon));
		auto value = remove_leading_whitespaces(std::string(colon + 1, line.end()));
		res.headers[key] = value;
	}
	return res;
}
struct ChunkProcessor {
	std::string result;
	std::deque<char> buffer;
	int size = -1;
	int size_size = -1;
	bool error = false;
	
	int parse_hex(std::string str) {
		int res = 0;
		for (auto c : str) {
			res *= 16;
			if (std::isdigit(c)) res += c - '0';
			else if (c >= 'A' && c <= 'F') res += c - 'A' + 10;
			else if (c >= 'a' && c <= 'f') res += c - 'a' + 10;
			else {
				//sceClibPrintf("hex", "unexpected char");
				error = true;
				return -1;
			}
		}
		return res;
	}
	void try_to_parse_size() {
		size_t pos = 0;
		for (; pos + 1 < buffer.size(); pos++) if (buffer[pos] == '\r' && buffer[pos + 1] == '\n') break;
		if (pos + 1 >= buffer.size()) size = size_size = -1;
		else {
			size_size = pos;
			size = parse_hex(std::string(buffer.begin(), buffer.begin() + size_size));
		}
	}
	
	// -1 : error
	// 0 : not the end
	// 1 : end reached
	template<class T> int push(const T &str) {
		buffer.insert(buffer.end(), str.begin(), str.end());
		if (size == -1) {
			try_to_parse_size();
			if (error) return -1;
			if (size == -1) return 0;
		}
		while ((int) buffer.size() >= size_size + 2 + size + 2) {
			{
				auto tmp = std::string(buffer.begin() + size_size + 2 + size, buffer.begin() + size_size + 2 + size + 2);
				if (tmp != "\r\n") {
					Util_log_save("http-chunk", "expected \\r\\n after chunk content end : " + std::to_string(tmp[0]) + " " + std::to_string(tmp[1]));
					return -1;
				}
			}
			result.insert(result.end(), buffer.begin() + size_size + 2, buffer.begin() + size_size + 2 + size);
			buffer.erase(buffer.begin(), buffer.begin() + size_size + 2 + size + 2);
			// Util_log_save("http-chunk", "read chunk size : " + std::to_string(size));
			if (size == 0) {
				if (buffer.size()) {
					Util_log_save("http-chunk", "trailing data : " + std::string(buffer.begin(), buffer.end()));
					return -1;
				}
				return 1;
			}
			try_to_parse_size();
			if (error) return -1;
			if (size == -1) break;
		}
		return 0;
	}
};

static NetworkResult access_http_internal(const std::string &method, const std::string &url,
	std::map<std::string, std::string> request_headers, const std::string &body, bool follow_redirect) {

	NetworkResult res;

	if (url.substr(0, 7) != "http://" && url.substr(0, 8) != "https://") {
		res.fail = true;
		res.error = "unknown protocol";
		return res;
	}

	static const std::string DEFAULT_USER_AGENT = "Mozilla/5.0 (Linux; Android 11; Pixel 3a) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.101 Mobile Safari/537.36";
	static const std::map<std::string, std::string> default_headers = {
		{"Accept", "*/*"},
		{"Connection", "Keep-Alive"},
		{"User-Agent", DEFAULT_USER_AGENT}
	};

	uint8_t *buffer = new uint8_t[0x1000];

	for (auto header : default_headers) if (!request_headers.count(header.first)) request_headers[header.first] = header.second;
	{
		SceInt32 ret = 0;
		SceInt32 status_code = 0;
		if (g_httpTemplate >= 0) {
			res.templateId = g_httpTemplate;
		}
		else {
			res.templateId = sceHttpCreateTemplate(DEFAULT_USER_AGENT.c_str(), SCE_HTTP_VERSION_1_1, SCE_FALSE);
			g_httpTemplate = res.templateId;
		}
		res.connectionId = sceHttpCreateConnectionWithURL(res.templateId, url.c_str(), SCE_TRUE);
		for (auto i : request_headers) sceHttpAddRequestHeader(res.connectionId, i.first.c_str(), i.second.c_str(), SCE_HTTP_HEADER_OVERWRITE);

		res.requestId = sceHttpCreateRequestWithURL(res.connectionId, method == "GET" ? SCE_HTTP_METHOD_GET : SCE_HTTP_METHOD_POST, url.c_str(), method == "GET" ? 0 : body.size());

		if (method == "POST") {
			sceHttpSendRequest(res.requestId, body.c_str(), body.size());
		}
		else {
			sceHttpSendRequest(res.requestId, NULL, 0);
		}

		ret = sceHttpGetStatusCode(res.requestId, &status_code);

		if (ret != 0) {
			res.fail = true;
			res.error = "sceHttpGetStatusCode() failed";
			delete buffer;
			return res;
		}
		res.status_code = status_code;

		sceHttpGetAllResponseHeaders(res.requestId, &res.responseHeaders, &res.responseHeadersLen);

		SceInt32 len_read = 0;

		while (1) {
			len_read = sceHttpReadData(res.requestId, buffer, 0x1000);
			if (len_read <= 0) break;
			res.data.insert(res.data.end(), buffer, buffer + len_read);
		}

	}

	delete buffer;
	
	return res;
}
NetworkResult Access_http_get(std::string url, const std::map<std::string, std::string> &request_headers, bool follow_redirect) {
	NetworkResult result;
	while (1) {
		result = access_http_internal("GET", url , request_headers, "", follow_redirect);
		if (result.status_code / 100 != 3) {
			result.redirected_url = url;
			return result;
		}
		//sceClibPrintf("http, redir");
		if (!follow_redirect) {
			result.redirected_url = result.get_header("Location");
			return result;
		}
		auto new_url = result.get_header("Location");

		result.finalize();
		url = new_url;
	}
}
NetworkResult Access_http_post(const std::string &url, const std::map<std::string, std::string> &request_headers,
	const std::string &data) {
	
	auto result = access_http_internal("POST", url , request_headers, data, false);
	result.redirected_url = url;
	return result;
}
void NetworkResult::finalize () {
	sceHttpDeleteRequest(this->requestId);
	sceHttpDeleteConnection(this->connectionId);
}

std::string NetworkResult::get_header(std::string key) {
	char buffer[0x1000] = { 0 };
	char *bufPtr = &buffer[0];
	SceSize resLen = 0;

	sceHttpParseResponseHeader(this->responseHeaders, this->responseHeadersLen, key.c_str(), &bufPtr, &resLen);

	return std::string(buffer);
}



