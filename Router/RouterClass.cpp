# include "RouterClass.hpp"
# include "MimeTypesClass.hpp"

# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
# include <vector>
# include <set>

# define FILE_READ_BUF 65536

static bool	normalizeRemainder(std::string const & in, std::string & out)
{
	std::vector<std::string>	stack;
	size_t				i = 0;

	while (i < in.size())
	{
		while (i < in.size() && in[i] == '/')
			++i;
		size_t	start = i;
		while (i < in.size() && in[i] != '/')
			++i;
		if (i == start)
			break ;
		std::string	seg = in.substr(start, i - start);
		if (seg == ".")
			continue ;
		if (seg == "..")
		{
			if (stack.empty())
				return (false);
			stack.pop_back();
		}
		else
			stack.push_back(seg);
	}
	out = "";
	for (size_t k = 0; k < stack.size(); ++k)
	{
		out += "/";
		out += stack[k];
	}
	return (true);
}

static std::string	joinMethods(std::set<std::string> const & methods)
{
	std::string	out;

	for (std::set<std::string>::const_iterator it = methods.begin(); it != methods.end(); ++it)
	{
		if (!out.empty())
			out += ", ";
		out += *it;
	}
	return (out);
}

static bool	readFile(std::string const & path, std::string & out)
{
	int	fd = open(path.c_str(), O_RDONLY);

	if (fd == -1)
		return (false);
	char	buf[FILE_READ_BUF];
	ssize_t	n;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		out.append(buf, static_cast<size_t>(n));
	close(fd);
	return (n != -1);
}

Router::Router(HttpRequest const & req, ServerConfig const & config) : _req(req), _config(config)
{
}

Router::~Router(void)
{
}

LocationConfig const *	Router::matchLocation(void) const
{
	std::string const &	path = _req.getPath();
	LocationConfig const *	best = NULL;
	size_t			bestLen = 0;

	for (size_t i = 0; i < _config.locations.size(); ++i)
	{
		std::string const &	lp = _config.locations[i].path;
		bool			match = false;

		if (lp == "/")
			match = true;
		else if (path == lp)
			match = true;
		else if (path.size() > lp.size() && path.compare(0, lp.size(), lp) == 0 && path[lp.size()] == '/')
			match = true;
		if (match && (best == NULL || lp.size() > bestLen))
		{
			best = &_config.locations[i];
			bestLen = lp.size();
		}
	}
	return (best);
}

bool	Router::resolveFsPath(LocationConfig const & loc, std::string & out) const
{
	std::string	remainder = _req.getPath().substr(loc.path.size());
	std::string	normalized;

	if (!normalizeRemainder(remainder, normalized))
		return (false);
	std::string	root = loc.root;
	if (root.size() > 1 && root[root.size() - 1] == '/')
		root.erase(root.size() - 1);
	out = root + normalized;
	if (out.empty())
		out = "/";
	return (true);
}

HttpResponse	Router::serveFile(std::string const & fsPath) const
{
	std::string	content;

	if (!readFile(fsPath, content))
		return (this->makeError(403));

	HttpResponse	resp;
	resp.setStatus(200);
	resp.setBody(content, MimeTypes::forPath(fsPath));
	return (resp);
}

HttpResponse	Router::handleGet(LocationConfig const & loc, std::string const & fsPath) const
{
	struct stat	st;

	if (stat(fsPath.c_str(), &st) == -1)
		return (this->makeError(404));
	if (S_ISDIR(st.st_mode))
	{
		for (size_t i = 0; i < loc.index.size(); ++i)
		{
			std::string	indexPath = fsPath;

			if (!indexPath.empty() && indexPath[indexPath.size() - 1] != '/')
				indexPath += "/";
			indexPath += loc.index[i];

			struct stat	ist;
			if (stat(indexPath.c_str(), &ist) == 0 && S_ISREG(ist.st_mode))
				return (this->serveFile(indexPath));
		}
		return (this->makeError(403));
	}
	if (S_ISREG(st.st_mode))
		return (this->serveFile(fsPath));
	return (this->makeError(404));
}

HttpResponse	Router::makeRedirect(LocationConfig const & loc) const
{
	HttpResponse	resp;

	resp.setStatus(loc.redirectCode);
	resp.setHeader("Location", loc.redirectTarget);
	resp.setBody("", "text/html");
	return (resp);
}

HttpResponse	Router::makeError(int code) const
{
	HttpResponse	resp;

	resp.setStatus(code);
	resp.setBody(HttpResponse::defaultErrorPage(code), "text/html");
	return (resp);
}

HttpResponse	Router::run(void)
{
	LocationConfig const *	loc = this->matchLocation();

	if (loc == NULL)
		return (this->makeError(404));
	if (loc->redirectCode != 0)
		return (this->makeRedirect(*loc));

	std::string	method = _req.getMethod();
	if (loc->methods.find(method) == loc->methods.end())
	{
		HttpResponse	resp = this->makeError(405);
		resp.setHeader("Allow", joinMethods(loc->methods));
		return (resp);
	}

	std::string	fsPath;
	if (!this->resolveFsPath(*loc, fsPath))
		return (this->makeError(403));

	if (method == "GET")
		return (this->handleGet(*loc, fsPath));
	return (this->makeError(501));
}
