# include "RouterClass.hpp"
# include "MimeTypesClass.hpp"
# include "UploadClass.hpp"

# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
# include <dirent.h>
# include <cstdio>
# include <vector>
# include <set>

# define FILE_READ_BUF 65536

static std::string	htmlEscape(std::string const & s)
{
	std::string	out;

	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '&')
			out += "&amp;";
		else if (s[i] == '<')
			out += "&lt;";
		else if (s[i] == '>')
			out += "&gt;";
		else if (s[i] == '"')
			out += "&quot;";
		else if (s[i] == '\'')
			out += "&#39;";
		else
			out += s[i];
	}
	return (out);
}

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

LocationConfig const *	Router::matchLocation(ServerConfig const & config, std::string const & path)
{
	LocationConfig const *	best = NULL;
	size_t			bestLen = 0;

	for (size_t i = 0; i < config.locations.size(); ++i)
	{
		std::string const &	lp = config.locations[i].path;
		bool			match = false;

		if (lp == "/")
			match = true;
		else if (path == lp)
			match = true;
		else if (path.size() > lp.size() && path.compare(0, lp.size(), lp) == 0 && path[lp.size()] == '/')
			match = true;
		if (match && (best == NULL || lp.size() > bestLen))
		{
			best = &config.locations[i];
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
		if (loc.autoindex)
			return (this->autoIndex(fsPath));
		return (this->makeError(403));
	}
	if (S_ISREG(st.st_mode))
		return (this->serveFile(fsPath));
	return (this->makeError(404));
}

HttpResponse	Router::autoIndex(std::string const & fsPath) const
{
	DIR *	dir = opendir(fsPath.c_str());

	if (dir == NULL)
		return (this->makeError(403));

	std::string	urlPath = _req.getPath();
	if (urlPath.empty() || urlPath[urlPath.size() - 1] != '/')
		urlPath += "/";

	std::string	body;
	body += "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n";
	body += "<title>Index of " + htmlEscape(urlPath) + "</title>\n</head>\n<body>\n";
	body += "<h1>Index of " + htmlEscape(urlPath) + "</h1>\n<hr>\n<ul>\n";

	struct dirent *	entry;
	while ((entry = readdir(dir)) != NULL)
	{
		std::string	name = entry->d_name;
		if (name == "." || name == "..")
			continue ;

		std::string	full = fsPath;
		if (!full.empty() && full[full.size() - 1] != '/')
			full += "/";
		full += name;

		struct stat	st;
		std::string	suffix = "";
		if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
			suffix = "/";

		std::string	href = htmlEscape(urlPath + name + suffix);
		body += "<li><a href=\"" + href + "\">" + htmlEscape(name + suffix) + "</a></li>\n";
	}
	closedir(dir);

	body += "</ul>\n<hr>\n</body>\n</html>\n";

	HttpResponse	resp;
	resp.setStatus(200);
	resp.setBody(body, "text/html");
	return (resp);
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
	std::map<int, std::string>::const_iterator	it = _config.errorPages.find(code);

	if (it != _config.errorPages.end())
	{
		std::string	root = _config.root;
		if (root.size() > 1 && root[root.size() - 1] == '/')
			root.erase(root.size() - 1);

		std::string	fsPath = root;
		if (it->second.empty() || it->second[0] != '/')
			fsPath += "/";
		fsPath += it->second;

		std::string	content;
		if (readFile(fsPath, content))
		{
			HttpResponse	resp;
			resp.setStatus(code);
			resp.setBody(content, MimeTypes::forPath(fsPath));
			return (resp);
		}
	}

	HttpResponse	resp;
	resp.setStatus(code);
	resp.setBody(HttpResponse::defaultErrorPage(code), "text/html");
	return (resp);
}

HttpResponse	Router::handlePost(LocationConfig const & loc) const
{
	if (!loc.uploadEnabled)
		return (this->makeError(403));

	Upload::Result	result = Upload::handle(_req, loc);
	if (!result.ok)
		return (this->makeError(result.errorCode));

	std::string	body = "Created:\n";
	for (size_t i = 0; i < result.files.size(); ++i)
		body += result.files[i] + "\n";

	HttpResponse	resp;
	resp.setStatus(201);
	resp.setBody(body, "text/plain");
	return (resp);
}

HttpResponse	Router::handleDelete(std::string const & fsPath) const
{
	struct stat	st;

	if (stat(fsPath.c_str(), &st) == -1)
		return (this->makeError(404));
	if (S_ISDIR(st.st_mode))
		return (this->makeError(403));
	if (std::remove(fsPath.c_str()) != 0)
		return (this->makeError(403));

	HttpResponse	resp;
	resp.setStatus(204);
	return (resp);
}

HttpResponse	Router::run(void)
{
	LocationConfig const *	loc = Router::matchLocation(_config, _req.getPath());

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
	if (method == "POST")
		return (this->handlePost(*loc));
	if (method == "DELETE")
		return (this->handleDelete(fsPath));
	return (this->makeError(501));
}
