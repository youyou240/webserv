# pragma once

# include <string>
# include <vector>
# include <set>
# include <map>
# include <utility>
# include <cstddef>
# include <stdint.h>

# include "LocationConfigClass.hpp"

struct ServerConfig {

	std::vector< std::pair<std::string, uint16_t> >	listen;
	std::vector<std::string>						serverNames;
	std::string										root;
	std::vector<std::string>						index;
	bool											autoindex;
	std::map<int, std::string>						errorPages;
	size_t											clientMaxBodySize;
	std::set<std::string>							methods;
	int												redirectCode;
	std::string										redirectTarget;
	std::map<std::string, std::string>				cgi;
	std::vector<LocationConfig>						locations;

	ServerConfig(void);

	void	resolveLocations(void);
};
