#ifndef SERVER_POLL_HPP_
#define SERVER_POLL_HPP_

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <map>
#include <string>
#include <vector>
#include <csignal>
#include <stdexcept>
#include <iostream>

#include "instance.hpp"
#include "../http/client.hpp"
#include "../models/enums.hpp"
#include "../models/consts.hpp"
#include "../models/IServer.hpp"

namespace Webserv {
namespace Server {
class Poll {
 public:
	typedef Webserv::Models::IServer	IServer;
	typedef Webserv::Models::ERead		ERead;
	typedef Webserv::Server::Instance	Instance;
	typedef Webserv::Http::Client		Client;

 private:
	bool	_alive;
	int		epoll_fd;

	std::map<int, Instance *> _instances;
	std::map<int, Client *> _clients;

 public:
	explicit Poll(const std::vector<IServer *> &servers) : _alive(true) {
		if (!_create_poll())
			throw std::runtime_error("Error while initializing epoll.");
		if (!_add_servers(servers))
			throw std::runtime_error("Error while adding servers to epoll.");
		if (!_add_stdin())
			throw std::runtime_error("Error while adding stdin to epoll.");
	}

	~Poll() {
		for (std::map<int, Instance *>::iterator it = _instances.begin();
			it != _instances.end(); ++it)
			delete it->second;

		for (std::map<int, Client *>::iterator it = _clients.begin();
			it != _clients.end(); ++it)
			delete it->second;
		close(epoll_fd);
	}

	int	run() {
		struct epoll_event events[MAX_CONNS];

		std::cout << "Up and awaiting..." << std::endl;

		int	nfds, i;
		while (_alive) {
			nfds = epoll_wait(epoll_fd, events, MAX_CONNS, -1);
			for (i = 0; i < nfds; i++) {
				int ev_fd = events[i].data.fd;
				if (ev_fd == STDIN_FILENO) {
					_handle_stdin();
					continue;
				}
				if (events[i].events & EPOLLERR || events[i].events & EPOLLHUP) {
					close(ev_fd);
					continue;
				}
				if (_instances.find(ev_fd) != _instances.end()) {
					_handle_connection(ev_fd);
				} else if (events[i].events & EPOLLIN) {
					_handle_read(ev_fd);
				} else if (events[i].events & EPOLLOUT) {
					_handle_write(ev_fd);
				}
			}
		}

		return 0;
	}

 private:
	bool	_create_poll() {
		epoll_fd = epoll_create1(0);
		return (epoll_fd != -1);
	}

	bool	_add_servers(const std::vector<IServer *> &servers) {
		std::vector<IServer *>::const_iterator it = servers.begin();
		for (; it != servers.end(); it++) {
			if (!_add_server(*it))
				return false;
		}
		return true;
	}
	bool	_add_server(const IServer* serv) {
		Instance *new_server;
		try {
			new_server = new Instance(*serv);
			if (!new_server) {
				std::cerr << "add_server: alloc failed" << std::endl;
				return false;
			}
		} catch (std::exception &e) {
			return false;
		}

		int new_fd = new_server->get_fd();
		if (new_fd == -1) {
			delete new_server;
			std::cerr << "add_server: invalid fd" << std::endl;
			return false;
		}

		struct epoll_event event = {};
		event.events = EPOLLIN;
		event.data.fd = new_fd;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_fd, &event) == -1) {
			delete new_server;
			std::cerr << "add_server: epoll_ctl failed" << std::endl;
			return false;
		}

		_instances[new_fd] = new_server;
		return true;
	}
	bool	_add_stdin() {
		struct	epoll_event event = {};
		event.events = EPOLLIN;
		event.data.fd = STDIN_FILENO;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1) {
			std::cerr << "add_stdin: epoll_ctl failed" << std::endl;
			return false;
		}
		return true;
	}

	void	_handle_connection(int fd) {
		Client *client = new Client(fd);
		if (!client) {
			std::cerr << "handle_connection: alloc failed" << std::endl;
			return;
		}

		int new_fd = client->get_fd();
		if (new_fd == -1) {
			delete client;
			std::cerr << "handle_connection: invalid fd" << std::endl;
			return;
		}

		struct epoll_event event = {};
		event.events = EPOLLIN;
		event.data.fd = new_fd;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_fd, &event) == -1) {
			delete client;
			std::cerr << "handle_connection: epoll_add failed()" << std::endl;
			return;
		}
		_clients[new_fd] = client;
	}
	void	_handle_read(int ev_fd) {
		if (_clients.find(ev_fd) == _clients.end()) {
			std::cerr << "handle_read: invalid fd" << std::endl;
			return;
		}

		Client *client = _clients[ev_fd];
		ERead ret = client->read_request();
		if (ret == Models::READ_EOF || ret == Models::READ_ERROR) {
			return _delete_client(ev_fd, client);
		}
		return _change_epoll_state(ev_fd, EPOLLOUT);
	}
	void	_handle_write(int ev_fd) {
		Client *client = _clients[ev_fd];
		if (!client) {
			std::cerr << "handle_write: invalid fd" << std::endl;
			return;
		}

		client->send_response();
		if (client->do_close())
			return _delete_client(ev_fd, client);
		return _change_epoll_state(ev_fd, EPOLLIN);
	}
	void	_handle_stdin() {
		std::string line;
		std::getline(std::cin, line);

		if (line == "quit" || line == "exit") {
			_alive = false;
			std::cout << "Shutting down Webserv gracefully..." << std::endl;
		}
	}

	void	_delete_client(int ev_fd, Client *client) {
		delete client;
		_clients.erase(ev_fd);
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ev_fd, NULL);
	}

	void	_change_epoll_state(int ev_fd, int state) {
		struct epoll_event event = {};
		event.events = state;
		event.data.fd = ev_fd;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ev_fd, &event) == -1) {
			std::cerr << "_change_epoll_state: failed()" << std::endl;
			Client *client = _clients[ev_fd];
			return _delete_client(ev_fd, client);
		}
	}
};
}  // namespace Server
}  // namespace Webserv

#endif  // SERVER_POLL_HPP_