# pragma once

# include <string>
# include <vector>
# include <exception>
# include <ostream>
# include <cstddef>
# include <stdint.h>

# include "ServerConfigClass.hpp"

enum TokenType { T_WORD, T_LBRACE, T_RBRACE, T_SEMI };

struct Token {
	TokenType	type;
	std::string	value;
	size_t		line;
};

class ConfigParser {

	std::string			_path;
	std::vector<Token>		_tokens;
	size_t				_pos;
	std::vector<ServerConfig>	_servers;

	ConfigParser(void);
	ConfigParser(ConfigParser const &);
	ConfigParser&	operator=(ConfigParser const &);

	std::string	readFile(void) const;
	void		tokenize(std::string const & content);

	bool		isAtEnd(void) const;
	Token const &	peek(void) const;
	Token const &	advance(void);
	void		fail(std::string const & msg, size_t line) const;

	void		parseServer(void);
	void		parseLocation(ServerConfig & server);
	void		parseDirective(ServerConfig & server, LocationConfig * location);

	void		handleListen(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);
	void		handleServerName(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);
	void		handleRoot(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);
	void		handleIndex(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);
	void		handleAutoindex(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);
	void		handleErrorPage(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);
	void		handleMaxBodySize(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);
	void		handleMethods(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);
	void		handleReturn(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);
	void		handleUploadStore(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);
	void		handleCgi(ServerConfig & server, LocationConfig * location, std::vector<Token> const & args, size_t line);

	uint16_t	parsePort(std::string const & str, size_t line) const;
	size_t		parseSize(std::string const & str, size_t line) const;
	int		parseCode(std::string const & str, size_t line) const;

public:
	ConfigParser(std::string const & path);
	~ConfigParser(void);

	std::vector<ServerConfig> &		parse(void);
	std::vector<ServerConfig> const &	getServers(void) const;

	class parseError : public std::exception {
		std::string	_msg;
	public:
		parseError(std::string const & msg) throw();
		virtual ~parseError(void) throw();
		virtual const char *	what(void) const throw();
	};
};

std::ostream &	operator<<(std::ostream &, ConfigParser const &);
