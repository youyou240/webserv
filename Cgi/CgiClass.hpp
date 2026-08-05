# pragma once

# include <string>
# include <vector>
# include <map>
# include <ctime>
# include <sys/types.h>
# include <stdint.h>

# include "../Server/AroleClass/AroleClass.hpp"
# include "../Http/HttpRequestClass.hpp"

# define CGI_TIMEOUT 10
# define CGI_READ_BUF 65536

class Client;
class Epoll;
class Cgi;

class CgiOut : public Arole {

	Cgi *	_cgi;
	bool	_dead;

	CgiOut(void);
	CgiOut(CgiOut const &);
	CgiOut &	operator=(CgiOut const &);

public:

	CgiOut(int fd, Cgi * cgi);
	~CgiOut(void);

	void		markDead(void);
	virtual void	handle_event(Epoll & manager, uint32_t events);
};

class CgiIn : public Arole {

	Cgi *			_cgi;
	std::string const &	_body;
	size_t			_offset;
	bool			_dead;

	CgiIn(void);
	CgiIn(CgiIn const &);
	CgiIn &	operator=(CgiIn const &);

public:

	CgiIn(int fd, Cgi * cgi, std::string const & body);
	~CgiIn(void);

	void		markDead(void);
	virtual void	handle_event(Epoll & manager, uint32_t events);
};

class Cgi {

	Client &				_client;
	pid_t					_pid;
	CgiOut *				_out;
	CgiIn *					_in;
	std::string				_output;
	time_t					_start;

	std::string				_scriptPath;
	std::string				_interpreter;
	std::string				_method;
	std::string				_query;
	std::string				_uri;
	std::string				_scriptName;
	std::string				_version;
	std::string				_remoteAddr;
	std::string				_serverName;
	std::string				_serverPort;
	std::string				_contentType;
	std::string				_body;
	std::map<std::string, std::string>	_headers;

	static std::vector<Cgi *>		_active;
	static std::map<pid_t, time_t>		_reaping;
	static std::vector<Arole *>		_deadPipes;

	std::vector<std::string>	buildEnv(void) const;
	std::string			buildResponse(void) const;
	void				removeActive(void);
	void				teardown(Epoll & manager);
	void				timeout(Epoll & manager);

	Cgi(void);
	Cgi(Cgi const &);
	Cgi &	operator=(Cgi const &);

public:

	Cgi(Client & client, HttpRequest const & req, std::string const & scriptPath, std::string const & interpreter, std::string const & serverName);
	~Cgi(void);

	bool	start(Epoll & manager);
	void	appendOutput(char const * data, size_t len);
	void	onEof(Epoll & manager);
	void	onInputDone(Epoll & manager);
	void	clientGone(Epoll & manager);

	static void	routine(Epoll & manager);
};
