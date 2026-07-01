#include "ServerClass.hpp"
#include <iostream>

Server::Server(ServerConfig const & config) : _config(config) {}

Server::~Server(void) {}

ServerConfig const &	Server::getConfig(void) const {return (_config);}

std::ostream &	operator<<(std::ostream & o, Server const & server)
{
	ServerConfig const &	cfg = server.getConfig();

	o << "[";
	if (cfg.serverNames.empty())
		o << "(default)";
	for (size_t n = 0; n < cfg.serverNames.size(); ++n)
		o << (n ? ", " : "") << cfg.serverNames[n];
	o << "] -> ";
	for (size_t l = 0; l < cfg.listen.size(); ++l)
		o << (l ? ", " : "") << (cfg.listen[l].first.empty() ? "0.0.0.0" : cfg.listen[l].first) << ":" << cfg.listen[l].second;
	return (o);
}
