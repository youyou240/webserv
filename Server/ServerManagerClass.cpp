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

void	ServerManager::openEndpoint(Server * srv, std::string const & host, uint16_t port)
{
	std::string	key = makeKey(host, port);
	bool		existed = _sockets.find(key) != _sockets.end();

	try {
		Socket*	sock = getOrCreateSocket(host, port);
		sock->addServer(srv);
		if (existed)
			std::cout << key << " already open - vhost added" << std::endl;
		else
			std::cout << key << " opened" << std::endl;
	} catch (std::exception & e) {
		std::cout << e.what() << std::endl;
	}
}

void	ServerManager::closeSocket(std::string const & key)
{
	std::map<std::string, Socket*>::iterator it = _sockets.find(key);

	if (it == _sockets.end())
	{
		std::cout << key << " is not open" << std::endl;
		return ;
	}
	it->second->stop(_epoll);
	delete it->second;
	_sockets.erase(it);
	std::cout << key << " stopped" << std::endl;
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

int	ServerManager::serverIndex(Server const * srv) const
{
	for (size_t i = 0; i < _servers.size(); ++i)
		if (_servers[i] == srv)
			return (static_cast<int>(i) + 1);
	return (0);
}

void	ServerManager::show(void)
{
	std::cout << "=== Initial servers (blocs config) ===" << std::endl;
	for (size_t i = 0; i < _servers.size(); ++i)
		std::cout << "  Server " << i + 1 << " " << *_servers[i] << std::endl;
	std::cout << "=== Sockets Open (endpoints) ===" << std::endl;
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
			std::cout << (v ? ", " : " ") << "Server " << serverIndex(vhosts[v]) << " (" << (c.serverNames.empty() ? "(default)" : c.serverNames[0]) << ")";
		}
		std::cout << std::endl;
	}
	std::cout << std::endl;
}

void	ServerManager::startMenu(void)
{
	if (_servers.empty())
	{
		std::cout << "No server configured" << std::endl;
		return ;
	}
	std::cout << "=== Choose a server ===" << std::endl;
	for (size_t i = 0; i < _servers.size(); ++i)
	{
		ServerConfig const &	c = _servers[i]->getConfig();
		std::cout << "  " << i + 1 << ") " << (c.serverNames.empty() ? "(default)" : c.serverNames[0]) << " listen:";
		for (size_t j = 0; j < c.listen.size(); ++j)
			std::cout << (j ? ", " : " ") << makeKey(c.listen[j].first, c.listen[j].second);
		std::cout << std::endl;
	}
	std::cout << "Enter server number" << std::endl;
	int	sel = safeInputClass::safeInputNumber();
	if (sel < 1 || sel > static_cast<int>(_servers.size()))
		return ;

	Server*			srv = _servers[sel - 1];
	ServerConfig const &	c = srv->getConfig();

	std::cout << "=== Choose an endpoint ===" << std::endl;
	for (size_t j = 0; j < c.listen.size(); ++j)
		std::cout << "  " << j + 1 << ") " << makeKey(c.listen[j].first, c.listen[j].second) << std::endl;
	std::cout << "  " << c.listen.size() + 1 << ") new port (0.0.0.0)" << std::endl;
	std::cout << "Enter endpoint number" << std::endl;
	int	ep = safeInputClass::safeInputNumber();
	if (ep < 1 || ep > static_cast<int>(c.listen.size()) + 1)
		return ;

	if (ep == static_cast<int>(c.listen.size()) + 1)
	{
		std::cout << "Enter the port you want to open" << std::endl;
		int	port = safeInputClass::safeInputNumber();
		if (port < 0 || port > 65535)
			return ;
		openEndpoint(srv, "", static_cast<uint16_t>(port));
	}
	else
		openEndpoint(srv, c.listen[ep - 1].first, c.listen[ep - 1].second);
}

void	ServerManager::stopMenu(void)
{
	if (_sockets.empty())
	{
		std::cout << "No open endpoint" << std::endl;
		return ;
	}
	std::vector<std::string>	keys;

	std::cout << "=== Choose an endpoint ===" << std::endl;
	for (std::map<std::string, Socket*>::iterator it = _sockets.begin(); it != _sockets.end(); ++it)
	{
		keys.push_back(it->first);
		std::cout << "  " << keys.size() << ") " << it->first << std::endl;
	}
	std::cout << "Enter endpoint number" << std::endl;
	int	sel = safeInputClass::safeInputNumber();
	if (sel < 1 || sel > static_cast<int>(keys.size()))
		return ;

	std::string	key = keys[sel - 1];
	std::cout << "=== " << key << " ===" << std::endl;
	std::cout << "  1) close the whole port" << std::endl;
	std::cout << "  2) remove a virtual host" << std::endl;
	std::cout << "Enter action number" << std::endl;
	int	act = safeInputClass::safeInputNumber();
	if (act == 1)
		closeSocket(key);
	else if (act == 2)
		removeVhost(key);
}

void	ServerManager::removeVhost(std::string const & key)
{
	std::map<std::string, Socket*>::iterator it = _sockets.find(key);

	if (it == _sockets.end())
		return ;
	std::vector<const Server*> const &	vhosts = it->second->getAcceptor().getServers();
	if (vhosts.empty())
	{
		std::cout << key << " has no virtual host" << std::endl;
		return ;
	}
	std::cout << "=== Virtual hosts on " << key << " ===" << std::endl;
	for (size_t v = 0; v < vhosts.size(); ++v)
	{
		ServerConfig const &	c = vhosts[v]->getConfig();
		std::cout << "  " << v + 1 << ") Server " << serverIndex(vhosts[v]) << " (" << (c.serverNames.empty() ? "(default)" : c.serverNames[0]) << ")" << std::endl;
	}
	std::cout << "Enter vhost number" << std::endl;
	int	sel = safeInputClass::safeInputNumber();
	if (sel < 1 || sel > static_cast<int>(vhosts.size()))
		return ;

	const Server*	target = vhosts[sel - 1];
	it->second->removeServer(target);
	std::cout << "Server " << serverIndex(target) << " removed from " << key << std::endl;
	if (it->second->getAcceptor().getServers().empty())
		std::cout << "warning: " << key << " now has no virtual host (still listening) - STOP > 1 to close it" << std::endl;
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
			startMenu();
		else if (event == "STOP")
			stopMenu();
		else if (event == "exit")
			isRunning = 1;
	} while (!isRunning);
	std::cout << "Le serveur reprend !" << std::endl;
}
