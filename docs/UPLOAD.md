# `POST` et l'upload — recevoir des octets et les poser sur le disque

> Jusqu'ici le serveur **donnait** : une URL entrait, un fichier sortait (`GET`, cf. `ROUTING.md`). Le `POST` inverse le sens du fil : c'est le **client** qui envoie des données, et le serveur qui doit les comprendre, les valider, puis les **écrire**. Ce document explique tout le chemin, de la théorie du protocole jusqu'à `write()`, ligne de code par ligne de code.
>
> Le corps d'une requête (comment on sait où il finit, `Content-Length` vs `chunked`) est traité dans **`HTTP_REQUESTS.md` §8 à §10 et §17.8** : ici on part du principe que le corps est **déjà assemblé et dé-chunké** dans `_request.getBody()`. Ce qu'on raconte ici, c'est ce qui se passe **après**.
>
> Renvois : `ROUTING.md` §6bis (résumé), `SECURITY.md` §3.7 (la défense), `FRONTEND.md` §6 (ce que le navigateur produit), `CONFIG.md` (la directive `upload_store`).

---

## 1. `GET` et `POST` — la vraie différence

Ce ne sont pas « deux façons d'envoyer des paramètres ». Ce sont deux **sémantiques** :

| | `GET` | `POST` |
|---|---|---|
| Intention | « donne-moi la ressource » | « voici des données, fais-en quelque chose » |
| Corps de requête | pas de corps (ignoré s'il y en a un) | **le corps est l'essentiel** |
| Effet de bord | aucun (méthode **sûre**) | oui, assumé |
| Rejouable ? | oui (**idempotent**) | non — deux POST = deux créations |
| Mise en cache | oui | non |
| Où sont les données | dans l'**URL** (query string) | dans le **corps** |

Trois conséquences très concrètes :

1. **La taille.** Une URL est bornée (chez nous : `MAX_REQUEST_LINE`, 8192 octets → `414`). Un corps ne l'est que par `client_max_body_size`. On ne peut donc pas uploader une image dans une query string.
2. **Le binaire.** Une URL n'accepte qu'un alphabet restreint ; tout le reste doit être percent-encodé (3 octets pour 1). Un corps transporte des octets **bruts**, y compris `\0`.
3. **La discrétion relative.** Une URL finit dans les logs, l'historique du navigateur, le `Referer`. Un corps, non. (Ce n'est pas de la sécurité — sans TLS, tout est en clair sur le fil.)

C'est pourquoi le rechargement d'une page après un POST déclenche le fameux « voulez-vous renvoyer les données ? » du navigateur : rejouer un POST n'est pas anodin. Notre livre d'or CGI contourne ça avec le **Post/Redirect/Get** (cf. `CGI.md` §9bis).

---

## 2. Le trajet complet d'un `POST` dans webserv

```
  recv() ──► HttpRequest::append()          (Http/, machine à états)
               │  request line : POST accepté (sinon 501)
               │  headers : Content-Length OU Transfer-Encoding
               ├─► headersDone() ──► Client::resolveMaxBodySize()
               │                       └─ matchLocation(path) → limite de la LOCATION
               │                          → setMaxBodySize() → 413 possible ICI, sans lire le corps
               ├─► [Expect: 100-continue ?] ──► "HTTP/1.1 100 Continue" ──► on reprend en EPOLLIN
               │   corps lu par morceaux (CL) ou dé-chunké (TE) → _body
               └─► isComplete()
                     │
                     ├─► Client::tryStartCgi()   ← LE CGI PASSE AVANT (extension .py/.php/.cgi)
                     │                              corps envoyé sur stdin (cf. CGI.md §11)
                     └─► Client::genResponse() ──► Router::run()
                            matchLocation → 404 si aucune
                            redirect      → 301/302 (court-circuit)
                            méthode POST autorisée ? → sinon 405 + Allow
                            resolveFsPath  → 403 si traversal dans l'URL
                            method == "POST" ──► Router::handlePost(loc)
                                                    upload activé ? → sinon 403
                                                    Upload::handle(req, loc)
                                                       ├─ multipart/form-data → parseMultipart
                                                       └─ sinon               → handleRaw
                                                    → 201 + liste des fichiers
```

Trois choses à retenir de ce schéma :

- **Le Router ne voit jamais du `chunked`.** Le dé-chunking est fait dans le parseur : `getBody()` rend toujours le corps final, quel que soit le cadrage utilisé sur le fil. `Upload` n'a donc aucune notion de `Transfer-Encoding`.
- **Le CGI a la priorité sur l'upload.** `tryStartCgi` est appelé **avant** `genResponse` dans `Client::handleRead`. Un `POST /cgi-bin/hello.py` ne passe jamais par `handlePost` : le corps part sur `stdin` du script. C'est la config qui tranche : une location avec `cgi` fait du CGI, une location avec `upload_store` fait de l'upload.
- **La limite de taille est appliquée avant l'upload, pas pendant.** Quand `handlePost` s'exécute, le corps est déjà intégralement en mémoire et déjà validé contre `client_max_body_size`. `Upload` n'a plus aucune vérification de taille à faire.

