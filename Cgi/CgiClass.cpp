# include "CgiClass.hpp"
# include "../Epoll/epollClass.hpp"
# include "../Http/HttpResponseClass.hpp"

# include <sys/epoll.h>
# include <sys/wait.h>
# include <unistd.h>
# include <fcntl.h>
# include <signal.h>
# include <sstream>

std::vector<Cgi *>	Cgi::_active;
std::map<pid_t, time_t>	Cgi::_reaping;
std::vector<Arole *>	Cgi::_deadPipes;

static std::string	toLowerCopy(std::string const & s)
{
	std::string	out(s);

	for (size_t i = 0; i < out.size(); ++i)
		if (out[i] >= 'A' && out[i] <= 'Z')
			out[i] = out[i] - 'A' + 'a';
	return (out);
}

static void	splitScriptPath(std::string const & path, std::string & dir, std::string & file)
{
	size_t	slash = path.find_last_of('/');

	if (slash == std::string::npos)
	{
		dir = ".";
		file = path;
	}
	else
	{
		dir = path.substr(0, slash);
		file = path.substr(slash + 1);
		if (dir.empty())
			dir = "/";
	}
}

CgiOut::CgiOut(int fd, Cgi * cgi) : Arole(fd), _cgi(cgi), _dead(false)
{
	_ev.events = EPOLLIN;
	_ev.data.ptr = this;
}

CgiOut::~CgiOut(void)
{
}

void	CgiOut::markDead(void)
{
	_dead = true;
}

void	CgiOut::handle_event(Epoll & manager, uint32_t)
{
	char	buf[CGI_READ_BUF];

	if (_dead)
		return ;

	ssize_t	n = read(_fd, buf, sizeof(buf));

	if (n > 0)
	{
		_cgi->appendOutput(buf, static_cast<size_t>(n));
		return ;
	}
	if (n == 0)
	{
		_cgi->onEof(manager);
		return ;
	}
	return ;
}

CgiIn::CgiIn(int fd, Cgi * cgi, std::string const & body) : Arole(fd), _cgi(cgi), _body(body), _offset(0), _dead(false)
{
	_ev.events = EPOLLOUT;
	_ev.data.ptr = this;
}

CgiIn::~CgiIn(void)
{
}

void	CgiIn::markDead(void)
{
	_dead = true;
}

void	CgiIn::handle_event(Epoll & manager, uint32_t events)
{
	if (_dead)
		return ;
	if (events & (EPOLLERR | EPOLLHUP))
	{
		_cgi->onInputDone(manager);
		return ;
	}
	if (_offset >= _body.size())
	{
		_cgi->onInputDone(manager);
		return ;
	}

	ssize_t	n = write(_fd, _body.data() + _offset, _body.size() - _offset);
	if (n > 0)
	{
		_offset += static_cast<size_t>(n);
		if (_offset >= _body.size())
		{
			_cgi->onInputDone(manager);
			return ;
		}
	}
	return ;
}

Cgi::Cgi(Client & client, HttpRequest const & req, std::string const & scriptPath, std::string const & interpreter)
	: _client(client), _pid(-1), _out(NULL), _in(NULL), _start(0), _scriptPath(scriptPath), _interpreter(interpreter)
{
	_method = req.getMethod();
	_query = req.getQuery();
	_contentType = req.getHeader("content-type");
	_body = req.getBody();
	_headers = req.getHeaders();
}

Cgi::~Cgi(void)
{
}

std::vector<std::string>	Cgi::buildEnv(void) const
{
	std::vector<std::string>	env;
	std::ostringstream		cl;

	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_PROTOCOL=HTTP/1.1");
	env.push_back("SERVER_SOFTWARE=webserv");
	env.push_back("REDIRECT_STATUS=200");
	env.push_back("REQUEST_METHOD=" + _method);
	env.push_back("QUERY_STRING=" + _query);
	env.push_back("SCRIPT_FILENAME=" + _scriptPath);
	env.push_back("SCRIPT_NAME=" + _scriptPath);
	if (!_contentType.empty())
		env.push_back("CONTENT_TYPE=" + _contentType);
	cl << _body.size();
	env.push_back("CONTENT_LENGTH=" + cl.str());

	for (std::map<std::string, std::string>::const_iterator it = _headers.begin(); it != _headers.end(); ++it)
	{
		std::string	name = "HTTP_";
		for (size_t i = 0; i < it->first.size(); ++i)
		{
			char	c = it->first[i];
			if (c >= 'a' && c <= 'z')
				c = c - 'a' + 'A';
			else if (c == '-')
				c = '_';
			name += c;
		}
		env.push_back(name + "=" + it->second);
	}
	return (env);
}

