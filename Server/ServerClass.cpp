#include "ServerClass.hpp"
#include "socket/socketClass.hpp"
#include "AroleClass/AroleClass.hpp"
#include "../Epoll/epollClass.hpp"
#include <sstream>
#include <iostream>

Server& Server::operator=(Server const & other) {(void)other; return(*this);}

static std::string	makeKey(std::string const & host, uint16_t port)
{
	std::ostringstream	oss;

	oss << (host.empty() ? "0.0.0.0" : host) << ":" << port;
	return (oss.str());
}

Server::Server(std::vector<ServerConfig> const & configs, Epoll & epoll) : _configs(configs), _epoll(epoll)
{
	for (size_t i = 0; i < _configs.size(); ++i)
	{
		for (size_t j = 0; j < _configs[i].listen.size(); ++j)
		{
			std::string	host = _configs[i].listen[j].first;
			uint16_t	port = _configs[i].listen[j].second;
			std::string	key = makeKey(host, port);
			std::map<std::string, Socket *>::iterator it = _sockets.find(key);

			if (it != _sockets.end())
			{
				it->second->addServerConfig(&_configs[i]);
				continue ;
			}
			try {
				Socket *sock = new Socket(host, port);
				sock->addServerConfig(&_configs[i]);
				_sockets.insert(std::make_pair(key, sock));
				sock->start(_epoll);
			} catch (std::exception &e) {
				std::cout << e.what() << std::endl;
			}
		}
	}
}

Server::~Server(void)
{
	for (std::map<std::string, Socket *>::iterator it = _sockets.begin(); it != _sockets.end(); ++it)
	{
		it->second->stop(_epoll);
		delete it->second;
	}
	_sockets.clear();
}

int		Server::alreadyUsePort(unsigned short int port)
{
	if (_sockets.find(makeKey("", port)) != _sockets.end())
		return (1);
	return (0);
}

size_t		Server::getOpenSocket(void) const {return (this->_sockets.size());}

void	Server::start(unsigned short int port)
{
	std::string	key = makeKey("", port);

	if (_sockets.find(key) != _sockets.end())
		return ;
	try {
		Socket *sock = new Socket("", port);
		_sockets.insert(std::make_pair(key, sock));
		sock->start(_epoll);
	} catch (std::exception &e) {
		std::cout << e.what() << std::endl;
	}
}

void	Server::stop(unsigned short int port)
{
	std::map<std::string, Socket *>::iterator it = _sockets.find(makeKey("", port));

	if (it == _sockets.end())
		return ;
	it->second->stop(_epoll);
	delete it->second;
	_sockets.erase(it);
}

void	Server::allSocketDebug(void) const
{
	for (std::map<std::string, Socket *>::const_iterator it = _sockets.begin(); it != _sockets.end(); ++it)
		std::cout << *(it->second) << "\tactive" << std::endl;
	std::cout << std::endl;
}

std::ostream &	operator<<(std::ostream & o, Server const &serv)
{
	o << serv.getOpenSocket() << " socket(s) ouvert(s) sur ce serveur" << std::endl;
	serv.allSocketDebug();
	return (o);
}