---

## 3. Le corps est en mémoire — la conséquence à assumer

`HttpRequest::_body` est un `std::string`. Un upload de 10 Mo, c'est 10 Mo de RAM **par connexion**, du premier octet reçu jusqu'à l'envoi de la réponse.

NGINX, lui, écrit le corps dans un fichier temporaire au fil de l'eau (`client_body_temp_path`) et ne garde presque rien en mémoire. Nous, non : c'est un choix de simplicité, parfaitement acceptable pour le sujet, **à condition de savoir que la seule protection est `client_max_body_size`**. C'est cette directive qui borne la mémoire :

```
location /uploads {
    client_max_body_size 10m;     # ← ceci est une limite de RAM, pas seulement de disque
    upload_store ./www/uploads;
}
```

Pire cas théorique : `nombre de clients simultanés × client_max_body_size`. Une valeur de `1g` sur une location publique serait une invitation au déni de service par épuisement mémoire. Le refus est **anticipé** quand c'est possible : un `Content-Length` annoncé au-dessus de la limite est refusé en `413` **dès les headers, sans lire un seul octet du corps** (`HTTP_REQUESTS.md` §17.7).

---

## 4. `Expect: 100-continue` — demander la permission avant d'envoyer

Un client poli qui s'apprête à envoyer 10 Mo ne les envoie pas à l'aveugle. Il envoie d'abord **les headers seuls**, avec :

```
Expect: 100-continue
```

et il attend. Deux réponses possibles du serveur :

- `HTTP/1.1 100 Continue` → « vas-y, envoie le corps » ;
- n'importe quelle réponse finale (`413`, `403`, `405`…) → le client **n'envoie jamais le corps**. 10 Mo économisés.

C'est **curl** qui fait ça automatiquement au-delà d'environ 1 Ko de corps. Côté webserv (`Server/AroleClass/ClientClass.cpp`) :

```cpp
bool	Client::wantsContinue(void) const
{
	if (_request.getVersion() != "HTTP/1.1")
		return (false);
	return (toLowerCopy(_request.getHeader("expect")).find("100-continue") != std::string::npos);
}
```

Le `100 Continue` n'est émis **que si** les headers sont complets, que la requête n'est **pas** en erreur et **pas** encore complète. L'ordre des tests dans `handleRead` est donc décisif : `isError()` est testé **avant** `wantsContinue()`. Un corps trop gros produit le `413` et jamais le `100`.

Détail d'implémentation intéressant : le `100 Continue` est une réponse **intermédiaire**, elle ne termine pas l'échange. Le flag `_sendingContinue` sert exactement à ça — quand `handleWrite` a fini de l'envoyer, il ne ferme rien et ne réinitialise pas la requête, il **repasse en `EPOLLIN`** pour lire le corps qui arrive :

```cpp
if (_sendingContinue)
{
	_sendingContinue = false;
	this->cleanResponse();
	_ev.events = EPOLLIN;
	manager.mod(this);
	return ;
}
```

---

## 5. Les trois façons dont un client encode un corps

Le header `Content-Type` de la requête dit **comment lire les octets du corps**. Trois cas dans la vraie vie :

| `Content-Type` | Qui l'envoie | Ce que ça contient | Ce que webserv en fait |
|---|---|---|---|
| `application/x-www-form-urlencoded` | formulaire HTML par défaut | `nom=jean&ville=Paris%20XI` | traité comme **corps brut** (écrit tel quel) |
| `multipart/form-data` | formulaire avec `<input type="file">`, `curl -F` | plusieurs parties délimitées, dont des fichiers | **découpé** : chaque partie avec un `filename` est écrite |
| tout le reste (`text/plain`, `application/octet-stream`, absent…) | `curl --data-binary`, `fetch`, API | des octets bruts | écrit **en entier** dans un seul fichier |

Le tri se fait en trois lignes :

```cpp
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
```

*(Router/UploadClass.cpp)*

