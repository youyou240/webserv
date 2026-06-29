# include "LocationConfigClass.hpp"
# include "ServerConfigClass.hpp"

LocationConfig::LocationConfig(void) : autoindex(false), clientMaxBodySize(DEFAULT_CLIENT_MAX_BODY_SIZE), redirectCode(0), uploadEnabled(false), hasRoot(false), hasIndex(false), hasAutoindex(false), hasMethods(false), hasMaxBodySize(false), hasRedirect(false)
{
}

void	LocationConfig::inheritFrom(ServerConfig const & server)
{
	if (!hasRoot)
		root = server.root;
	if (!hasIndex)
		index = server.index;
	if (!hasAutoindex)
		autoindex = server.autoindex;
	if (!hasMaxBodySize)
		clientMaxBodySize = server.clientMaxBodySize;
	if (!hasMethods)
		methods = server.methods;
	if (!hasRedirect)
	{
		redirectCode = server.redirectCode;
		redirectTarget = server.redirectTarget;
	}
	for (std::map<int, std::string>::const_iterator it = server.errorPages.begin(); it != server.errorPages.end(); ++it)
		if (errorPages.find(it->first) == errorPages.end())
			errorPages[it->first] = it->second;
	for (std::map<std::string, std::string>::const_iterator it = server.cgi.begin(); it != server.cgi.end(); ++it)
		if (cgi.find(it->first) == cgi.end())
			cgi[it->first] = it->second;
	return ;
}
