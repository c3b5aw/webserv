/*
	Interface representing a server block
*/

#ifndef MODELS_ISERVER_HPP_
#define MODELS_ISERVER_HPP_

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <utility>

#ifdef WEBSERV_SESSION
#include "http/session.hpp"
#endif

#include "models/IBlock.hpp"
#include "models/ILocation.hpp"

namespace Webserv {
namespace Models {

class IServer : public Webserv::Models::IBlock {
 public:
	typedef Webserv::Models::ILocation ILocation;

	typedef std::map<std::string, ILocation *>	LocationObject;
	typedef std::map<std::string, IServer *>	VHostsObject;

	#ifdef WEBSERV_SESSION
	typedef std::map<std::string, Session *>  	Sessions;
	#endif

 protected:
	const std::string _host;

	LocationObject	_locations;
	VHostsObject	_vhosts;

	#ifdef WEBSERV_SESSION
	Sessions	_sessions;
	#endif

 public:
	IServer()
	:	_host("0.0.0.0") {
		_port = 8000;
		if (getuid() == 0)  // nginx docs set 80 to root usr
			set_port(80);
	}

	IServer(const std::string &name, const std::string &host, const int port)
	:	_host(host) {
		_name = name;
		_port = port;
	}

	IServer(const IServer &lhs)
	:	_host(lhs._host) {
		_name = lhs._name;
		_port = lhs._port;
		_root = lhs._root;
		_upload_pass = lhs._upload_pass;
		_redirection = lhs._redirection;
		_redirection_code = lhs._redirection_code;
		_body_limit = lhs._body_limit;

		for (int i = 0; i < WEBSERV_METHODS_SUPPORTED; i++)
			_methods_allowed[i] = true;

		_autoindex = lhs._autoindex;

		LocationObject::const_iterator it;
		for (it = lhs._locations.begin(); it != lhs._locations.end(); ++it) {
			_locations[it->first] = it->second->clone();
		}

		std::vector<std::string>::const_iterator it2;
		for (it2 = lhs._indexs.begin(); it2 != lhs._indexs.end(); ++it2) {
			_indexs.push_back(*it2);
		}

		ErrorPagesObject::const_iterator it3;
		for (it3 = lhs._error_pages.begin(); it3 != lhs._error_pages.end(); ++it3) {
			_error_pages[it3->first] = it3->second;
		}

		CGIObject::const_iterator it4;
		for (it4 = lhs._cgi.begin(); it4 != lhs._cgi.end(); ++it4) {
			_cgi[it4->first] = it4->second;
		}

		VHostsObject::const_iterator it5;
		for (it5 = lhs._vhosts.begin(); it5 != lhs._vhosts.end(); ++it5) {
			_vhosts[it5->first] = new IServer(*(it5->second));
		}
	}

	~IServer() {
		LocationObject::iterator loc_it = _locations.begin();
		for (; loc_it != _locations.end(); loc_it++)
			delete loc_it->second;

		VHostsObject::iterator vhost_it = _vhosts.begin();
		for (; vhost_it != _vhosts.end(); vhost_it++)
			delete vhost_it->second;

		#ifdef WEBSERV_SESSION
		destroy_sessions();
		#endif
	}

	// Name
	const std::string &	get_name() const { return _name; }
	void				set_name(const std::string &name) {
		const_cast<std::string&>(_name) = name;
	}

	// Host:Port - IP
	const std::string &	get_host() const { return _host; }
	void				set_host(const std::string &host) {
		const_cast<std::string&>(_host) = host;
	}
	const std::string &	get_ip() const { return _host; }
	int 				get_port() const { return _port; }
	void				set_port(const int &port) {
		const_cast<int&>(_port) = port;
	}

	// Location(s)
	ILocation	*new_location(const std::string &key) {
		if (_locations.find(key) != _locations.end())
			return 0;
		ILocation *location = new ILocation(
			_name, _port,
			key,
			_root,
			_body_limit,
			_error_pages);

		_locations.insert(std::pair<std::string, ILocation *>(key, location));
		return location;
	}