bool	Cgi::start(Epoll & manager)
{
	int	outPipe[2];
	int	inPipe[2];

	if (pipe(outPipe) == -1)
		return (false);
	if (pipe(inPipe) == -1)
	{
		close(outPipe[0]);
		close(outPipe[1]);
		return (false);
	}

	_pid = fork();
	if (_pid == -1)
	{
		close(outPipe[0]);
		close(outPipe[1]);
		close(inPipe[0]);
		close(inPipe[1]);
		return (false);
	}
	if (_pid == 0)
	{
		dup2(inPipe[0], STDIN_FILENO);
		dup2(outPipe[1], STDOUT_FILENO);
		close(inPipe[0]);
		close(inPipe[1]);
		close(outPipe[0]);
		close(outPipe[1]);

		std::string	dir;
		std::string	file;
		splitScriptPath(_scriptPath, dir, file);
		if (chdir(dir.c_str()) == -1)
			_exit(1);

		std::vector<std::string>	envStrings = this->buildEnv();
		std::vector<char *>		envp;
		for (size_t i = 0; i < envStrings.size(); ++i)
			envp.push_back(const_cast<char *>(envStrings[i].c_str()));
		envp.push_back(NULL);

		std::vector<std::string>	argvStrings;
		std::string			execPath;
		if (_interpreter.empty())
		{
			execPath = "./" + file;
			argvStrings.push_back(execPath);
		}
		else
		{
			execPath = _interpreter;
			argvStrings.push_back(_interpreter);
			argvStrings.push_back(file);
		}
		std::vector<char *>	argv;
		for (size_t i = 0; i < argvStrings.size(); ++i)
			argv.push_back(const_cast<char *>(argvStrings[i].c_str()));
		argv.push_back(NULL);

		execve(execPath.c_str(), &argv[0], &envp[0]);
		_exit(1);
	}

	close(inPipe[0]);
	close(outPipe[1]);

	if (_body.empty())
		close(inPipe[1]);
	else
	{
		fcntl(inPipe[1], F_SETFL, O_NONBLOCK);
		fcntl(inPipe[1], F_SETFD, FD_CLOEXEC);
		_in = new CgiIn(inPipe[1], this, _body);
		manager.add(_in);
	}

	fcntl(outPipe[0], F_SETFL, O_NONBLOCK);
	fcntl(outPipe[0], F_SETFD, FD_CLOEXEC);
	_out = new CgiOut(outPipe[0], this);
	manager.add(_out);
	_start = time(NULL);
	_active.push_back(this);
	return (true);
}

void	Cgi::appendOutput(char const * data, size_t len)
{
	_output.append(data, len);
}

std::string	Cgi::buildResponse(void) const
{
	HttpResponse	resp;

	if (_output.empty())
	{
		resp.setStatus(502);
		resp.setBody(HttpResponse::defaultErrorPage(502), "text/html");
		return (resp.serialize());
	}

	size_t	sep = _output.find("\r\n\r\n");
	size_t	sepLen = 4;
	size_t	lf = _output.find("\n\n");
	if (sep == std::string::npos || (lf != std::string::npos && lf < sep))
	{
		sep = lf;
		sepLen = 2;
	}

	std::string	headerPart;
	std::string	body;
	if (sep == std::string::npos)
	{
		headerPart = "";
		body = _output;
	}
	else
	{
		headerPart = _output.substr(0, sep);
		body = _output.substr(sep + sepLen);
	}

	int		status = 200;
	std::string	contentType = "text/plain";
	std::istringstream	hs(headerPart);
	std::string		line;
	while (std::getline(hs, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		size_t	colon = line.find(':');
		if (colon == std::string::npos)
			continue ;
		std::string	key = line.substr(0, colon);
		std::string	value = line.substr(colon + 1);
		while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
			value.erase(0, 1);
		std::string	lkey = toLowerCopy(key);
		if (lkey == "status")
		{
			std::istringstream	ss(value);
			ss >> status;
			if (status < 100 || status > 599)
				status = 200;
		}
		else if (lkey == "content-type")
			contentType = value;
		else
			resp.setHeader(key, value);
	}

	resp.setStatus(status);
	resp.setBody(body, contentType);
	return (resp.serialize());
}

void	Cgi::removeActive(void)
{
	for (size_t i = 0; i < _active.size(); ++i)
		if (_active[i] == this)
		{
			_active.erase(_active.begin() + i);
			return ;
		}
}

void	Cgi::teardown(Epoll & manager)
{
	this->removeActive();
	if (_out != NULL)
	{
		manager.del(_out);
		_out->markDead();
		_deadPipes.push_back(_out);
		_out = NULL;
	}
	if (_in != NULL)
	{
		manager.del(_in);
		_in->markDead();
		_deadPipes.push_back(_in);
		_in = NULL;
	}
}

void	Cgi::onInputDone(Epoll & manager)
{
	if (_in != NULL)
	{
		manager.del(_in);
		delete _in;
		_in = NULL;
	}
}

void	Cgi::onEof(Epoll & manager)
{
	int	status = 0;

	if (waitpid(_pid, &status, WNOHANG) == 0)
		_reaping[_pid] = time(NULL);

	std::string	response = this->buildResponse();
	_client.deliverCgiResponse(response, manager);
	this->teardown(manager);
	delete this;
}

void	Cgi::timeout(Epoll & manager)
{
	HttpResponse	resp;

	kill(_pid, SIGKILL);
	_reaping[_pid] = time(NULL);
	resp.setStatus(504);
	resp.setBody(HttpResponse::defaultErrorPage(504), "text/html");
	_client.deliverCgiResponse(resp.serialize(), manager);
	this->teardown(manager);
	delete this;
}

void	Cgi::clientGone(Epoll & manager)
{
	kill(_pid, SIGKILL);
	_reaping[_pid] = time(NULL);
	this->teardown(manager);
	delete this;
}

void	Cgi::routine(Epoll & manager)
{
	time_t	now = time(NULL);

	for (size_t i = 0; i < _deadPipes.size(); ++i)
		delete _deadPipes[i];
	_deadPipes.clear();

	for (size_t i = 0; i < _active.size(); )
	{
		Cgi *	c = _active[i];
		if (now - c->_start >= CGI_TIMEOUT)
			c->timeout(manager);
		else
			++i;
	}
	for (std::map<pid_t, time_t>::iterator it = _reaping.begin(); it != _reaping.end(); )
	{
		int	status = 0;
		pid_t	r = waitpid(it->first, &status, WNOHANG);
		if (r != 0)
		{
			std::map<pid_t, time_t>::iterator	del = it++;
			_reaping.erase(del);
		}
		else
			++it;
	}
}
