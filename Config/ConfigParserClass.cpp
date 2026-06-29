# include "ConfigParserClass.hpp"
# include <fstream>
# include <sstream>

ConfigParser::ConfigParser(std::string const & path) : _path(path), _pos(0) {}

ConfigParser::~ConfigParser(void) {}

ConfigParser::parseError::parseError(std::string const & msg) throw() : _msg(msg) {}

ConfigParser::parseError::~parseError(void) throw() {}

const char *	ConfigParser::parseError::what(void) const throw() {return (_msg.c_str());}

std::vector<ServerConfig> const &	ConfigParser::getServers(void) const {return (_servers);}

std::string	ConfigParser::readFile(void) const
{
	std::ifstream	file(_path.c_str());

	if (!file.is_open())
		throw parseError("Config error: cannot open config file '" + _path + "'");
	std::stringstream	ss;
	ss << file.rdbuf();
	return (ss.str());
}

void	ConfigParser::tokenize(std::string const & content)
{
	size_t	line = 1;
	size_t	i = 0;

	while (i < content.size())
	{
		char	c = content[i];
		if (c == '\n')
		{
			line++;
			i++;
		}
		else if (c == ' ' || c == '\t' || c == '\r')
			i++;
		else if (c == '#')
		{
			while (i < content.size() && content[i] != '\n')
				i++;
		}
		else if (c == '{' || c == '}' || c == ';')
		{
			Token	tok;
			tok.type = c == '{' ? T_LBRACE : (c == '}' ? T_RBRACE : T_SEMI);
			tok.value = std::string(1, c);
			tok.line = line;
			_tokens.push_back(tok);
			i++;
		}
		else
		{
			size_t	start = i;
			while (i < content.size())
			{
				char	d = content[i];
				if (d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == '{' || d == '}' || d == ';' || d == '#')
					break;
				i++;
			}
			Token	tok;
			tok.type = T_WORD;
			tok.value = content.substr(start, i - start);
			tok.line = line;
			_tokens.push_back(tok);
		}
	}
}

bool	ConfigParser::isAtEnd(void) const {return (_pos >= _tokens.size());}

Token const &	ConfigParser::peek(void) const {return (_tokens[_pos]);}

Token const &	ConfigParser::advance(void) {return (_tokens[_pos++]);}

void	ConfigParser::fail(std::string const & msg, size_t line) const
{
	std::ostringstream	oss;

	oss << "Config error (line " << line << "): " << msg;
	throw parseError(oss.str());
}

std::vector<ServerConfig> &	ConfigParser::parse(void)
{
	tokenize(readFile());
	while (!isAtEnd())
	{
		if (peek().type != T_WORD || peek().value != "server")
			fail("expected 'server' block", peek().line);
		parseServer();
	}
	if (_servers.empty())
		throw parseError("Config error: no server block defined");
	for (std::vector<ServerConfig>::iterator it = _servers.begin(); it != _servers.end(); ++it)
		it->resolveLocations();
	return (_servers);
}

void	ConfigParser::parseServer(void)
{
	ServerConfig	server;
	size_t		startLine = advance().line;

	if (isAtEnd() || peek().type != T_LBRACE)
		fail("expected '{' after 'server'", startLine);
	advance();
	while (!isAtEnd() && peek().type != T_RBRACE)
	{
		if (peek().type == T_WORD && peek().value == "location")
			parseLocation(server);
		else
			parseDirective(server, NULL);
	}
	if (isAtEnd())
		fail("unclosed 'server' block", startLine);
	advance();
	if (server.listen.empty())
		fail("'server' block without 'listen' directive", startLine);
	_servers.push_back(server);
}

void	ConfigParser::parseLocation(ServerConfig & server)
{
	size_t		startLine = advance().line;
	LocationConfig	location;

	if (isAtEnd() || peek().type != T_WORD)
		fail("expected path after 'location'", startLine);
	if (peek().value[0] != '/')
		fail("location path must start with '/'", peek().line);
	location.path = advance().value;
	if (isAtEnd() || peek().type != T_LBRACE)
		fail("expected '{' after location path", startLine);
	advance();
	while (!isAtEnd() && peek().type != T_RBRACE)
		parseDirective(server, &location);
	if (isAtEnd())
		fail("unclosed 'location' block", startLine);
	advance();
	server.locations.push_back(location);
}

