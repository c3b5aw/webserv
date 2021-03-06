#ifndef HTTP_REQUEST_HPP_
#define HTTP_REQUEST_HPP_

#include <stdlib.h>

#include <map>
#include <string>
#include <utility>
#include <iostream>

#include "http/enums.hpp"
#include "http/utils.hpp"

namespace Webserv {
namespace HTTP {
class Request {
 public:
	typedef std::map<std::string, std::string> HeadersObject;

	#ifdef WEBSERV_SESSION
	typedef std::map<std::string, std::string> Cookies;
	#endif

 private:
	struct timeval _time;

	std::string	_raw_request;

	METHODS		_method;
	std::string _host;
	std::string	_uri;
	std::string _query;
	std::string _http_version;

	HeadersObject	_headers;

	FORM		_post_form;
	size_t		_body_size;
	std::string	_multipart_boundary;

	#ifdef WEBSERV_SESSION
	Cookies _cookies;
	#endif

	bool	_headers_ready;
	bool	_body_ready;
	bool	_chunked;
	bool	_closed;

	STATUS_CODE	_http_code;

 public:
	explicit Request(const std::string &buffer)
	:	_raw_request(buffer),
		_method(METH_UNKNOWN),
		_host(""), _uri(""),
		 _http_version(""),
		_headers(),
		_post_form(FORM_UNKNOWN),
		_body_size(0),
		_multipart_boundary(""),
		_headers_ready(false),
		 _body_ready(false),
		_chunked(false), _closed(false),
		_http_code(OK) {
		gettimeofday(&_time, NULL);
	}

	void	handle_buffer(const std::string &buffer) { _raw_request += buffer; }

	bool	init() {
		if (_extract_method() == false)
			return false;
		if (_extract_uri() == false)
			return false;
		if (_extract_http_version() == false)
			return false;
		if (_extract_headers(&_headers) == false)
			return false;
		if (_validate_host() == false)
			return false;
		if (_method == METH_POST && _validate_post() == false)
			return false;
		_raw_request.erase(0, 2);
		_headers_ready = true;
		return true;
	}

	bool	read_body() {
		if (_chunked == true) {
			if (_read_chunks() == READ_WAIT)
				return false;
			_body_size = _raw_request.size();
			_chunked = false;
		}
		if (_raw_request.size() < _body_size)
			return false;
		_body_ready = true;
		return true;
	}

	bool	closed() {
		if (!_closed) {
			HeadersObject::const_iterator it = _headers.find("connection");
			if (it != _headers.end()) {
				if (it->second == "close") {
					_closed = true;
					return true;
				}
			}
		}
		return _closed;
	}

	const struct timeval *get_time() const { return &_time; }
	int	get_code() const { return _http_code; }
	const std::string get_raw_request() const { return _raw_request; }
	METHODS		get_method() const { return _method; }
	const std::string get_uri() const { return _uri; }
	const std::string get_query() const { return _query; }
	const std::string get_host() const { return _host; }
	bool		get_header_status() const { return _headers_ready; }
	const HeadersObject &get_headers() {
		return _headers;
	}
	const std::string get_header_value(const std::string &headerName) const {
		HeadersObject::const_iterator it = _headers.find(headerName);
		if (it != _headers.end())
			return it->second;
		return "";
	}

	#ifdef WEBSERV_SESSION
	const Cookies &get_cookies() const {
		return _cookies;
	}
	#endif

	#ifdef WEBSERV_SESSION
	void	add_cookie(const std::string &key, const std::string &value) {
		_cookies[key] = value;
		HeadersObject::iterator it = _headers.find("cookies");
		if (it == _headers.end())
			_headers["cookies"] = key + "=" + value;
		else
			_headers["cookies"] += "; " + key + "=" + value;
	}
	#endif

 private:
	READ	_read_chunks() {
		if (_raw_request.find("0\r\n\r\n") == std::string::npos) {
			return READ_WAIT;
		} else {
			std::string payload;
			do {
				const size_t header_end = _raw_request.find("\r\n");
				if (header_end == std::string::npos) {
					return READ_ERROR;
				}
				const int64_t chunk_size = strtol(
						_raw_request.substr(0, header_end).c_str(), NULL, 16);
				_raw_request.erase(0, header_end + 2);
				if (chunk_size == 0) {
					_raw_request = payload;
					return READ_OK;
				}
				payload += _raw_request.substr(0, chunk_size);
				_raw_request.erase(0, chunk_size + 2);
			} while (true);
		}
	}

	bool	_extract_method() {
		const size_t method_separator_pos = _raw_request.find(" ");
		if (method_separator_pos == std::string::npos)
			return _invalid_request(BAD_REQUEST);
		_method = enumerate_method(_raw_request.substr(0, method_separator_pos));
		_raw_request.erase(0, method_separator_pos + 1);
		if (_method == METH_UNKNOWN)
			return _invalid_request(BAD_REQUEST);
		if (_method == METH_GET || _method == METH_POST
			|| _method == METH_DELETE)
			return true;
		return _invalid_request(NOT_IMPLEMENTED);
	}

