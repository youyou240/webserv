# pragma once

# include <map>
# include <string>
# include <vector>
# include <ostream>
# include <exception>
# include <csignal>
# include <cstdlib>
# include <cstddef>
# include <stdint.h>

# include "../Config/ServerConfigClass.hpp"

class Epoll;
class Socket;

class Server {

	std::map<std::string, Socket *>		_sockets;
	std::vector<ServerConfig>		_configs;
	Epoll&					_epoll;

	Server(Server const &);
	Server& operator=(Server const &);

	void		sigManager(void);

public:
	Server(std::vector<ServerConfig> const & configs, Epoll & epoll);
	~Server(void);

	void		start(unsigned short int);
	void		stop(unsigned short int);

	int		alreadyUsePort(unsigned short int);

	size_t		getOpenSocket(void) const;
	int		getIsActive(void) const;

	void		allSocketDebug(void) const;
};

std::ostream &	operator<<(std::ostream &, Server const &);