**Pourquoi l'urlencoded n'est pas décodé.** Décoder `nom=jean&ville=Paris` reviendrait à interpréter des **champs applicatifs** — c'est le travail d'un framework (ou d'un CGI), pas d'un serveur HTTP. Un serveur transporte ; il ne décide pas du sens des champs. Si on veut traiter un formulaire urlencoded, on l'envoie à un **CGI** : le script reçoit le corps sur `stdin` et fait ce qu'il veut (c'est exactement ce que fait `www/cgi-bin/guestbook.py`).

---

## 6. `multipart/form-data` sur le fil, octet par octet

C'est le format le plus fastidieux du POST, et le seul que le navigateur sache produire pour un fichier.

### 6.1 D'abord : deux étages, pas un seul

Avant de lire l'exemple, il faut être au clair sur **ce qui est dans les headers** et **ce qui est dans le corps**. Ce sont deux zones distinctes, séparées par une ligne vide (`HTTP_REQUESTS.md` §7). Voici la requête **entière**, avec tous les `\r\n` rendus visibles :

```
POST /uploads HTTP/1.1\r\n                                            ┐
Host: localhost:8080\r\n                                              │  LES HEADERS
Content-Type: multipart/form-data; boundary=----WebKitAbC123\r\n      │  (métadonnées)
Content-Length: 421\r\n                                               ┘
\r\n          ← LIGNE VIDE : fin des headers, tout ce qui suit est le corps
------WebKitAbC123\r\n                                                ┐
Content-Disposition: form-data; name="file"; filename="photo.png"\r\n │
Content-Type: image/png\r\n                                           │
\r\n                                                                  │  LE CORPS
<octets bruts du fichier, sans aucun encodage>                        │  (getBody())
\r\n------WebKitAbC123\r\n                                            │
Content-Disposition: form-data; name="comment"\r\n                    │
\r\n                                                                  │
un champ texte ordinaire                                              │
\r\n------WebKitAbC123--\r\n                                          ┘
```

**Le `Content-Type` est une seule ligne, dans les headers.** Tout le reste — délimiteurs, parties, fichiers — est dans le **corps**. Le header ne *contient* pas le multipart : il l'**annonce**, en donnant deux informations :

- `multipart/form-data` → « le corps est découpé en morceaux » ;
- `boundary=----WebKitAbC123` → « voici **le mot** qui sert de séparateur entre les morceaux ».

Image mentale : le client envoie un colis contenant plusieurs objets, séparés par du scotch marqué `----WebKitAbC123`. Le scotch est **dans** le colis ; l'étiquette qui dit ce qui est écrit dessus est **sur** le colis.

**Pourquoi le boundary ne peut pas être dans le corps** : problème de poule et d'œuf. Pour découper le corps, il faut connaître le séparateur — on ne peut donc pas aller le chercher dans ce qu'on ne sait pas encore découper. Le header arrive **avant** sur le fil : au moment où le premier octet du corps arrive, la règle de découpage est déjà connue. Exactement la même logique que `Content-Length`, qui annonce la longueur avant de l'envoyer.

**Attention aux deux `Content-Type`, qui n'ont rien à voir :**

| Où | Portée | Lu par notre code ? |
|---|---|---|
| dans les **headers** de la requête | décrit **tout le corps** | oui — `req.getHeader("content-type")` → `extractBoundary` |
| dans une **partie** (`Content-Type: image/png`) | décrit **ce seul fichier** | non — on ne cherche que `filename=` dans les en-têtes de partie |

Côté code, la frontière est nette — deux membres différents de `HttpRequest`, remplis par deux états différents de la machine à états :

```cpp
std::string const &	body        = req.getBody();                 // le corps entier
std::string		contentType = req.getHeader("content-type");  // UNE ligne de header
```

### 6.2 Les règles de découpage du corps

Quatre règles à graver :

1. **Le délimiteur sur le fil vaut `--` + le boundary annoncé.** Le `boundary=` du `Content-Type` ne contient **pas** ces deux tirets — la RFC impose de les préfixer dans le corps. D'où, dans le code : `delim = "--" + boundary;`.

   | Où | Ce qu'on lit |
   |---|---|
   | dans le header | `boundary=----WebKitAbC123` (4 tirets) |
   | dans le corps | `------WebKitAbC123` (6 tirets) |

   Le piège est visuel : Chrome choisit un boundary qui **commence lui-même** par des tirets, donc `--` (imposé) + `----WebKit…` (le nom) donne six tirets d'affilée. Ce ne sont pas les mêmes.
2. **Le `\r\n` qui précède un délimiteur fait partie du délimiteur**, pas des données. Si on l'oubliait, chaque fichier uploadé finirait par deux octets parasites. D'où : `sep = "\r\n" + delim;` pour chercher la **fin** d'une partie.
3. **Chaque partie a ses propres en-têtes**, terminés par une ligne vide (`\r\n\r\n`) — même structure qu'une requête HTTP, en miniature.
4. **`--` après le dernier délimiteur** signifie « fin du multipart ». C'est le seul signal de terminaison.

**Le cadrage est ici par délimiteur, pas par longueur.** C'est tout l'opposé du `chunked` (qui annonce la taille de chaque morceau). Le boundary est donc choisi **long et aléatoire** par le client, pour qu'il n'ait aucune chance d'apparaître par hasard à l'intérieur d'un fichier — si ça arrivait, le corps serait coupé au mauvais endroit. Ce n'est pas notre problème : c'est au client de choisir un boundary sûr.

---

## 7. `parseMultipart` — lecture du code, position par position

```cpp
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
```

*(Router/UploadClass.cpp)*

On cherche le **premier** délimiteur. La RFC autorise un « préambule » (du texte avant la première partie, destiné aux clients qui ne comprennent pas le multipart) : le `find` le saute naturellement. Pas de délimiteur du tout → le corps ne correspond pas au `Content-Type` annoncé → **`400`**.

Ensuite, la boucle. À chaque tour, `pos` est **juste après** un délimiteur :

```cpp
	while (true)
	{
		if (pos + 2 <= body.size() && body.compare(pos, 2, "--") == 0)
			break ;                                    // (a) délimiteur de fin
		if (pos + 2 <= body.size() && body.compare(pos, 2, "\r\n") == 0)
			pos += 2;                                  // (b) une partie commence
		else
		{
			res.errorCode = 400;                       // (c) ni l'un ni l'autre
			return (res);
		}
```

- **(a)** deux tirets après le délimiteur → c'est le terminateur, on sort ; tout ce qui suit (l'« épilogue ») est ignoré, comme le veut la RFC.
- **(b)** un `\r\n` → une nouvelle partie commence juste après.
- **(c)** autre chose → le corps est malformé. **`400`**. Ce `else` est important : sans lui, un corps tronqué ferait boucler ou lire hors bornes.

```cpp
		size_t	hdrEnd = body.find("\r\n\r\n", pos);
		if (hdrEnd == std::string::npos) { res.errorCode = 400; return (res); }
		std::string	headers = body.substr(pos, hdrEnd - pos);
		size_t		dataStart = hdrEnd + 4;
```

Les en-têtes de la partie vont de `pos` à la ligne vide. `+ 4` parce que `"\r\n\r\n"` fait quatre octets. Pas de ligne vide → partie sans fin d'en-têtes → **`400`**.

```cpp
		std::string	sep = "\r\n" + delim;
		size_t		nextPos = body.find(sep, dataStart);
		if (nextPos == std::string::npos) { res.errorCode = 400; return (res); }
		std::string	data = body.substr(dataStart, nextPos - dataStart);
```

Les **données** vont de la fin des en-têtes jusqu'au `\r\n` qui précède le délimiteur suivant. C'est ici que la règle 2 du §6.2 est appliquée : en cherchant `"\r\n" + delim` et non `delim`, le CRLF de cadrage est exclu des données. Un fichier uploadé est **bit pour bit identique** à l'original. Pas de délimiteur suivant → corps tronqué (connexion coupée en plein upload) → **`400`**.

```cpp
		std::string	rawName = extractFilename(headers);
		if (!rawName.empty())
		{
			std::string	name = sanitizeFilename(rawName);
			if (name.empty()) { res.errorCode = 400; return (res); }
			if (!writeFile(store, name, data)) { res.errorCode = 500; return (res); }
			res.files.push_back(name);
		}
		pos = nextPos + sep.size();
	}
	res.ok = true;
	return (res);
}
```

**Pas de `filename=` → la partie est ignorée.** C'est un champ de formulaire ordinaire (`name="comment"`), pas un fichier : on ne l'écrit pas. Conséquence assumée et documentée : un multipart sans aucun fichier renvoie un **`201` avec une liste vide**. C'est aussi ce qui arrive si l'utilisateur soumet le formulaire **sans choisir de fichier** (le navigateur envoie quand même une partie, avec `filename=""`).

Et `extractFilename`, volontairement minimaliste :

```cpp
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
```

On cherche littéralement `filename="` et on lit jusqu'au guillemet suivant. On ne gère ni les guillemets échappés (`\"`) ni l'encodage RFC 5987 (`filename*=UTF-8''...`) : hors sujet, et surtout **inutile** puisque le nom est de toute façon réduit à son basename juste après.

---

## 8. `extractBoundary` — les pièges du `Content-Type`

```cpp
static std::string	extractBoundary(std::string const & contentType)
{
	size_t	p = contentType.find("boundary=");

	if (p == std::string::npos)
		return ("");
	std::string	b = contentType.substr(p + 9);
	size_t		sc = b.find(';');
	if (sc != std::string::npos)
		b = b.substr(0, sc);
	while (!b.empty() && b[0] == ' ')          b.erase(0, 1);
	while (!b.empty() && b[b.size() - 1] == ' ') b.erase(b.size() - 1);
	if (b.size() >= 2 && b[0] == '"' && b[b.size() - 1] == '"')
		b = b.substr(1, b.size() - 2);
	return (b);
}
```

Chaque ligne répond à une forme réelle rencontrée sur le fil :

| Forme envoyée par un client | Traitement |
|---|---|
| `multipart/form-data; boundary=----AbC123` | cas nominal |
| `multipart/form-data; boundary=xyz; charset=utf-8` | coupé au `;` |
| `multipart/form-data; boundary= xyz ` | espaces rognés des deux côtés |
| `multipart/form-data; boundary="a;b"` | guillemets retirés — **obligatoire** si le boundary contient des caractères spéciaux |
| `multipart/form-data` (sans boundary) | chaîne vide → **`400`** dans `Upload::handle` |

Le cas des guillemets n'est pas théorique : la RFC 2046 impose de quoter un boundary contenant certains caractères. Sans le déquotage, `delim` vaudrait `--"a;b"` et **aucune** partie ne serait trouvée → `400` sur un corps pourtant valide.

---

## 9. `sanitizeFilename` — la défense, en cinq lignes

```cpp
static std::string	sanitizeFilename(std::string const & raw)
{
	size_t	slash = raw.find_last_of("/\\");
	std::string	base = (slash == std::string::npos) ? raw : raw.substr(slash + 1);

	if (base == "." || base == "..")
		return ("");
	return (base);
}
```

**Le `filename` vient du client.** Rien ne l'oblige à ressembler à un nom de fichier. Un attaquant envoie ce qu'il veut :

| `filename` envoyé | Sans défense | Avec `sanitizeFilename` |
|---|---|---|
| `../../../etc/cron.d/pwn` | écriture hors du store → **exécution de code** | `pwn` dans `upload_store` |
| `..\..\windows\system32\x` | idem sur les chemins Windows (d'où le `\\`) | `x` |
| `/etc/passwd` | écrasement d'un fichier système | `passwd` |
| `..` | `open("./www/uploads/..")` sur un dossier | rejeté → `400` |

On ne garde que le **basename** : tout ce qui suit le dernier `/` **ou** `\`. Le chemin résultant ne peut plus, structurellement, sortir de `upload_store` — il n'y a plus un seul séparateur dedans.

**C'est la même défense que l'anti-traversal du GET** (`Router::normalizeRemainder`, `ROUTING.md` §5), mais appliquée à une donnée venue du **corps** au lieu de l'**URL**. Le principe unique de `SECURITY.md` s'applique à l'identique : toute donnée qui traverse la frontière du process est hostile jusqu'à preuve du contraire.

**Défense en profondeur.** Une tentative par l'URL (`POST /uploads/../../x`) est **en plus** arrêtée en amont : `Router::run` appelle `resolveFsPath` **pour toutes les méthodes**, y compris POST, et un `..` qui remonte au-dessus du root donne `403` avant même d'atteindre `handlePost`. Deux barrières indépendantes, sur deux chemins de données différents.

---

## 10. `writeFile` — l'écriture, et pourquoi elle ne passe pas par l'epoll

```cpp
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
```

Trois points à savoir défendre en soutenance :

**a) `write` sur un fichier disque n'a pas besoin du multiplexeur.** La règle absolue du sujet (« jamais de `read`/`write` sans passer par le poll ») vise les **sockets, pipes et FIFO** — les FD qui peuvent bloquer indéfiniment parce qu'ils dépendent d'un autre process ou du réseau. Un fichier régulier est toujours « prêt » : `write` sur un disque n'attend jamais un pair. C'est la même exemption qui autorise `readFile` côté GET. **`epoll` ne sait d'ailleurs pas surveiller un fichier régulier** : il rendrait `EPERM`.

**b) La boucle d'écriture est quand même obligatoire.** `write` peut écrire **moins** que demandé (écriture partielle), même sur un fichier. Ignorer sa valeur de retour, c'est tronquer silencieusement un upload. La boucle avance `total` jusqu'à épuisement, exactement comme `handleWrite` suit `_bytes_send` pour la socket.

**c) `O_TRUNC` écrase.** Uploader deux fois `photo.png` remplace le premier. C'est un choix (celui de NGINX+PHP par défaut aussi), mais il a des conséquences — voir §13.

Les droits `0644` (lecture pour tous, écriture pour le propriétaire) sont volontaires : un fichier uploadé ne doit **jamais** naître exécutable.

---

## 11. `handleRaw` — le corps brut, nom pris dans l'URL

Quand le `Content-Type` n'est pas multipart, il n'y a ni parties ni `filename`. Le nom doit donc venir d'ailleurs : de **l'URL**.

```cpp
static Upload::Result	handleRaw(std::string const & body, LocationConfig const & loc, std::string const & urlPath)
{
	Upload::Result	res;
	std::string	remainder = urlPath.substr(loc.path.size());
	size_t		slash = remainder.find_last_of('/');
	std::string	name = (slash == std::string::npos) ? remainder : remainder.substr(slash + 1);

	name = sanitizeFilename(name);
	if (name.empty())
		name = generateName();
	...
}
```

Déroulé sur des cas réels, avec `location /uploads` :

| Requête | `remainder` | Nom retenu | Fichier écrit |
|---|---|---|---|
| `POST /uploads/notes.txt` | `/notes.txt` | `notes.txt` | `./www/uploads/notes.txt` |
| `POST /uploads/a/b/c.bin` | `/a/b/c.bin` | `c.bin` | `./www/uploads/c.bin` (pas de sous-dossier) |
| `POST /uploads` | `` (vide) | généré | `./www/uploads/upload_1754...` |
| `POST /uploads/` | `/` | généré | `./www/uploads/upload_1754...` |

Le nom de repli :

```cpp
static std::string	generateName(void)
{
	std::ostringstream	oss;

	oss << "upload_" << static_cast<long>(time(NULL));
	return (oss.str());
}
```

`std::ostringstream` parce que **`std::to_string` est du C++11** — interdit ici. C'est l'idiome C++98 pour convertir un nombre en chaîne, le même que partout ailleurs dans le projet.

> **Piège de test.** `curl -T fichier URL` envoie un **`PUT`**, pas un POST. Notre parseur n'accepte que `GET`, `POST` et `DELETE` → un `curl -T` répond **`501`**, jamais `201`. Pour tester le corps brut : `curl --data-binary @fichier http://…` (POST implicite dès qu'il y a des données).

---

## 12. `Router::handlePost` — la porte d'entrée et la réponse

```cpp
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
```

*(Router/RouterClass.cpp)*

**`uploadEnabled` est levé par la seule présence de `upload_store`** dans la location (`Config/ConfigParserDirectives.cpp`). Pas de directive → pas d'upload → `403`. C'est du refus **par défaut** : une location n'accepte d'écrire sur le disque que si la config le dit explicitement.

**Pourquoi `201 Created` et pas `200 OK` ?** `201` est le code exact de la RFC 7231 quand la requête **a créé une ou plusieurs ressources**. `200` dirait « voici le résultat de ton action », `201` dit « ça a créé quelque chose ». C'est précisément le genre de détail sur lequel se joue le point « codes de statut HTTP exacts » du sujet.

**Le contrat `Upload::Result`** est volontairement pauvre — trois champs, aucune allocation, copiable :

```cpp
struct Result {
	bool				ok;
	int				errorCode;
	std::vector<std::string>	files;
};
```

`ok == false` **et** `errorCode` porte le code à renvoyer. Le Router n'a pas à deviner pourquoi ça a raté : il fait `makeError(result.errorCode)`, qui sert au passage la page d'erreur custom (`error_page`) si la config en déclare une. `Upload` décide du **code**, le Router décide de la **présentation**. Séparation nette.

---

## 13. Le tableau des codes — qui répond quoi, et pourquoi

| Code | Émis par | Cause |
|---|---|---|
| `100` | `Client::handleRead` | `Expect: 100-continue` accepté (§4) |
| `201` | `Router::handlePost` | fichiers écrits — corps = leur liste |
| `400` | `HttpRequest` | `Content-Length` non numérique, `CL` + `TE` ensemble, chunk invalide… |
| `400` | `Upload::parseMultipart` | boundary absent du `Content-Type`, corps sans délimiteur, partie tronquée, `filename` valant `.`/`..` |
| `403` | `Router::run` | traversal dans l'URL (`resolveFsPath`) |
| `403` | `Router::handlePost` | location sans `upload_store` |
| `405` | `Router::run` | `POST` absent de la liste `methods` de la location (+ header `Allow`) |
| `413` | `HttpRequest` | corps > `client_max_body_size` de la **location** |
| `500` | `Upload` (`writeFile`) | `open`/`write` a échoué : dossier absent, droits, disque plein |
| `501` | `HttpRequest` | méthode hors {GET, POST, DELETE} — c'est le cas de `PUT` |

Deux distinctions que l'évaluateur peut tester :

- **`403` vs `405`.** `POST /` sur la config d'exemple donne `405` (la location `/` ne déclare que `GET`), **pas** `403` : le contrôle de méthode passe **avant** `handlePost`. Le `403` « upload non activé » ne concerne qu'une location qui **autorise** POST mais n'a pas de `upload_store`.
- **`400` vs `500`.** `400` = le client a mal formé sa requête. `500` = la requête était valide, c'est **nous** qui n'avons pas réussi à écrire. Ne jamais renvoyer `400` pour un disque plein : ce serait accuser le client d'une faute de serveur.

---

## 14. Les limites connues (à savoir énoncer avant qu'on te les trouve)

1. **Tout le corps en mémoire** (§3). Pas de streaming vers le disque. Borné par `client_max_body_size`.
2. **`O_TRUNC` écrase sans prévenir.** Deux uploads du même nom, le second gagne. Il n'y a ni suffixe automatique, ni `409 Conflict`. Corollaire à surveiller : si `upload_store` pointe vers un dossier **servi** par une location, un client peut remplacer un fichier du site (`hello.txt`, voire `index.html` si le store était la racine).
3. **Ne jamais faire pointer `upload_store` dans une location qui déclare `cgi`.** Sinon un client uploade `x.py` puis le `GET`, et le serveur l'**exécute** : exécution de code arbitraire. Dans `configs/default.conf` c'est bien séparé — `/uploads` n'a **aucune** directive `cgi`, donc un `.py` uploadé y est servi comme un fichier texte, jamais lancé. C'est une propriété de la **config**, pas du code : à vérifier à chaque nouvelle config.
4. **Collision de `generateName`** : deux uploads sans nom dans la même seconde produisent le même `upload_<timestamp>` — le second écrase le premier.
5. **Pas de sous-dossiers.** `POST /uploads/a/b/c.bin` écrit `c.bin` à plat (conséquence directe du basename). Voulu : créer des dossiers depuis une donnée client ouvre une autre surface d'attaque.
6. **`Content-Type` d'une partie ignoré.** Le `Content-Type: image/png` interne au multipart n'est ni stocké ni vérifié ; à la relecture, c'est `MimeTypes::forPath` qui déduit le type de l'**extension**. Un fichier annoncé `image/png` mais nommé `.txt` ressortira en `text/plain`.
7. **Pas de `Location:` dans la réponse `201`.** La RFC recommande d'indiquer l'URL de la ressource créée. On renvoie la liste des noms dans le corps à la place. Améliorable, non bloquant.
8. **Multipart imbriqué (`multipart/mixed`) non géré.** Aucun navigateur actuel n'en produit.

---

## 15. Banc d'essai

> Serveur lancé avec `configs/default.conf` depuis la racine du repo. Location cible : `/uploads` (`methods GET POST DELETE`, `upload_store ./www/uploads`, `client_max_body_size 10m`).

### 15.1 Multipart — le chemin navigateur

```sh
echo "coucou webserv" > /tmp/up.txt
curl -v -F "file=@/tmp/up.txt" http://localhost:8080/uploads
# -> 201 Created, corps "Created:\nup.txt"
ls -l www/uploads/up.txt
cat www/uploads/up.txt          # "coucou webserv" -- octet pour octet, sans CRLF parasite
```

Vérifier dans la trace `-v` : `Content-Type: multipart/form-data; boundary=------------------------XXXX` côté requête.

### 15.2 Multipart avec plusieurs fichiers et un champ texte

```sh
curl -v -F "a=@/tmp/up.txt" -F "b=@/tmp/up.txt;filename=second.txt" -F "comment=salut" \
     http://localhost:8080/uploads
# -> 201, corps listant up.txt ET second.txt ; le champ "comment" est ignore (pas de filename)
```

### 15.3 Corps brut — nom pris dans l'URL

```sh
curl -v --data-binary @/tmp/up.txt http://localhost:8080/uploads/brut.txt
ls -l www/uploads/brut.txt
# ATTENTION : curl -T enverrait un PUT -> 501 (cf. §11)
curl -v -T /tmp/up.txt http://localhost:8080/uploads/nope.txt   # 501, pas 201
```

### 15.4 Sans nom dans l'URL — nom généré

```sh
curl -v --data-binary @/tmp/up.txt http://localhost:8080/uploads
ls -lt www/uploads | head -3    # un fichier upload_<timestamp>
```

### 15.5 Relire ce qu'on vient d'uploader (la boucle complète)

```sh
curl -v http://localhost:8080/uploads/up.txt    # 200, text/plain, "coucou webserv"
curl -v http://localhost:8080/uploads/          # 200, autoindex liste tout
curl -v -X DELETE http://localhost:8080/uploads/up.txt   # 204
```

### 15.6 Sécurité — traversal par le `filename`

```sh
curl -v -F 'file=@/tmp/up.txt;filename=../../pwned.txt' http://localhost:8080/uploads
ls -l www/uploads/pwned.txt     # ECRIT ICI (basename garde)
ls -l pwned.txt                 # ne doit PAS exister a la racine du repo
ls -l www/pwned.txt             # ne doit PAS exister non plus
```

### 15.7 Sécurité — traversal par l'URL (l'autre barrière)

```sh
curl -v --path-as-is --data-binary @/tmp/up.txt 'http://localhost:8080/uploads/../../pwn.txt'
# -> 403 (resolveFsPath), on n'atteint meme pas handlePost
```

### 15.8 Multipart malformé → `400`

```sh
# a) Content-Type multipart sans boundary
curl -v -H 'Content-Type: multipart/form-data' --data-binary 'nimporte quoi' \
     http://localhost:8080/uploads          # 400

# b) boundary annonce mais absent du corps
curl -v -H 'Content-Type: multipart/form-data; boundary=ZZZ' --data-binary 'pas de delimiteur' \
     http://localhost:8080/uploads          # 400

# c) partie tronquee (pas de delimiteur final)
printf -- '--ZZZ\r\nContent-Disposition: form-data; name="f"; filename="x.txt"\r\n\r\nabc' > /tmp/trunc
curl -v -H 'Content-Type: multipart/form-data; boundary=ZZZ' --data-binary @/tmp/trunc \
     http://localhost:8080/uploads          # 400
```

### 15.9 Les codes du chemin POST

```sh
# 405 : POST interdit sur la location / (GET seulement) + header Allow
curl -sD - -o /dev/null -X POST http://localhost:8080/ | grep -iE 'HTTP/|allow'

# 413 : corps au-dessus de la limite de la location (10m sur /uploads)
head -c 20000000 /dev/zero > /tmp/big.bin
curl -v --data-binary @/tmp/big.bin http://localhost:8080/uploads/big.bin   # 413

# 500 : rendre le store non inscriptible, puis reessayer
chmod 500 www/uploads && curl -v -F "file=@/tmp/up.txt" http://localhost:8080/uploads   # 500
chmod 755 www/uploads
```

### 15.10 `Expect: 100-continue` — voir les deux réponses

```sh
head -c 2000 /dev/zero > /tmp/mid.bin
curl -v -F "file=@/tmp/mid.bin" http://localhost:8080/uploads
# dans la trace : "< HTTP/1.1 100 Continue" PUIS "< HTTP/1.1 201 Created"

# refus anticipe : au-dessus de la limite, le 413 arrive SANS 100 Continue
curl -v -F "file=@/tmp/big.bin" http://localhost:8080/uploads
```

### 15.11 Corps `chunked` — le Router n'en sait rien

```sh
curl -v -H 'Transfer-Encoding: chunked' --data-binary @/tmp/up.txt \
     http://localhost:8080/uploads/chunky.txt
cat www/uploads/chunky.txt      # contenu dechunke, identique a /tmp/up.txt
```

C'est la preuve que le dé-chunking du parseur est transparent pour l'upload.

### 15.12 Depuis un navigateur

Ouvrir `http://localhost:8080/` : la section **Upload** du tableau de bord contient les deux formulaires (`multipart/form-data` et `text/plain`). C'est le seul moyen de tester l'encodage **réel** d'un navigateur, que `curl` ne reproduit qu'approximativement (boundary différent, ordre des en-têtes de partie différent). Détail de ce que chaque `enctype` produit sur le fil : `FRONTEND.md` §6.

---

## 16. Ce qu'il faut savoir répondre

| Question | Réponse courte |
|---|---|
| « Où finit le corps ? » | `Content-Length` ou `chunked` — décidé par le **parseur**, jamais par `Upload` (`HTTP_REQUESTS.md` §8) |
| « Et si le client ment sur `Content-Length` ? » | trop court → timeout ; trop long → on ne lit que ce qui est annoncé, le reste est jeté au reset keep-alive |
| « Pourquoi `write` sans passer par epoll ? » | fichier régulier : jamais bloquant, exempté par le sujet ; `epoll` refuserait de le surveiller (§10) |
| « Comment on empêche l'écriture hors du dossier ? » | `sanitizeFilename` = basename seul, **plus** `resolveFsPath` sur l'URL — deux barrières (§9) |
| « Pourquoi `201` et pas `200` ? » | RFC 7231 : la requête a **créé** une ressource (§12) |
| « Que se passe-t-il si on uploade 1 Go ? » | `413` dès les headers si `Content-Length` l'annonce ; sinon coupé à la limite pendant la lecture |
| « Le multipart est-il streamé ? » | non, corps entier en mémoire — borné par `client_max_body_size` (§3) |
| « Un `.py` uploadé peut-il être exécuté ? » | pas avec cette config : `/uploads` ne déclare aucune extension `cgi` (§14.3) |

---

## 17. Renvois

- **`HTTP_REQUESTS.md`** §8 à §10 (cadrage du corps, `Content-Type`), §17.7 à §17.9 (le carrefour, le corps, `setMaxBodySize`) — tout ce qui se passe **avant** l'upload.
- **`ROUTING.md`** §2 (l'ordre de `run()`), §5 (anti-traversal partagé), §6bis (résumé de l'upload).
- **`SECURITY.md`** §3.7 (confinement dans `upload_store`), §2.9-2.10 (limites de taille).
- **`CONFIG.md`** (`upload_store`, `client_max_body_size` et l'héritage server → location).
- **`FRONTEND.md`** §6 (les trois `enctype` d'un formulaire et leur rendu exact sur le fil).
- **`CGI.md`** §11 (l'autre destination possible d'un corps POST : `stdin` d'un script).
- RFC 7578 (`multipart/form-data`), RFC 7231 §4.3.3 (`POST`) et §6.3.2 (`201 Created`).
