# Le routing — de la requête comprise au fichier servi

> La requête est parsée (`Http/HttpRequest`, cf. `HTTP_REQUESTS.md`), le bon virtual host est choisi (`Client::selectVhost`) : il reste à **décider quoi renvoyer**. C'est le rôle du **Router** (`Router/`). Ce document explique, décision par décision, comment une URL devient une réponse.
>
> Le Router est branché à **un seul endroit** : `Client::genResponse` (cf. `ARCHITECTURE.md` acte 5). Tant que la requête n'est pas complète, on n'y arrive pas ; une fois complète, `genResponse` délègue tout au Router et sérialise le résultat.

---

## 1. Vue d'ensemble : un objet jetable par requête

```cpp
void	Client::genResponse(void)
{
	const Server *	srv = this->selectVhost();
	if (srv == NULL)
	{
		this->genErrorResponse(500);
		return ;
	}
	Router		router(_request, srv->getConfig());
	HttpResponse	resp = router.run();
	resp.setHeader("Connection", _request.isKeepAlive() ? "keep-alive" : "close");
	_response = resp.serialize();
	_bytes_send = 0;
}
```

*(Server/AroleClass/ClientClass.cpp)*

Le `Router` naît avec **deux références** — la requête et la `ServerConfig` du vhost — et meurt à la fin de `genResponse`. Il ne possède rien, ne copie rien : il **lit** la requête et la config, et **rend** un `HttpResponse` par valeur. Le header `Connection` reste géré par le `Client` (le Router ne connaît pas le keep-alive : ce n'est pas une décision de routing).

Pourquoi un objet plutôt que des fonctions libres ? Pour ne pas trimballer `(request, config)` dans chaque helper : ce sont des **membres référence**, et chaque méthode privée les lit sans les recevoir en paramètre. L'objet est volontairement non-copiable (ctor de copie et `operator=` privés).

---

## 2. `run()` — l'aiguillage, dans l'ordre qui compte

```cpp
HttpResponse	Router::run(void)
{
	LocationConfig const *	loc = this->matchLocation();
	if (loc == NULL)                       return (this->makeError(404));
	if (loc->redirectCode != 0)            return (this->makeRedirect(*loc));
	if (méthode pas dans loc->methods)     return (405 + header Allow);
	if (!this->resolveFsPath(*loc, fsPath))return (this->makeError(403));
	if (méthode == "GET")                  return (this->handleGet(*loc, fsPath));
	return (this->makeError(501));
}
```

*(Router/RouterClass.cpp)*

L'ordre n'est pas arbitraire :

1. **Pas de location** qui matche → `404` (aucune règle ne couvre ce chemin).
2. **Redirection d'abord** : une `location` avec `return 301 …` court-circuite tout le reste — comme NGINX, une redirection ne dépend ni de la méthode ni de l'existence d'un fichier.
3. **Méthode** ensuite : si elle n'est pas dans la liste autorisée de la location → `405` **avec le header `Allow`** (obligatoire : il annonce au client les méthodes acceptées).
4. **Résolution du chemin disque** : échoue si tentative de traversal (§5) → `403`.
5. **Dispatch par méthode** : `GET` est traité ; `POST`/`DELETE` (autorisés par la config mais pas encore implémentés) renvoient `501` **temporairement** — ces branches deviendront l'upload et la suppression (phases à venir).

---

## 3. Le matching de `location` — le préfixe le plus long gagne

```cpp
LocationConfig const *	Router::matchLocation(void) const
{
	// pour chaque location : "/" matche tout ; sinon égalité exacte,
	// ou path == lp + "/..." (frontière sur un '/')
	// on garde celle dont le path est le PLUS LONG
}
```

*(Router/RouterClass.cpp)*

Deux règles :

- **Frontière stricte** : `/upload` ne doit **pas** matcher `/uploads`. On exige donc soit l'égalité exacte (`path == lp`), soit que le caractère juste après le préfixe soit un `/` (`path[lp.size()] == '/'`). Ainsi `/uploads` matche `/uploads` et `/uploads/x`, mais jamais `/uploadsxyz`.
- **Le plus long l'emporte** : si `/` et `/uploads` matchent tous deux `/uploads/x`, c'est `/uploads` (5 > 1) qui est choisi. C'est la spécificité attendue : la règle la plus précise gagne.

`/` matche toujours (c'est le fourre-tout par défaut). Une config sans `location /` peut donc renvoyer `404` sur un chemin non couvert — c'est voulu.

---

## 4. Le mapping URL → disque — comportement *alias*, comme le sujet

C'est **le** point à comprendre. Le sujet donne un exemple sans ambiguïté :

> `/kapouet` rooté sur `/tmp/www` ⇒ `/kapouet/pouic/toto/pouet` cherche `/tmp/www/pouic/toto/pouet`.

Le préfixe de la location (`/kapouet`) est donc **retiré**, et ce qui reste est collé au `root`. C'est le comportement `alias` de NGINX (pas son `root`, qui lui garderait le préfixe). Notre `resolveFsPath` :

```cpp
std::string	remainder = _req.getPath().substr(loc.path.size());   // retire le préfixe
// remainder normalisé (§5), root sans slash final, puis :
out = root + normalized;
```

*(Router/RouterClass.cpp)*

Conséquence concrète, à retenir pour écrire les configs : **une location sert le contenu de son propre `root`, pas d'un sous-dossier nommé comme elle**. Exemple tiré de `configs/default.conf` :

```
location /uploads {
    root ./www/uploads;      # <-- son propre root
}
```

- `GET /uploads/hello.txt` → retire `/uploads` → `/hello.txt` → `./www/uploads/hello.txt`. ✅
- Si `/uploads` héritait du root serveur `./www` **sans** root propre, `GET /uploads/hello.txt` chercherait `./www/hello.txt` (préfixe retiré) — presque jamais ce qu'on veut. D'où le `root ./www/uploads;` explicite.

Pour `location /` : `GET /about.html` → retire `/` → `about.html` → `./www/about.html`. `GET /` → remainder vide → `./www` (un dossier, cf. §6).

---

## 5. Normalisation & défense anti-traversal (purement lexicale)

Le parseur a déjà percent-décodé le path et rejeté les caractères de contrôle — mais il n'a **pas** résolu les `..`. Un chemin comme `/uploads/../../etc/passwd` matcherait `/uploads` et pourrait, naïvement, sortir du root. `normalizeRemainder` l'en empêche **avant** tout accès disque :

```cpp
// découpe le remainder sur les '/', et pour chaque segment :
//   "."  -> ignoré
//   ".." -> dépile un segment ; si la pile est VIDE -> return false (évasion)
//   sinon -> empile
```

*(Router/RouterClass.cpp)*

Un `..` qui tente de remonter au-dessus de la racine de la location vide la pile → `false` → `resolveFsPath` échoue → `403`. C'est une vérification **lexicale** (sur la chaîne), pas un `realpath` : `realpath` n'est pas dans la liste des fonctions autorisées, et une résolution purement textuelle est à la fois suffisante et sûre (on ne dépend pas de l'état réel du disque ni des liens symboliques pour cette garantie).

> À tester : `GET /uploads/../../etc/passwd`, `GET /a/b/../../../x` → doivent tous renvoyer `403`, jamais un fichier hors root.

---

## 6. `GET` statique — dossier, index, fichier

```cpp
HttpResponse	Router::handleGet(LocationConfig const & loc, std::string const & fsPath) const
{
	struct stat	st;
	if (stat(fsPath.c_str(), &st) == -1)   return (this->makeError(404));
	if (S_ISDIR(st.st_mode))
	{
		// pour chaque fichier index de la location : si <dir>/<index> est un
		// fichier régulier -> le servir ;
		// aucun trouvé -> makeError(403)   (l'autoindex se branchera ici)
	}
	if (S_ISREG(st.st_mode))               return (this->serveFile(fsPath));
	return (this->makeError(404));         // ni dossier ni fichier régulier
}
```

*(Router/RouterClass.cpp)*

- **`stat` échoue** → `404` (la ressource n'existe pas).
- **Dossier** → on essaie les fichiers `index` de la config, dans l'ordre (`index index.html index.htm`). Le premier qui est un vrai fichier est servi. Aucun ? → `403` **pour l'instant** : c'est exactement le point où l'**autoindex** (directory listing) viendra se brancher (`autoindex on` → générer la page ; `off` → `403`).
- **Fichier régulier** → servi.
- **Autre chose** (device, socket…) → `404`.

Le service lui-même :

```cpp
HttpResponse	Router::serveFile(std::string const & fsPath) const
{
	std::string	content;
	if (!readFile(fsPath, content))   return (this->makeError(403));
	// 200 + body + Content-Type = MimeTypes::forPath(fsPath)
}
```

`readFile` fait `open`/`read` en boucle/`close` — **lecture d'un fichier disque régulier, exemptée du poll** (cf. CLAUDE.md §0). On teste la **valeur de retour** de `read` (jamais `errno`), et le `close` est local à la lecture (ce n'est pas un FD géré par `Arole` — la règle « un seul `close` dans `~Arole` » ne concerne que les sockets/pipes du reactor). Si `open` échoue (droits) → `403`.

Le `Content-Type` vient de `MimeTypes::forPath` (`Router/MimeTypes`), une petite table extension → type (`.html`→`text/html`, `.css`→`text/css`, `.png`→`image/png`…), défaut `application/octet-stream`.

---

## 7. Redirections & pages d'erreur

**Redirection** (`makeRedirect`) : `status = redirectCode` (301/302), header `Location: redirectTarget`, body vide. Court-circuite tout (cf. §2). Exemple : `location /old { return 301 http://localhost:8080/; }`.

**Erreurs** (`makeError`) : aujourd'hui, page HTML **par défaut** via `HttpResponse::defaultErrorPage(code)` (style NGINX). `makeError` est une **méthode** (elle a accès à `_config`) précisément pour que le branchement des `error_page` **custom** (lire le fichier déclaré en config, retomber sur la page par défaut sinon) se fasse à **un seul endroit**, sans toucher au reste.

---

## 8. Ce qui est encore en attente (et où ça se branche)

| Feature | État | Point d'insertion |
|---|---|---|
| Directory listing (autoindex) | ⏳ `403` sur dossier sans index | branche « aucun index trouvé » de `handleGet` |
| Pages d'erreur custom (`error_page`) | ⏳ page par défaut | `Router::makeError` (accès à `_config.errorPages`) |
| POST / upload | ⏳ `501` | branche non-GET de `run()` |
| DELETE | ⏳ `501` | branche non-GET de `run()` |
| CGI (`.py`…) | ⏳ fichier servi tel quel | avant `handleGet`, sur détection d'extension `cgi` |
| `client_max_body_size` par location | ⏳ limite du **server** | la limite vient de `selectVhost`, à raffiner avec la location matchée |

Chaque case cochée de la roadmap (CLAUDE.md §8) remplace une de ces branches — la structure de `run()` est faite pour ça.

---

## 9. Banc d'essai complet du routing

> Serveur lancé avec `configs/default.conf`, **depuis la racine du repo** (pour que `./www` se résolve). Rappel de la topologie utile : `localhost:8080` (root `./www`, `location /` GET+autoindex off, `/uploads` root `./www/uploads` GET/POST/DELETE+autoindex on, `/cgi-bin` GET/POST, `/old` redirect 301), `site-b.local:8080` (root `./www-b`), `9090` (root `./www`, autoindex on).
>
> Astuce : `curl -s -o /dev/null -w '%{http_code}\n' URL` n'affiche que le code. Pour voir les **headers d'un GET** sans le corps : `curl -s -D - -o /dev/null URL` (dump headers + jette le body). **Ne pas utiliser `curl -I`** : ce flag envoie un **HEAD**, méthode que le serveur ne gère pas (parseur → `501`). Idem `-X` pour tester une méthode précise.

### 9.1 Cas nominal — `200` + bon `Content-Type`

```sh
curl -v http://localhost:8080/                    # 200  ./www/index.html (index résolu sur un dossier)
curl -v http://localhost:8080/index.html          # 200  fichier direct
curl -v http://localhost:8080/about.html          # 200  chemin nested sous location /
curl -s -D - -o /dev/null http://localhost:8080/style.css | grep -i content-type   # text/css
curl -s -D - -o /dev/null http://localhost:8080/about.html | grep -i content-type  # text/html
```

À vérifier : le body reçu correspond au fichier disque, et `Content-Length` = taille exacte du fichier (sinon keep-alive cassé).

### 9.2 Le mapping *alias* (préfixe de location retiré)

```sh
curl -v http://localhost:8080/uploads/hello.txt   # 200  /uploads retiré -> ./www/uploads/hello.txt
curl -s -D - -o /dev/null http://localhost:8080/uploads/hello.txt | grep -i content-type   # text/plain
```

Preuve du strip : le fichier est à `./www/uploads/hello.txt` **et** la `location /uploads` a `root ./www/uploads`. Si on avait servi en mode « root nginx » (préfixe gardé), le serveur aurait cherché `./www/uploads/uploads/hello.txt` → `404`. Un `200` prouve donc le comportement alias.

### 9.3 Matching de location — le plus long l'emporte, frontière stricte

```sh
# /uploads/hello.txt est servi par la location /uploads (5) pas par / (1)
curl -v http://localhost:8080/uploads/hello.txt   # 200  (si c'était / qui matchait: 404 car ./www/uploads/hello.txt via / donnerait ./www/uploads/hello.txt... voir note)
# frontière stricte : un chemin qui RESSEMBLE à /uploads mais n'est pas une sous-ressource
curl -v http://localhost:8080/uploadsxyz          # géré par location / (pas /uploads) -> 404 (./www/uploadsxyz absent)
```

> Note pédagogique : `/uploadsxyz` ne doit **jamais** être capté par `location /uploads`. S'il l'était, il serait résolu contre `./www/uploads` et pourrait fuiter un fichier inattendu. Le test confirme la frontière sur `/`.

### 9.4 Résolution de l'`index`

```sh
curl -v http://localhost:8080/                    # 200  dossier ./www -> index.html trouvé
curl -v http://localhost:9090/                    # 200  dossier ./www -> index.html (port 9090, autre server)
```

### 9.5 `404 Not Found`

```sh
curl -v http://localhost:8080/nope.html           # 404  fichier absent
curl -v http://localhost:8080/a/b/c/d.html        # 404  chemin profond absent
```

### 9.6 `403 Forbidden` — dossier sans index (autoindex off)

```sh
curl -v http://localhost:8080/private/            # 403  ./www/private existe, pas d'index, autoindex off
curl -v http://localhost:8080/private/note.txt    # 200  le fichier lui-même reste servable
```

> Quand l'autoindex sera implémenté (phase 3), `/private/` restera `403` (car `location /` a `autoindex off`), mais `http://localhost:9090/` (autoindex on) listera le dossier au lieu de servir l'index si l'index disparaissait.

### 9.7 `405 Method Not Allowed` — + header `Allow`

```sh
curl -v -X POST   http://localhost:8080/          # 405, POST interdit sur location / (GET only)
curl -v -X DELETE http://localhost:8080/          # 405, idem
curl -sD - -o /dev/null -X POST http://localhost:8080/ | grep -i allow   # Allow: GET
```

Le header `Allow` doit lister **exactement** les méthodes de la location (ici `GET`). Sur `/uploads` (GET, POST, DELETE) :

```sh
curl -sD - -o /dev/null -X PATCH http://localhost:8080/uploads/hello.txt 2>/dev/null
# NB: PATCH est rejeté 501 par le PARSEUR avant le routing (méthode hors GET/POST/DELETE).
# Pour tester le 405 du router, utiliser une méthode connue mais interdite sur la route.
```

### 9.8 `301` redirection

```sh
curl -v http://localhost:8080/old                 # 301, Location: http://localhost:8080/
curl -sD - -o /dev/null http://localhost:8080/old | grep -i location   # Location: http://localhost:8080/
curl -vL http://localhost:8080/old                # -L suit la redirection -> aboutit sur l'index
# la redirection ignore la méthode ET l'existence d'un fichier :
curl -v -X POST http://localhost:8080/old         # 301 quand même (court-circuit avant le check méthode)
```

### 9.9 `501` — méthode connue mais handler pas encore écrit

```sh
curl -v -X POST   http://localhost:8080/uploads/hello.txt   # 501 (POST autorisé sur /uploads, upload pas codé)
curl -v -X DELETE http://localhost:8080/uploads/hello.txt   # 501 (DELETE autorisé, pas codé)
```

> Deviendra `201`/`200`/`204` quand upload et DELETE seront faits. À ne pas confondre avec le `501` du **parseur** (méthode totalement inconnue comme `PATCH`).

### 9.10 Path traversal — doit **toujours** échouer en `403`

```sh
curl -v 'http://localhost:8080/../etc/passwd'                 # 403
curl -v 'http://localhost:8080/uploads/../../etc/passwd'      # 403
curl -v 'http://localhost:8080/a/b/../../../../etc/passwd'    # 403
curl -v 'http://localhost:8080/%2e%2e/%2e%2e/etc/passwd'      # 403 (encodé : décodé par le parseur, puis normalisé)
curl -v 'http://localhost:8080/uploads/./../../etc/passwd'    # 403 (le "." est ignoré, les ".." remontent trop haut)
```

Aucune de ces requêtes ne doit renvoyer un contenu hors du root, ni `200`, ni `404` sur un vrai fichier système. **Toujours `403`.** C'est le test de sécurité central du routing.

### 9.11 Virtual hosts — sélection par `Host`

```sh
curl -v -H 'Host: localhost'     http://localhost:8080/       # 200  ./www/index.html
curl -v -H 'Host: site-b.local'  http://localhost:8080/       # 200  ./www-b/index.html
curl -v -H 'Host: inconnu.xyz'   http://localhost:8080/       # 200  ./www (1er vhost = défaut)
```

Comparer les corps : `localhost` et `site-b.local` renvoient deux pages différentes **sur le même port** — c'est la preuve du routing par `Host`.

### 9.12 MIME — table d'extensions

```sh
for ext in html css js json txt png; do
  curl -s -D - -o /dev/null "http://localhost:8080/probe.$ext" | grep -i content-type
done
# (créer des fichiers www/probe.* pour un test exhaustif ; sinon 404)
```

### 9.13 Récap — matrice attendue

| Requête | Code | Pourquoi |
|---|---|---|
| `GET /` | 200 | index.html du dossier root |
| `GET /style.css` | 200 `text/css` | fichier + MIME |
| `GET /uploads/hello.txt` | 200 | mapping alias (préfixe retiré) |
| `GET /uploadsxyz` | 404 | frontière stricte (pas capté par /uploads) |
| `GET /private/` | 403 | dossier sans index, autoindex off |
| `GET /nope.html` | 404 | fichier absent |
| `POST /` | 405 + `Allow: GET` | méthode interdite sur la route |
| `POST /uploads/hello.txt` | 501 | méthode autorisée, handler pas encore écrit |
| `GET /old` | 301 + `Location` | redirection, court-circuit |
| `GET /../etc/passwd` | 403 | traversal bloqué |
| `GET /` avec `Host: site-b.local` | 200 (autre body) | virtual host |