void	ConfigParser::parseDirective(ServerConfig & server, LocationConfig * location)
{
	Token			key = advance();
	std::vector<Token>	args;

	if (key.type != T_WORD)
		fail("expected directive name", key.line);
	while (!isAtEnd() && peek().type == T_WORD)
		args.push_back(advance());
	if (isAtEnd() || peek().type != T_SEMI)
		fail("expected ';' after directive '" + key.value + "'", key.line);
	advance();
	if (key.value == "listen")
		handleListen(server, location, args, key.line);
	else if (key.value == "server_name")
		handleServerName(server, location, args, key.line);
	else if (key.value == "root")
		handleRoot(server, location, args, key.line);
	else if (key.value == "index")
		handleIndex(server, location, args, key.line);
	else if (key.value == "autoindex")
		handleAutoindex(server, location, args, key.line);
	else if (key.value == "error_page")
		handleErrorPage(server, location, args, key.line);
	else if (key.value == "client_max_body_size")
		handleMaxBodySize(server, location, args, key.line);
	else if (key.value == "methods")
		handleMethods(server, location, args, key.line);
	else if (key.value == "return")
		handleReturn(server, location, args, key.line);
	else if (key.value == "upload_store")
		handleUploadStore(server, location, args, key.line);
	else if (key.value == "cgi")
		handleCgi(server, location, args, key.line);
	else
		fail("unknown directive '" + key.value + "'", key.line);
}

static void	printIndex(std::ostream & o, std::string const & pad, std::vector<std::string> const & index)
{
	o << pad << "index";
	for (size_t i = 0; i < index.size(); ++i)
		o << " " << index[i];
	o << std::endl;
}

static void	printMethods(std::ostream & o, std::string const & pad, std::set<std::string> const & methods)
{
	o << pad << "methods";
	for (std::set<std::string>::const_iterator it = methods.begin(); it != methods.end(); ++it)
		o << " " << *it;
	o << std::endl;
}

static void	printErrorPages(std::ostream & o, std::string const & pad, std::map<int, std::string> const & pages)
{
	for (std::map<int, std::string>::const_iterator it = pages.begin(); it != pages.end(); ++it)
		o << pad << "error_page " << it->first << " " << it->second << std::endl;
}

static void	printCgi(std::ostream & o, std::string const & pad, std::map<std::string, std::string> const & cgi)
{
	for (std::map<std::string, std::string>::const_iterator it = cgi.begin(); it != cgi.end(); ++it)
		o << pad << "cgi " << it->first << " " << it->second << std::endl;
}

std::ostream &	operator<<(std::ostream & o, ConfigParser const & parser)
{
	std::vector<ServerConfig> const &	servers = parser.getServers();

	for (size_t s = 0; s < servers.size(); ++s)
	{
		ServerConfig const &	sv = servers[s];
		o << "server #" << s << std::endl;
		for (size_t i = 0; i < sv.listen.size(); ++i)
			o << "  listen " << (sv.listen[i].first.empty() ? "0.0.0.0" : sv.listen[i].first) << ":" << sv.listen[i].second << std::endl;
		for (size_t i = 0; i < sv.serverNames.size(); ++i)
			o << "  server_name " << sv.serverNames[i] << std::endl;
		o << "  root " << sv.root << std::endl;
		printIndex(o, "  ", sv.index);
		o << "  autoindex " << (sv.autoindex ? "on" : "off") << std::endl;
		o << "  client_max_body_size " << sv.clientMaxBodySize << std::endl;
		printMethods(o, "  ", sv.methods);
		printErrorPages(o, "  ", sv.errorPages);
		printCgi(o, "  ", sv.cgi);
		if (sv.redirectCode)
			o << "  return " << sv.redirectCode << " " << sv.redirectTarget << std::endl;
		for (size_t l = 0; l < sv.locations.size(); ++l)
		{
			LocationConfig const &	loc = sv.locations[l];
			o << "  location " << loc.path << std::endl;
			o << "    root " << loc.root << std::endl;
			printIndex(o, "    ", loc.index);
			o << "    autoindex " << (loc.autoindex ? "on" : "off") << std::endl;
			o << "    client_max_body_size " << loc.clientMaxBodySize << std::endl;
			printMethods(o, "    ", loc.methods);
			printErrorPages(o, "    ", loc.errorPages);
			printCgi(o, "    ", loc.cgi);
			if (loc.redirectCode)
				o << "    return " << loc.redirectCode << " " << loc.redirectTarget << std::endl;
			if (loc.uploadEnabled)
				o << "    upload_store " << loc.uploadStore << std::endl;
		}
	}
	return (o);
}
