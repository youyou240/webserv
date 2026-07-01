#include "ServerManagerClass.hpp"
#include "../Epoll/epollClass.hpp"
#include "ServerClass.hpp"
#include "AroleClass/AroleClass.hpp"
#include "socket/socketClass.hpp"
#include "../myzod/safeInputClass.hpp"
#include <iostream>
#include <sstream>
#include <csignal>

volatile sig_atomic_t isRunning = 1;

static std::string	makeKey(std::string const & host, uint16_t port)
{
	std::ostringstream	oss;

	oss << (host.empty() ? "0.0.0.0" : host) << ":" << port;
	return (oss.str());
}

ServerManager::ServerManager(Epoll & epoll) : _epoll(epoll), _active(1) {}

ServerManager::~ServerManager(void)
{
	for (std::map<std::string, Socket*>::iterator it = _sockets.begin(); it != _sockets.end(); ++it)
	{
		it->second->stop(_epoll);
		delete it->second;
	}
	_sockets.clear();
	for (std::vector<Server*>::iterator it = _servers.begin(); it != _servers.end(); ++it)
		delete *it;
	_servers.clear();
}

void	ServerManager::handleSigEvent(int signum)
{
	(void)signum;
	isRunning = 0;
}

void	ServerManager::addServer(Server * srv)
{
	for (std::vector<Server*>::iterator it = _servers.begin(); it != _servers.end(); ++it) {
		if (*it == srv)
			return ;
	}
	_servers.push_back(srv);
}

Socket*	ServerManager::getOrCreateSocket(std::string const & host, uint16_t port)
{
	std::string	key = makeKey(host, port);
	std::map<std::string, Socket*>::iterator it = _sockets.find(key);

	if (it != _sockets.end())
		return (it->second);
	Socket*	sock = new Socket(host, port);
	_sockets.insert(std::make_pair(key, sock));
	sock->start(_epoll);
	return (sock);
}

void	ServerManager::closeSocket(uint16_t port)
{
	std::map<std::string, Socket*>::iterator it = _sockets.find(makeKey("", port));

	if (it == _sockets.end())
	{
		std::cout << "Port " << port << " is not open" << std::endl;
		return ;
	}
	it->second->stop(_epoll);
	delete it->second;
	_sockets.erase(it);
	std::cout << "Port " << port << " stopped" << std::endl;
}

void	ServerManager::build(std::vector<ServerConfig> const & configs)
{
	for (size_t i = 0; i < configs.size(); ++i)
	{
		Server*	srv = new Server(configs[i]);
		addServer(srv);
		for (size_t j = 0; j < configs[i].listen.size(); ++j)
		{
			try {
				Socket*	sock = getOrCreateSocket(configs[i].listen[j].first, configs[i].listen[j].second);
				sock->addServer(srv);
			} catch (std::exception & e) {
				std::cout << e.what() << std::endl;
			}
		}
	}
}

void	ServerManager::run(void)
{
	while (this->_active)
	{
		if (isRunning)
		{
			int	num_events = _epoll.wait();
			std::vector<struct epoll_event> const &	events = _epoll.getEvents();

			for (int i = 0; i < num_events; ++i)
			{
				Arole *role = static_cast<Arole *>(events[i].data.ptr);
				if (role == NULL)
					continue ;
				role->handle_event(_epoll, events[i].events);
			}
			Arole::launchActiveRoutine(_epoll);
		}
		else
			sigManager();
	}
}

void	ServerManager::show(void)
{
	std::cout << "=== Servers (blocs config) ===" << std::endl;
	for (size_t i = 0; i < _servers.size(); ++i)
		std::cout << "  Server " << i + 1 << " " << *_servers[i] << std::endl;
	std::cout << "=== Sockets (endpoints) ===" << std::endl;
	for (std::map<std::string, Socket*>::iterator it = _sockets.begin(); it != _sockets.end(); ++it)
	{
		Acceptor const &			acc = it->second->getAcceptor();
		std::vector<const Server*> const &	vhosts = acc.getServers();

		std::cout << "  " << it->first << " (fd " << acc.getFD() << ") clients=" << acc.getNbClient() << " vhosts:";
		if (vhosts.empty())
			std::cout << " (aucun)";
		for (size_t v = 0; v < vhosts.size(); ++v)
		{
			ServerConfig const &	c = vhosts[v]->getConfig();
			std::cout << " " << (c.serverNames.empty() ? "(default)" : c.serverNames[0]);
		}
		std::cout << std::endl;
	}
	std::cout << std::endl;
}

void	ServerManager::sigManager(void)
{
	do {
		std::cout << "SHOW | START | STOP | EXIT | exit" << std::endl;
		std::string event = safeInputClass::safeInput();
		if (event == "SHOW")
			show();
		else if (event == "EXIT")
		{
			this->_active = 0;
			return ;
		}
		else if (event == "START")
		{
			std::cout << "Enter the port you want to open" << std::endl;
			int port = safeInputClass::safeInputPort();
			if (port == -1)
				continue ;
			if (_sockets.find(makeKey("", port)) != _sockets.end())
				std::cout << "Port " << port << " is already open" << std::endl;
			else {
				try {
					getOrCreateSocket("", static_cast<uint16_t>(port));
					std::cout << "Port " << port << " opened" << std::endl;
				} catch (std::exception & e) {
					std::cout << e.what() << std::endl;
				}
			}
		}
		else if (event == "STOP")
		{
			std::cout << "Enter the port you want to stop" << std::endl;
			int port = safeInputClass::safeInputPort();
			if (port == -1)
				continue ;
			closeSocket(static_cast<uint16_t>(port));
		}
		else if (event == "exit")
			isRunning = 1;
	} while (!isRunning);
	std::cout << "Le serveur reprend !" << std::endl;
}