	// vHost(s) solver
	const IServer *get_vhost(const std::string &host) const {
		if (_vhosts.size() > 0) {
			std::string real_host = host;
			int port = -1;
			if (real_host.find(':') != std::string::npos) {
				real_host = real_host.substr(0, real_host.find(':'));
				port = atoi(host.substr(host.find(':') + 1).c_str());
			}
			VHostsObject::const_iterator it = _vhosts.find(real_host);
			if (it != _vhosts.end() && (port == -1 || port == it->second->get_port()))
				return it->second;
		}
		return this;
	}

	const IBlock
	*get_block_using_vhosts(const std::string &host,
		const std::string &uri) const {
		const IServer *vhost = get_vhost(host);
		return vhost->get_block(uri);
	}

	const IBlock *get_block(const std::string &uri) const {
		std::string real_uri = uri;
		if (real_uri.find("/", 1) != std::string::npos)
			real_uri = real_uri.substr(0, real_uri.find("/", 1));
		LocationObject::const_iterator it = _locations.find(real_uri);
		if (it != _locations.end())
			return dynamic_cast<IBlock*>(it->second);
		return dynamic_cast<IBlock*>(const_cast<IServer*>(this));
	}

	// ILocation(s) solver
	ILocation *get_location(const std::string &uri) const {
		std::string real_uri = uri;
		if (real_uri.find("/", 1) != std::string::npos)
			real_uri = real_uri.substr(0, real_uri.find("/", 1));
		LocationObject::const_iterator loc_it = _locations.find(real_uri);
		if (loc_it != _locations.end())
			return loc_it->second;
		return 0;
	}

	// Error Page(s) solver
	const std::string get_error_page(int status, const std::string &uri) const {
		ILocation *loc = get_location(uri);
		if (loc)
			return loc->get_error_page(status);
		return IBlock::get_error_page(status);
	}

	const std::string
	get_error_page(int status, const std::string &host,
		const std::string &uri) const {
		const IServer *server = get_vhost(host);
		if (server)
			return server->get_error_page(status, uri);
		return get_error_page(status, uri);
	}

	// Cookies / Sessions
	#ifdef WEBSERV_SESSION
	void	destroy_sessions() {
		Sessions::iterator it = _sessions.begin();
		for (; it != _sessions.end(); it++)
			delete it->second;
	}
	#endif

	#ifdef WEBSERV_SESSION
	void	collect_sessions() {
		time_t now = time(0);
		Sessions::iterator it = _sessions.begin();
		for (; it != _sessions.end(); it++) {
			if (!it->second->alive(now)) {
				delete it->second;
				_sessions.erase(it);
				return collect_sessions();
			}
		}
	}
	#endif

	#ifdef WEBSERV_SESSION
	Session *add_session(const std::string &sid) {
		Session *session = new Session(sid);
		std::pair<Sessions::iterator, bool> ret =
			_sessions.insert(std::pair<std::string, Session *>(sid, session));
		if (ret.second)
			return session;
		return 0;
	}
	#endif

	#ifdef WEBSERV_SESSION
	void	del_session(const std::string &sid) {
		Sessions::iterator it = _sessions.find(sid);
		if (it != _sessions.end()) {
			delete it->second;
			_sessions.erase(it);
		}
	}
	#endif

	#ifdef WEBSERV_SESSION
	Session	*get_session(const std::string &sid) {
		Sessions::iterator it = _sessions.find(sid);
		if (it != _sessions.end())
			return it->second;
		return 0;
	}
	#endif

	// Other
	IServer *clone() const {
		return new IServer(*this);
	}

	void	merge(IServer *lhs) {
		std::string lowered_name = lhs->get_name();
		_vhosts.insert(std::pair<std::string, IServer *>(\
			*_strtolower(&lowered_name), lhs->clone()));
	}

 private:
	inline std::string* _strtolower(std::string *s) {
		for (std::string::iterator it = s->begin(); it != s->end(); it++)
			*it = std::tolower(*it);
		return s;
	}
};
}  // namespace Models
}  // namespace Webserv

#endif  // MODELS_ISERVER_HPP_
