# include "ConfigParserClass.hpp"
# include <cstdlib>
# include <set>
# include <utility>

uint16_t	ConfigParser::parsePort(std::string const & str, size_t line) const
{
	if (str.empty())
		fail("invalid port (empty)", line);
	for (size_t i = 0; i < str.size(); ++i)
		if (str[i] < '0' || str[i] > '9')
			fail("invalid port '" + str + "' (not numeric)", line);
	long	value = std::atol(str.c_str());
	if (value < 1 || value > 65535)
		fail("port '" + str + "' out of range (1-65535)", line);
	return (static_cast<uint16_t>(value));
}

int	ConfigParser::parseCode(std::string const & str, size_t line) const
{
	if (str.empty())
		fail("invalid status code (empty)", line);
	for (size_t i = 0; i < str.size(); ++i)
		if (str[i] < '0' || str[i] > '9')
			fail("invalid status code '" + str + "' (not numeric)", line);
	int	value = std::atoi(str.c_str());
	if (value < 100 || value > 599)
		fail("status code '" + str + "' out of range (100-599)", line);
	return (value);
}

size_t	ConfigParser::parseSize(std::string const & str, size_t line) const
{
	char		suffix = str[str.size() - 1];
	unsigned long	multiplier = 1;
	std::string	digits = str;

	if (str.empty())
		fail("invalid size (empty)", line);
	if (suffix == 'k' || suffix == 'K')
		multiplier = 1024UL;
	else if (suffix == 'm' || suffix == 'M')
		multiplier = 1024UL * 1024UL;
	else if (suffix == 'g' || suffix == 'G')
		multiplier = 1024UL * 1024UL * 1024UL;
	if (multiplier != 1)
		digits = str.substr(0, str.size() - 1);
	if (digits.empty())
		fail("invalid size '" + str + "'", line);
	for (size_t i = 0; i < digits.size(); ++i)
		if (digits[i] < '0' || digits[i] > '9')
			fail("invalid size '" + str + "' (not numeric)", line);
	unsigned long	value = std::strtoul(digits.c_str(), NULL, 10);
	return (static_cast<size_t>(value * multiplier));
}

void	ConfigParser::handleListen(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	if (location)
		fail("'listen' is only allowed at server level", line);
	if (args.empty())
		fail("'listen' expects at least one host:port", line);
	for (size_t i = 0; i < args.size(); ++i)
	{
		std::string	arg = args[i].value;
		std::string	host = "";
		std::string	portStr = arg;
		size_t		colon = arg.rfind(':');

		if (colon != std::string::npos)
		{
			host = arg.substr(0, colon);
			portStr = arg.substr(colon + 1);
		}
		uint16_t	port = parsePort(portStr, line);
		server.listen.push_back(std::make_pair(host, port));
	}
}

void	ConfigParser::handleServerName(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	if (location)
		fail("'server_name' is only allowed at server level", line);
	if (args.empty())
		fail("'server_name' expects at least one name", line);
	for (size_t i = 0; i < args.size(); ++i)
		server.serverNames.push_back(args[i].value);
}

void	ConfigParser::handleRoot(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	if (args.size() != 1)
		fail("'root' expects exactly one path", line);
	if (location)
	{
		location->root = args[0].value;
		location->hasRoot = true;
	}
	else
		server.root = args[0].value;
}

void	ConfigParser::handleIndex(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	if (args.empty())
		fail("'index' expects at least one file", line);
	if (location)
	{
		location->index.clear();
		for (size_t i = 0; i < args.size(); ++i)
			location->index.push_back(args[i].value);
		location->hasIndex = true;
	}
	else
	{
		server.index.clear();
		for (size_t i = 0; i < args.size(); ++i)
			server.index.push_back(args[i].value);
	}
}

void	ConfigParser::handleAutoindex(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	bool	value = false;

	if (args.size() != 1)
		fail("'autoindex' expects 'on' or 'off'", line);
	if (args[0].value == "on")
		value = true;
	else if (args[0].value != "off")
		fail("'autoindex' expects 'on' or 'off'", line);
	if (location)
	{
		location->autoindex = value;
		location->hasAutoindex = true;
	}
	else
		server.autoindex = value;
}

void	ConfigParser::handleErrorPage(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	if (args.size() < 2)
		fail("'error_page' expects one or more codes and a path", line);
	std::string	path = args[args.size() - 1].value;
	for (size_t i = 0; i + 1 < args.size(); ++i)
	{
		int	code = parseCode(args[i].value, line);
		if (location)
			location->errorPages[code] = path;
		else
			server.errorPages[code] = path;
	}
}

void	ConfigParser::handleMaxBodySize(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	if (args.size() != 1)
		fail("'client_max_body_size' expects exactly one value", line);
	size_t	value = parseSize(args[0].value, line);
	if (location)
	{
		location->clientMaxBodySize = value;
		location->hasMaxBodySize = true;
	}
	else
		server.clientMaxBodySize = value;
}

void	ConfigParser::handleMethods(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	std::set<std::string>	methods;

	if (args.empty())
		fail("'methods' expects at least one method", line);
	for (size_t i = 0; i < args.size(); ++i)
	{
		std::string	m = args[i].value;
		if (m != "GET" && m != "POST" && m != "DELETE")
			fail("unsupported method '" + m + "' (GET, POST, DELETE only)", line);
		methods.insert(m);
	}
	if (location)
	{
		location->methods = methods;
		location->hasMethods = true;
	}
	else
		server.methods = methods;
}

void	ConfigParser::handleReturn(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	if (args.size() < 1 || args.size() > 2)
		fail("'return' expects a code and an optional URL", line);
	int		code = parseCode(args[0].value, line);
	std::string	target = args.size() == 2 ? args[1].value : "";
	if (location)
	{
		location->redirectCode = code;
		location->redirectTarget = target;
		location->hasRedirect = true;
	}
	else
	{
		server.redirectCode = code;
		server.redirectTarget = target;
	}
}

void	ConfigParser::handleUploadStore(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	(void)server;
	if (!location)
		fail("'upload_store' is only allowed in a location block", line);
	if (args.size() != 1)
		fail("'upload_store' expects exactly one directory", line);
	location->uploadStore = args[0].value;
	location->uploadEnabled = true;
}

void	ConfigParser::handleCgi(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line)
{
	if (args.size() != 2)
		fail("'cgi' expects an extension and an interpreter path", line);
	std::string	ext = args[0].value;
	if (ext.empty() || ext[0] != '.')
		fail("'cgi' extension must start with '.'", line);
	if (location)
		location->cgi[ext] = args[1].value;
	else
		server.cgi[ext] = args[1].value;
}