	bool	_extract_uri() {
		const size_t uri_separator_pos = _raw_request.find(" ");
		if (uri_separator_pos == std::string::npos)
			return _invalid_request(BAD_REQUEST);
		if (uri_separator_pos > 8192)
			return _invalid_request(REQUEST_URI_TOO_LONG);
		_uri = _raw_request.substr(0, uri_separator_pos);
		if (_uri == "" || _uri.find("/") != 0)
			return _invalid_request(BAD_REQUEST);
		_raw_request.erase(0, uri_separator_pos + 1);
		if (_uri.find("?") != std::string::npos) {
			_query = _uri.substr(_uri.find("?") + 1);
			_uri.erase(_uri.find("?"));
		}
		return true;
	}

	bool	_extract_http_version() {
		const size_t version_separator_pos = _raw_request.find("\r\n");
		if (version_separator_pos == std::string::npos)
			return _invalid_request(BAD_REQUEST);
		_http_version = _raw_request.substr(0, version_separator_pos);
		_strtolower(&_http_version);
		size_t version_start;
		if (_http_version == "" || _http_version.find("http") != 0
			|| (version_start = _http_version.find("/")) == std::string::npos)
			return _invalid_request(BAD_REQUEST);
		_http_version = _http_version.substr(version_start + 1);
		if (_http_version != "1.1")
			return _invalid_request(HTTP_VERSION_NOT_SUPPORTED);
		_raw_request.erase(0, version_separator_pos + 2);
		return true;
	}

	bool	_extract_headers(HeadersObject *bucket) {
		size_t header_size = 0;
		size_t	header_separator_pos = _raw_request.find("\r\n");
		while (header_separator_pos != std::string::npos) {
			std::string	header_str = _raw_request.substr(0, header_separator_pos);
			size_t		header_name_separator_pos = header_str.find(":");
			if (header_name_separator_pos == std::string::npos)
				break;
			std::string	header_name = header_str.substr(0, header_name_separator_pos);
			std::string	header_value = header_str.substr(header_name_separator_pos + 1);
			_strtolower(&header_name);
			if (header_name != "cookie")
				_strtolower(&header_value);
			_trim(&header_value);
			(*bucket)[header_name] = header_value;
			#ifdef WEBSERV_SESSION
			if (header_name == "cookie")
				_extract_cookies(header_value);
			#endif
			if (header_name == "host")
				_host = header_value;
			_raw_request.erase(0, header_separator_pos + 2);
			header_size += header_separator_pos + 2;
			if (header_size > 16384)
				return _invalid_request(REQUEST_HEADER_FIELDS_TOO_LARGE);
			header_separator_pos = _raw_request.find("\r\n");
		}
		return true;
	}

	#ifdef WEBSERV_SESSION
	void	_extract_cookies(std::string data) {
		try {
			while (data.find(";")) {
				const std::string cookie = data.substr(0, data.find(";"));
				_cookies[cookie.substr(0, cookie.find("="))]
					= cookie.substr(cookie.find("=") + 1);
				if (cookie.size() + 2 > data.size())
					break;
				data = data.substr(cookie.size() + 2);
			}
		} catch (std::exception &e) {
			return;
		}
	}
	#endif

	bool	_validate_host() {
		HeadersObject::const_iterator it = _headers.find("host");
		if (it == _headers.end() || it->second == "") {
			return _invalid_request(BAD_REQUEST);
		}
		return true;
	}

	bool	_validate_post() {
		HeadersObject::const_iterator it = _headers.find("transfer-encoding");
		if (it != _headers.end() &&
			it->second.find("chunked") != std::string::npos) {
			_chunked = true;
		} else {
			it = _headers.find("content-length");
			if (it == _headers.end() || it->second == "")
				return _invalid_request(BAD_REQUEST);
			_body_size = static_cast<size_t>(strtol(it->second.c_str(), NULL, 10));
		}
		it = _headers.find("content-type");
		if (it == _headers.end() || it->second == "")
			return _invalid_request(BAD_REQUEST);
		if (it->second == "application/x-www-form-urlencoded")
			_post_form = FORM_URLENCODED;
		else if (it->second == "multipart/form-data")
			_post_form = FORM_MULTIPART;
		return true;
	}

	bool	_invalid_request(STATUS_CODE http_code) {
		_http_code = http_code;
		_closed = true;
		return false;
	}

	inline std::string* _strtolower(std::string *s) {
		for (std::string::iterator it = s->begin(); it != s->end(); it++)
			*it = std::tolower(*it);
		return s;
	}

	inline std::string* _rtrim(std::string *s, const char *t = " \t") {
	    s->erase(s->find_last_not_of(t) + 1);
	    return s;
	}

	inline std::string* _ltrim(std::string *s, const char *t = " \t") {
	    s->erase(0, s->find_first_not_of(t));
	    return s;
	}

	inline std::string* _trim(std::string *s, const char *t = " \t") {
	    return _ltrim(_rtrim(s, t), t);
	}
};
}  // namespace HTTP
}  // namespace Webserv

#endif  // HTTP_REQUEST_HPP_
