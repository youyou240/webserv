# pragma once

# include <map>
# include <string>
# include <vector>
# include <stdint.h>

# include "../Config/ServerConfigClass.hpp"

class Epoll;
class Server;
class Socket;

class ServerManager {

	Epoll&							_epoll;
	std::vector<Server*>			_servers;
	std::map<std::string, Socket*>	_sockets;
	int								_active;

	ServerManager(void);
	ServerManager(ServerManager const &);
	ServerManager& operator=(ServerManager const &);

	Socket*	getOrCreateSocket(std::string const & host, uint16_t port);
	void	openEndpoint(Server * srv, std::string const & host, uint16_t port);
	void	closeSocket(std::string const & key);
	void	addServer(Server * srv);
	int		serverIndex(Server const * srv) const;
	void	startMenu(void);
	void	stopMenu(void);
	void	removeVhost(std::string const & key);
	void	sigManager(void);

public:
	ServerManager(Epoll & epoll);
	~ServerManager(void);

	void	build(std::vector<ServerConfig> const & configs);
	void	run(void);
	void	show(void);

	static void	handleSigEvent(int signum);
};
