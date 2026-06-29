# pragma once

# include <string>
# include <vector>
# include <set>
# include <map>
# include <cstddef>

# define DEFAULT_CLIENT_MAX_BODY_SIZE 1048576UL
# define DEFAULT_INDEX_FILE "index.html"

class ServerConfig;

struct LocationConfig {

	std::string				path;
	std::string				root;
	std::vector<std::string>		index;
	std::set<std::string>			methods;
	bool					autoindex;
	std::map<int, std::string>		errorPages;
	size_t					clientMaxBodySize;
	int					redirectCode;
	std::string				redirectTarget;
	bool					uploadEnabled;
	std::string				uploadStore;
	std::map<std::string, std::string>	cgi;

	bool					hasRoot;
	bool					hasIndex;
	bool					hasAutoindex;
	bool					hasMethods;
	bool					hasMaxBodySize;
	bool					hasRedirect;

	LocationConfig(void);

	void	inheritFrom(ServerConfig const & server);
};
