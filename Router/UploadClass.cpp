# include "UploadClass.hpp"

# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
# include <ctime>
# include <sstream>

Upload::Result::Result(void) : ok(false), errorCode(0)
{
}

static std::string	sanitizeFilename(std::string const & raw)
{
	size_t	slash = raw.find_last_of("/\\");
	std::string	base = (slash == std::string::npos) ? raw : raw.substr(slash + 1);

	if (base == "." || base == "..")
		return ("");
	return (base);
}

static std::string	extractBoundary(std::string const & contentType)
{
	size_t	p = contentType.find("boundary=");

	if (p == std::string::npos)
		return ("");
	std::string	b = contentType.substr(p + 9);
	size_t		sc = b.find(';');
	if (sc != std::string::npos)
		b = b.substr(0, sc);
	while (!b.empty() && b[0] == ' ')
		b.erase(0, 1);
	while (!b.empty() && b[b.size() - 1] == ' ')
		b.erase(b.size() - 1);
	if (b.size() >= 2 && b[0] == '"' && b[b.size() - 1] == '"')
		b = b.substr(1, b.size() - 2);
	return (b);
}

static std::string	extractFilename(std::string const & headers)
{
	size_t	p = headers.find("filename=\"");

	if (p == std::string::npos)
		return ("");
	p += 10;
	size_t	end = headers.find('"', p);
	if (end == std::string::npos)
		return ("");
	return (headers.substr(p, end - p));
}

static bool	writeFile(std::string const & store, std::string const & name, std::string const & data)
{
	std::string	path = store;

	if (!path.empty() && path[path.size() - 1] != '/')
		path += "/";
	path += name;

	int	fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1)
		return (false);

	size_t	total = 0;
	while (total < data.size())
	{
		ssize_t	n = write(fd, data.data() + total, data.size() - total);
		if (n <= 0)
		{
			close(fd);
			return (false);
		}
		total += static_cast<size_t>(n);
	}
	close(fd);
	return (true);
}

static Upload::Result	parseMultipart(std::string const & body, std::string const & boundary, std::string const & store)
{
	Upload::Result	res;
	std::string	delim = "--" + boundary;
	size_t		pos = body.find(delim);

	if (pos == std::string::npos)
	{
		res.errorCode = 400;
		return (res);
	}
	pos += delim.size();
	while (true)
	{
		if (pos + 2 <= body.size() && body.compare(pos, 2, "--") == 0)
			break ;
		if (pos + 2 <= body.size() && body.compare(pos, 2, "\r\n") == 0)
			pos += 2;
		else
		{
			res.errorCode = 400;
			return (res);
		}

		size_t	hdrEnd = body.find("\r\n\r\n", pos);
		if (hdrEnd == std::string::npos)
		{
			res.errorCode = 400;
			return (res);
		}
		std::string	headers = body.substr(pos, hdrEnd - pos);
		size_t		dataStart = hdrEnd + 4;

		std::string	sep = "\r\n" + delim;
		size_t		nextPos = body.find(sep, dataStart);
		if (nextPos == std::string::npos)
		{
			res.errorCode = 400;
			return (res);
		}
		std::string	data = body.substr(dataStart, nextPos - dataStart);

		std::string	rawName = extractFilename(headers);
		if (!rawName.empty())
		{
			std::string	name = sanitizeFilename(rawName);
			if (name.empty())
			{
				res.errorCode = 400;
				return (res);
			}
			if (!writeFile(store, name, data))
			{
				res.errorCode = 500;
				return (res);
			}
			res.files.push_back(name);
		}
		pos = nextPos + sep.size();
	}
	res.ok = true;
	return (res);
}

static std::string	generateName(void)
{
	std::ostringstream	oss;

	oss << "upload_" << static_cast<long>(time(NULL));
	return (oss.str());
}

static Upload::Result	handleRaw(std::string const & body, LocationConfig const & loc, std::string const & urlPath)
{
	Upload::Result	res;
	std::string	remainder = urlPath.substr(loc.path.size());
	size_t		slash = remainder.find_last_of('/');
	std::string	name = (slash == std::string::npos) ? remainder : remainder.substr(slash + 1);

	name = sanitizeFilename(name);
	if (name.empty())
		name = generateName();

	if (!writeFile(loc.uploadStore, name, body))
	{
		res.errorCode = 500;
		return (res);
	}
	res.files.push_back(name);
	res.ok = true;
	return (res);
}

Upload::Result	Upload::handle(HttpRequest const & req, LocationConfig const & loc)
{
	std::string const &	body = req.getBody();
	std::string		contentType = req.getHeader("content-type");

	if (contentType.find("multipart/form-data") != std::string::npos)
	{
		std::string	boundary = extractBoundary(contentType);
		if (boundary.empty())
		{
			Upload::Result	res;
			res.errorCode = 400;
			return (res);
		}
		return (parseMultipart(body, boundary, loc.uploadStore));
	}
	return (handleRaw(body, loc, req.getPath()));
}
