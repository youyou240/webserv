# pragma once

# include <ostream>
# include <cstddef>

# include "../Config/ServerConfigClass.hpp"

class Server {

	ServerConfig	_config;

	Server(void);
	Server(Server const &);
	Server& operator=(Server const &);

public:
	Server(ServerConfig const & config);
	~Server(void);

	ServerConfig const &	getConfig(void) const;
};

std::ostream &	operator<<(std::ostream &, Server const &);
