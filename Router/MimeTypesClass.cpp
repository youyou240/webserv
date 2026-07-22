# include "MimeTypesClass.hpp"

# define DEFAULT_MIME "application/octet-stream"

static std::string	extractExtension(std::string const & path)
{
	size_t	slash = path.find_last_of('/');
	size_t	dot = path.find_last_of('.');

	if (dot == std::string::npos)
		return ("");
	if (slash != std::string::npos && dot < slash)
		return ("");
	std::string	ext = path.substr(dot + 1);
	for (size_t i = 0; i < ext.size(); ++i)
		if (ext[i] >= 'A' && ext[i] <= 'Z')
			ext[i] = ext[i] - 'A' + 'a';
	return (ext);
}

std::string	MimeTypes::forPath(std::string const & path)
{
	std::string	ext = extractExtension(path);

	if (ext == "html" || ext == "htm")
		return ("text/html");
	if (ext == "css")
		return ("text/css");
	if (ext == "js")
		return ("application/javascript");
	if (ext == "json")
		return ("application/json");
	if (ext == "txt")
		return ("text/plain");
	if (ext == "xml")
		return ("application/xml");
	if (ext == "png")
		return ("image/png");
	if (ext == "jpg" || ext == "jpeg")
		return ("image/jpeg");
	if (ext == "gif")
		return ("image/gif");
	if (ext == "svg")
		return ("image/svg+xml");
	if (ext == "ico")
		return ("image/x-icon");
	if (ext == "pdf")
		return ("application/pdf");
	return (DEFAULT_MIME);
}
