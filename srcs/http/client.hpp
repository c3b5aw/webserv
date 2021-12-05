#ifndef HTTP_CLIENT_HPP_
#define HTTP_CLIENT_HPP_

#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <ctime>
#include <string>
#include <sstream>

#include "enums.hpp"
#include "request.hpp"
#include "response.hpp"
#include "../consts.hpp"
#include "../models/IServer.hpp"

namespace Webserv {
namespace HTTP {
class Client {
	typedef Webserv::HTTP::Request		Request;
	typedef Webserv::Models::IServer	IServer;

 private:
	IServer	*_master;

	struct sockaddr_in	_addr;
	socklen_t 			_addr_len;

	std::string 	_ip;
	int				_fd;
	struct timeval 	ping;

	Request		*req;
	Response	*resp;

 public:
	Client(IServer *master, int ev_fd)
	:	_master(master),
		_addr(), _addr_len(0),
		_fd(-1) ,
		req(0), resp(0) {
		_fd = accept(ev_fd, (struct sockaddr *)&_addr, &_addr_len);
		if (fcntl(_fd, F_SETFL, O_NONBLOCK) == -1) {
			close(_fd);
			_fd = -1;
			std::cerr << "fcntl() failed" << std::endl;
		}
		gettimeofday(&ping, NULL);
		#ifndef WEBSERV_BENCHMARK
			_resolve_client_ip();
		#endif
	}

	~Client() {
		if (_fd != -1)
			close(_fd);
		if (req)
			delete req;
		if (resp)
			delete resp;
	}

	READ read_request() {
		char buffer[WEBSERV_REQUEST_BUFFER_SIZE + 1] = {0};
		ssize_t n = recv(_fd, buffer, WEBSERV_REQUEST_BUFFER_SIZE, 0);
		if (n == -1) {
			return READ_ERROR;
		} else if (n == 0) {
			return READ_EOF;
		} else {
			if (req == NULL) {
				req = new Request(buffer);
				ping = *(req->get_time());
			} else {
				req->handle_buffer(buffer);
			}
			return _request_status();
		}
	}

	void	abort(int code) {
		if (resp)
			delete resp;
		resp = new Response(code);
		resp->prepare(_master);

		send(_fd, resp->toString(), resp->size(), 0);
	}

	bool	send_response() {
		if (resp)
			delete resp;
		resp = new Response(req);
		resp->prepare(_master);
		send(_fd, resp->toString(), resp->size(), 0);
		return _close();
	}

	int	get_fd() const { return _fd; }
	bool	is_expired(time_t now) const {
		return (now - ping.tv_sec) > WEBSERV_CLIENT_TIMEOUT;
	}

 private:
	READ	_request_status() {
		bool header_status = req->get_header_status();
		const std::string request = req->get_raw_request();
		if (header_status == false && request.find("\r\n\r\n") == std::string::npos) {
			return READ_WAIT;
		} else {
			if (header_status == false) {
				if (req->init() == false) {
					return READ_OK;
				}
			}
			if (req->get_method() == METH_POST) {
				if (req->read_body() == false) {
					return READ_WAIT;
				}
				return READ_OK;
			}
			return READ_OK;
		}
	}

	bool	_close() {
		if (req) {
			bool state = req->closed();

			#ifndef WEBSERV_BENCHMARK
				__log();
			#endif

			delete req;
			req = NULL;
			return state;
		}
		return true;
	}

	void	__log() const {
		struct timeval _end;
		gettimeofday(&_end, NULL);

		time_t time = (time_t)_end.tv_sec;
		struct tm local_time;
		localtime_r(&time, &local_time);
		char buffer[25];

		strftime(buffer, 25, "%Y/%m/%d - %H:%M:%S", &local_time);
		std::cout << "[\033[1;36mWEBSERV\033[0m] " << buffer << " |"
		<< get_method() << " " << get_uri() << " |"
		<< get_http_code() << "| "
		<< get_time_diff(&_end) << " | "
		<< get_client_ip() << " -> " << get_master_ip() << std::endl;
	}

	void	_resolve_client_ip() {
		getpeername(_fd, (struct sockaddr *)&_addr, &_addr_len);
		_ip = inet_ntoa(_addr.sin_addr);
	}

	const std::string	get_client_ip() const { return _ip; }
	const std::string get_master_ip() const { return _master->get_ip(); }

	std::string	get_http_code() const {
		if (resp)
			return color_code((STATUS_CODE)resp->status());
		return "?";
	}

	std::string	get_method() const {
		if (req)
			return color_method(req->get_method());
		return color_method(METH_UNKNOWN);
	}

	std::string get_uri() const {
		if (req) {
			std::string uri = req->get_uri();

			if (uri.size() > 20)
				return (uri.substr(0, 18) + "..");
			uri.insert(uri.size(), 20 - uri.size(), ' ');
			return uri;
		}
		return "?                   ";
	}

	const struct timeval *get_time() const {
		if (req)
			return req->get_time();
		return NULL;
	}

	std::string	get_time_diff(struct timeval *ptr) const {
		if (get_time() == 0)
			return "        - ";
		size_t	usec = ptr->tv_usec - get_time()->tv_usec;
		size_t	sec = ptr->tv_sec - get_time()->tv_sec;
		bool	is_micro = false;
		std::stringstream ss;

		if (sec > 0) {
			ss << sec << " s";
		} else {
			if (usec >= 1000) {
				ss << usec / 1000 << " ms";
			} else {
				ss << usec << " μs";
				is_micro = true;
			}
		}

		std::string str = ss.str();
		if (str.size() > 10)
			return "eternity..";
		return str.insert(0, (is_micro ? 11 : 10) - str.length(), ' ');
	}
};
}  // namespace HTTP
}  // namespace Webserv

#endif  // HTTP_CLIENT_HPP_
