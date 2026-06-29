# include "ServerConfigClass.hpp"

ServerConfig::ServerConfig(void) : autoindex(false), clientMaxBodySize(DEFAULT_CLIENT_MAX_BODY_SIZE), redirectCode(0)
{
	index.push_back(DEFAULT_INDEX_FILE);
	methods.insert("GET");
}

void	ServerConfig::resolveLocations(void)
{
	for (std::vector<LocationConfig>::iterator it = locations.begin(); it != locations.end(); ++it)
		it->inheritFrom(*this);
	return ;
}
