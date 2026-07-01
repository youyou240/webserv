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
	void	closeSocket(uint16_t port);
	void	addServer(Server * srv);
	void	sigManager(void);

public:
	ServerManager(Epoll & epoll);
	~ServerManager(void);

	void	build(std::vector<ServerConfig> const & configs);
	void	run(void);
	void	show(void);

	static void	handleSigEvent(int signum);
};
